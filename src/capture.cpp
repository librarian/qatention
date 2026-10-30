#include "core/debuglog.h"
#include "core/media.h"
#include "core/session.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include <csignal>
#include <iostream>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif
static volatile std::sig_atomic_t interrupted = 0;

static void interrupt(int)
{
    interrupted = 1;
}

static QString passwordPrompt()
{
    std::cerr << "BMC password: " << std::flush;
#ifdef Q_OS_WIN
    const auto handle = GetStdHandle(STD_INPUT_HANDLE);
    DWORD old = 0;
    const bool terminal = GetConsoleMode(handle, &old);
    if (terminal) {
        SetConsoleMode(handle, old & ~ENABLE_ECHO_INPUT);
    }
#else
    termios old{}, hidden{};
    const bool terminal = tcgetattr(STDIN_FILENO, &old) == 0;
    if (terminal) {
        hidden = old;
        hidden.c_lflag &= ~tcflag_t(ECHO);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden);
    }
#endif
    std::string password;
    std::getline(std::cin, password);
#ifdef Q_OS_WIN
    if (terminal) {
        SetConsoleMode(handle, old);
    }
#else
    if (terminal) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &old);
    }
#endif
    std::cerr << "\n";
    return QString::fromUtf8(password.data(), qsizetype(password.size()));
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("qatention-capture");
    app.setApplicationVersion(QATENTION_VERSION);
#ifdef Q_OS_MACOS
    // The secondary executable may not be recognized as the app's main bundle.
    QCoreApplication::addLibraryPath(
        QCoreApplication::applicationDirPath() + "/../PlugIns");
