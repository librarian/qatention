#include "debuglog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

#include <spdlog/logger.h>
#include <spdlog/sinks/rotating_file_sink.h>

namespace aten {
Q_LOGGING_CATEGORY(sessionLogging, "qatention.session", QtInfoMsg)

DebugLog::DebugLog(const QString& path, QObject* parent)
    : QObject(parent)
    , filePath(path)
{}

DebugLog::~DebugLog() = default;

void DebugLog::setEnabled(bool enabled)
{
    if (active == enabled) {
        return;
    }
    active = enabled;
    if (active) {
        write("Debug logging enabled");
    } else {
        logger.reset();
    }
    emit enabledChanged();
}

void DebugLog::appendHistory(const QString& line)
{
    lines += line;
    constexpr qsizetype MaxHistoryCharacters = 65536;
    if (lines.size() > MaxHistoryCharacters) {
        const auto boundary =
            lines.indexOf('\n', lines.size() - MaxHistoryCharacters);
        lines.remove(
            0,
            boundary < 0 ? lines.size() - MaxHistoryCharacters : boundary + 1);
    }
}

void DebugLog::write(const QString& message)
{
    if (!active) {
        return;
    }
    qCDebug(sessionLogging).noquote() << message;
    const auto line =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) + "  " +
        message;
    appendHistory(line + "\n");
    try {
        if (!logger) {
            if (!QDir().mkpath(QFileInfo(filePath).absolutePath())) {
                throw spdlog::spdlog_ex("Cannot create log directory");
            }
            spdlog::file_event_handlers handlers;
            handlers.after_open =
                [path = filePath](const spdlog::filename_t&, std::FILE*)
            {
                if (!QFile::setPermissions(
                        path,
                        QFileDevice::ReadOwner | QFileDevice::WriteOwner))
                {
                    throw spdlog::spdlog_ex(
                        "Cannot restrict log file permissions");
                }
            };
#ifdef SPDLOG_WCHAR_FILENAMES
            const auto filename = filePath.toStdWString();
#else
            const auto filename = QFile::encodeName(filePath).toStdString();
#endif
            auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                filename,
                static_cast<std::size_t>(MaxFileBytes),
                1,
                false,
                handlers);
            logger =
                std::make_shared<spdlog::logger>("session", std::move(sink));
            // The same UTC line goes to disk and the Qt viewer. spdlog owns
            // rotation and flushing; it must not add a second timestamp.
            logger->set_pattern("%v");
            logger->flush_on(spdlog::level::info);
            logger->set_error_handler([](const std::string& error)
                                      { throw spdlog::spdlog_ex(error); });
        }
        const auto bytes = line.toUtf8();
        logger->info(
            "{}",
            spdlog::string_view_t(
                bytes.constData(),
                static_cast<std::size_t>(bytes.size())));
    } catch (const spdlog::spdlog_ex& error) {
        logger.reset();
        appendHistory(
            "Cannot write log file: " + QString::fromUtf8(error.what()) + "\n");
    }
    emit changed();
}

void DebugLog::clear()
{
    logger.reset();
    lines.clear();
    if (QFile::exists(filePath) && !QFile::remove(filePath)) {
        appendHistory("Cannot clear log file.\n");
    }
    emit changed();
}

void DebugLog::capture(const QByteArray& packet)
{
    if (!active || captured || packet.isEmpty() ||
        packet.size() > 16LL * 1024 * 1024)
    {
        return;
    }
    captured = true;
    QFile out(path() + "-unsupported-frame.bin");
    if (out.open(QIODevice::WriteOnly)) {
        out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        if (out.write(packet) == packet.size()) {
            write(
                "Saved unsupported video packet (may contain console "
                "pixels): " +
                out.fileName());
            return;
        }
    }
    write("Could not save unsupported video packet: " + out.errorString());
}
}   // namespace aten