#endif
    QCommandLineParser parser;
    parser.setApplicationDescription(
        "ATEN console capture and read-only ISO mounting.");
    parser.addHelpOption();
    parser.addVersionOption();
    for (const auto& name:
         {"host",
          "user",
          "output",
          "raw-output",
          "mount-iso",
          "jnlp",
          "vmedia-host",
          "vmedia-port",
          "client-pfx",
          "timeout",
          "web-port",
          "replay-video"})
    {
        parser.addOption(
            QCommandLineOption(name, QString("Set %1.").arg(name), "value"));
    }
    for (const auto& name:
         {"http", "vmedia-plain", "check-native-decoder", "verify-cert"})
    {
        parser.addOption(
            QCommandLineOption(name, QString("Enable %1.").arg(name)));
    }
    parser.process(app);
    try {
        if (parser.isSet("replay-video")) {
            std::cout << aten::replayVideoTrace(
                             parser.value("replay-video"),
                             parser.value("output"))
                             .toStdString()
                      << "\n";
            return 0;
        }
        if (parser.isSet("check-native-decoder")) {
            aten::Decoder decoder;
            decoder.resize({8, 8});
            // Known 8x8 ASPEED VQ white block used to verify native linkage.
            if (!decoder.decode(
                    QByteArray::fromHex("000001bc0000205300000000"),
                    0x57) ||
                decoder.image().pixel(0, 0) != qRgb(255, 255, 255))
            {
                throw aten::Error("Native decoder check failed");
            }
            std::cout << "ASPEED decoder OK\n";
            return 0;
        }
        if (!parser.isSet("host")) {
            throw aten::Error("--host is required");
        }
        auto integer = [&](const QString& name, int fallback, int max)
        {
            if (!parser.isSet(name)) {
                return fallback;
            }
            bool ok = false;
            const int value = parser.value(name).toInt(&ok);
            if (!ok || value < 1 || value > max) {
                throw aten::Error(("Invalid --" + name).toStdString());
            }
            return value;
        };
        const int seconds = integer("timeout", 30, 86400);
        const int mediaPort = integer("vmedia-port", 0, 65535);
        const bool mount = parser.isSet("mount-iso");
        const QString output = parser.isSet("output")
                                   ? parser.value("output")
                                   : QDateTime::currentDateTimeUtc().toString(
                                         "'capture-'yyyyMMdd-HHmmss'.bmp'");
        if (!mount && QFile::exists(output)) {
            throw aten::Error("Refusing to overwrite an existing screenshot");
        }
        if (parser.isSet("raw-output") &&
            QFile::exists(parser.value("raw-output")))
        {
            throw aten::Error("Refusing to overwrite an existing raw packet");
        }
        app.setProperty("clientPfx", parser.value("client-pfx"));
        QVariantMap options = {
            {"host", parser.value("host")},
            {"user", parser.value("user")},
            {"autoReconnect", false},
            {"allowUntrusted", !parser.isSet("verify-cert")},
            {"http", parser.isSet("http")},
            {"webPort",
             integer("web-port", parser.isSet("http") ? 80 : 443, 65535)}};
        aten::Session session;
        aten::Media media;
        aten::DebugLog log(
            QFileInfo(output).absolutePath() + "/" +
            QFileInfo(output).completeBaseName() + ".log");
        log.setEnabled(true);
        QObject::connect(
            &session,
            &aten::Session::diagnostic,
            &log,
            &aten::DebugLog::write);
        QObject::connect(
            &media,
            &aten::Media::diagnostic,
            &log,
            &aten::DebugLog::write);
        aten::Launch endpoint;
        if (parser.isSet("jnlp")) {
            QFile file(parser.value("jnlp"));
            if (!file.open(QIODevice::ReadOnly) || file.size() > 1024LL * 1024)
            {
                throw aten::Error("Cannot read JNLP");
            }
            endpoint = aten::parseJnlp(file.readAll());
            endpoint.host = parser.value("host");
            options["autoArm"] = false;
            options["user"] = endpoint.user;
            options["password"] = endpoint.password;
            options["port"] = endpoint.port;
            options["mediaPort"] = endpoint.mediaPort;
            options["tls"] = endpoint.tls;
        } else {
            if (parser.value("user").isEmpty()) {
                throw aten::Error("--user is required unless --jnlp is used");
            }
            options["password"] = passwordPrompt();
        }
        bool finishing = false, mounted = false, started = false,
             rawSaved = false;
        bool shutdownStarted = false;
        int exitCode = 0;
        auto exitWhenClean = [&]
        {
            if (shutdownStarted && !media.busy() && !session.logoutPending()) {
                app.exit(exitCode);
            }
        };
        QObject::connect(
            &session,
            &aten::Session::logoutsFinished,
            &app,
            exitWhenClean);
        auto finish = [&](int result)
        {
            if (finishing) {
                return;
            }
            finishing = true;
            exitCode = result;
            QTimer::singleShot(
                0,
                &app,
                [&]
                {
                    media.unmount();
                    session.close();
                    shutdownStarted = true;
                    exitWhenClean();
                });
        };
        auto attach = [&]
        {
            auto mediaEndpoint =
                endpoint.host.isEmpty() ? session.mediaEndpoint() : endpoint;
            if (parser.isSet("vmedia-host")) {
                mediaEndpoint.host = parser.value("vmedia-host");
            }
            if (mediaPort) {
                mediaEndpoint.mediaPort = quint16(mediaPort);
            }
            if (parser.isSet("vmedia-plain")) {
                mediaEndpoint.tls = false;
            }
            started = true;
            session.setKeepMedia(true);
            media.mount(
                mediaEndpoint,
                !parser.isSet("verify-cert"),
                parser.value("mount-iso"));
        };
        QObject::connect(
            &media,
            &aten::Media::changed,
            &app,
            [&]
            {
                session.setKeepMedia(media.busy());
                exitWhenClean();
                std::cerr << media.status().toStdString() << "\n";
                if (media.status().startsWith("Mounted:")) {
                    mounted = true;
                }
                if (started && !media.busy() && !finishing) {
                    finish(1);
                }
            });
        QObject::connect(
            &session,
            &aten::Session::stateChanged,
            &app,
            [&]
            {
                if (finishing) {
                    return;
                }
                if (session.connected() && mount && !started) {
                    attach();
                } else if (
                    !session.busy() && session.status() != "Disconnected") {
                    std::cerr << session.status().toStdString() << "\n";
                    finish(1);
                }
            });
        QObject::connect(
            &session,
            &aten::Session::frameReady,
            &app,
            [&](const QImage& frame)
            {
                if (mount || finishing) {
                    return;
                }
                if (!frame.save(output, "BMP")) {
                    std::cerr << "Cannot save screenshot\n";
                    finish(1);
                } else {
                    std::cout << "Saved " << output.toStdString() << "\n";
                    finish(0);
                }
            });
        QObject::connect(
            &session,
            &aten::Session::videoPacket,
            &app,
            [&](const QByteArray& packet, quint32 encoding)
            {
                if (rawSaved || !parser.isSet("raw-output") ||
                    encoding != 0x57) {
                    return;
                }
                QFile file(parser.value("raw-output"));
                if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) ||
                    file.write(packet) != packet.size())
                {
                    std::cerr << "Cannot save raw packet\n";
                    finish(1);
                } else {
                    rawSaved = true;
                }
            });
        std::signal(SIGINT, interrupt);
        std::signal(SIGTERM, interrupt);
        QTimer signalPoll;
        signalPoll.setInterval(100);
        QObject::connect(
            &signalPoll,
            &QTimer::timeout,
            &app,
            [&]
            {
                if (interrupted) {
                    finish(mounted ? 0 : 1);
                }
            });
        signalPoll.start();
        QTimer::singleShot(
            seconds * 1000,
            &app,
            [&]
            {
                if (!mount || !mounted) {
                    std::cerr << "Timed out\n";
                }
                finish(mount && mounted ? 0 : 1);
            });
        QTimer::singleShot(
            0,
            &app,
            [&]
            {
                if (mount && parser.isSet("jnlp")) {
                    attach();
                } else {
                    session.open(options);
                }
            });
        const int result = app.exec();
        media.disconnect(&app);
        session.disconnect(&app);
        return result;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
