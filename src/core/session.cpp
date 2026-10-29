#include "session.h"

#include "certificatechain.h"
#include "media.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QSslConfiguration>
#include <QSslKey>
#include <QUrlQuery>

static void initCertificateResource()
{
    Q_INIT_RESOURCE(vendor_certificate);
}

namespace aten {
static constexpr qsizetype MaxPacket = 32LL * 1024 * 1024;

Session::Session(QObject* parent)
    : QObject(parent)
{
    initCertificateResource();
    retry.setSingleShot(true);
    connect(
        &retry,
        &QTimer::timeout,
        this,
        [this] { startAttempt(reconnectOptions); });
    deadline.setSingleShot(true);
    nextFrame.setSingleShot(true);
    connect(
        &deadline,
        &QTimer::timeout,
        this,
        [this] { fail("Connection or handshake timed out", true); });
    connect(&nextFrame, &QTimer::timeout, this, &Session::requestFrame);
    heartbeat.setInterval(3000);
    connect(
        &heartbeat,
        &QTimer::timeout,
        this,
        [this]
        {
            if (!connected()) {
                return;
            }
            emit diagnostic(QString(
                                "Pipeline: requests=%1 full=%2 replies=%3 "
                                "decoded=%4 pending=%5 streaming=%6 "
                                "keys=%7 pointers=%8 rejectedInput=%9")
                                .arg(requests)
                                .arg(fullRequests)
                                .arg(updates)
                                .arg(frames)
                                .arg(pending)
                                .arg(streaming)
                                .arg(keyEvents)
                                .arg(pointerEvents)
                                .arg(rejectedInput));
            if (videoTrace.active()) {
                videoTrace.record(
                    {{"type", "input-counts"},
                     {"keys", double(keyEvents)},
                     {"pointers", double(pointerEvents)}});
            }
            if (received.elapsed() > 60000) {
                fail("BMC stopped responding", true);
                return;
            }
            send(QByteArray::fromHex("150000000100000000"));
            if (powerPoll.elapsed() >= 10000) {
                send(QByteArray(1, 0x37));
                powerPoll.restart();
            }
            // Other replies can keep the connection alive while an incremental
            // video request remains unanswered. Check the video deadline
            // independently.
            requestFrame();
        });
}

void Session::reportTlsErrors(
    const QString& endpoint,
    const QList<QSslError>& errors)
{
    QStringList details;
    for (const auto& error: errors) {
        details.append(error.errorString());
        const auto cert = error.certificate();
        emit diagnostic(
            endpoint + " TLS: " + error.errorString() + "; subject=" +
            cert.subjectInfo(QSslCertificate::CommonName).join(",") +
            "; issuer=" +
            cert.issuerInfo(QSslCertificate::CommonName).join(",") +
            "; SHA256=" +
            QString::fromLatin1(
                cert.digest(QCryptographicHash::Sha256).toHex()));
    }
    tlsFailure =
        endpoint + " certificate validation failed: " + details.join("; ");
}

void Session::setStatus(const QString& text)
{
    if (statusText == text) {
        return;
    }
    statusText = text;
    emit diagnostic(text);
    emit stateChanged();
}

void Session::close()
{
    retry.stop();
    reconnectOptions.clear();
    established = false;
    retryCount = 0;
    resetTransport();
    mediaLaunch = {};
}

void Session::resetTransport()
{
    if (closing) {
        return;
    }
    QScopedValueRollback guard(closing, true);
    releaseInput();
    ++generation;
    delete issuerFetch;
    issuerFetch = nullptr;
    deadline.stop();
    heartbeat.stop();
    nextFrame.stop();
    if (reply) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
        reply = nullptr;
    }
    if (socket) {
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
        socket = nullptr;
    }
    if (webAuthenticated) {
        const auto cookies = web.cookieJar()->cookiesForUrl(baseUrl);
        const auto url = baseUrl;
        const bool trust = allowUntrusted;
        auto logout = [cookies, url, trust]
        {
            // Survive closing a tab long enough to finish its bounded logout
            // request.
            auto* manager =
                new QNetworkAccessManager(QCoreApplication::instance());
            auto* jar = new QNetworkCookieJar(manager);
            jar->setCookiesFromUrl(cookies, url);
            manager->setCookieJar(jar);
            QNetworkRequest request(url.resolved(QUrl("/cgi/logout.cgi")));
            request.setTransferTimeout(3000);
            auto* r = manager->get(request);
            QObject::connect(
                r,
                &QNetworkReply::sslErrors,
                manager,
                [r, trust](const QList<QSslError>& errors)
                {
                    if (trust ||
                        (CertificateChain::missingIssuerOnly(errors) &&
                         CertificateChain::verify(
                             CertificateChain::cached(
                                 r->sslConfiguration().peerCertificateChain()),
                             r->url().host())))
                    {
                        r->ignoreSslErrors(errors);
                    }
                });
            QObject::connect(
                r,
                &QNetworkReply::finished,
                manager,
                &QObject::deleteLater);
        };
        if (keepMedia) {
            deferredLogouts.append(logout);
        } else {
            logout();
        }
        webAuthenticated = false;
    }
    web.setCookieJar(new QNetworkCookieJar(&web));
    tlsFailure.clear();
    consoleCandidate = QSslCertificate();
    buffer.clear();
    launch = {};
    decoder.reset();
    pending = false;
    forceFull = false;
    controlling = true;
    if (videoTrace.active()) {
        videoTrace.record({{"type", "reset"}});
    }
    requests = fullRequests = updates = keyEvents = pointerEvents =
        rejectedInput = 0;
    phase = Phase::Idle;
    emit clearFrame();
    setStatus("Disconnected");
}

void Session::fail(const QString& text, bool transient)
{
    if (closing) {
        return;
    }
    if (transient && established && !reconnectOptions.isEmpty() &&
        retryCount < 30)
    {
        resetTransport();
        const int seconds = ++retryCount <= 5 ? 5 : 30;
        phase = Phase::Reconnecting;
        setStatus(
            text +
            QString(" — reconnect %1/30 in %2 seconds (Disconnect cancels)")
                .arg(retryCount)
                .arg(seconds));
        retry.start(seconds * 1000);
    } else {
        close();
        setStatus(text);
    }
}

void Session::open(const QVariantMap& o)
{
    close();
    emit diagnostic("TLS backend: " + QSslSocket::activeBackend());
    if (o.value("autoArm", true).toBool() &&
        o.value("autoReconnect", true).toBool())
    {
        reconnectOptions = o;
    }
    startAttempt(o);
}

void Session::startAttempt(const QVariantMap& o)
{
    try {
        consoleTrust = o.value("consoleTrust").toMap();
        allowUntrusted = o.value("allowUntrusted", true).toBool();
        switchJava = o.value("switchJava").toBool();
        // Streaming cadence is controlled by the View menu and survives
        // reconnects.
        launch.host = o.value("host").toString().trimmed();
        if (launch.host.contains("://")) {
            launch.host = QUrl(launch.host).host();
        } else {
            if (launch.host.contains('/')) {
                launch.host = launch.host.section('/', 0, 0);
            }
            if (launch.host.contains('@')) {
                launch.host = launch.host.section('@', -1);
            }
            if (launch.host.startsWith('[')) {
                launch.host = launch.host.section(']', 0, 0).mid(1);
            } else if (launch.host.count(':') == 1) {
                launch.host = launch.host.section(':', 0, 0);
            }
        }
        if (launch.host.isEmpty() || launch.host.contains('/') ||
            launch.host.contains('@'))
        {
            throw Error("Enter a BMC hostname or IP address");
        }
        launch.user = o.value("user").toString();
        launch.password = o.value("password").toString();
        if (launch.user.isEmpty() || launch.password.isEmpty()) {
            throw Error("Enter credentials");
        }
        if (o.value("autoArm", true).toBool()) {
            phase = Phase::Web;
            baseUrl = QUrl();
            baseUrl.setScheme(
                o.value("http", false).toBool() ? "http" : "https");
            baseUrl.setHost(launch.host);
            auto webPort = o.value("webPort", 443).toInt();
            if (webPort < 1 || webPort > 65535) {
                throw Error("Invalid HTTPS port");
            }
            baseUrl.setPort(webPort);
            // QUrlQuery leaves '+' literal; form-urlencoded servers interpret
            // that as a space.
            QByteArray body =
                "name=" + QUrl::toPercentEncoding(launch.user) +
                "&pwd=" + QUrl::toPercentEncoding(launch.password);
            launch.user.clear();
            launch.password.clear();
            setStatus("Logging in to BMC…");
            requestWeb("/cgi/login.cgi", body, 0);
        } else {
            auto port = o.value("port", 5900).toInt();
            if (port < 1 || port > 65535) {
                throw Error("Invalid KVM port");
            }
            launch.mediaPort =
                quint16(qBound(1, o.value("mediaPort", 623).toInt(), 65535));
            launch.port = quint16(port);
            launch.tls = o.value("tls", true).toBool();
            credentials(launch.user, launch.password);
            connectKvm();
        }
    } catch (const std::exception& e) {
        fail(QString::fromUtf8(e.what()));
    }
}

void Session::requestWeb(
    const QString& path,
    const QByteArray& body,
    int step,
    const QByteArray& csrf,
    bool chainRetried)
{
    tlsFailure.clear();
    emit diagnostic(QString("HTTPS request step %1 to %2:%3: %4")
                        .arg(step)
                        .arg(baseUrl.host())
                        .arg(baseUrl.port())
                        .arg(path));
    QNetworkRequest request(baseUrl.resolved(QUrl(path)));
    request.setHeader(
        QNetworkRequest::UserAgentHeader,
        "Mozilla/5.0 QATENtion");
    request.setRawHeader("Referer", baseUrl.toEncoded());
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::SameOriginRedirectPolicy);
    request.setTransferTimeout(15000);
    auto ssl = request.sslConfiguration();
    ssl.setProtocol(QSsl::TlsV1_2OrLater);
    request.setSslConfiguration(ssl);
    if (!csrf.isEmpty()) {
        request.setRawHeader("CSRF_TOKEN", csrf);
    }
    if (!body.isEmpty()) {
        request.setHeader(
            QNetworkRequest::ContentTypeHeader,
            "application/x-www-form-urlencoded");
    }
    auto* r = body.isEmpty() ? web.get(request) : web.post(request, body);
    reply = r;
    const auto epoch = generation;
    auto missingChain = std::make_shared<QList<QSslCertificate>>();
    connect(
        r,
        &QNetworkReply::sslErrors,
        this,
        [this, r, missingChain](const QList<QSslError>& errors)
        {
            reportTlsErrors("HTTPS", errors);
            if (allowUntrusted) {
                r->ignoreSslErrors();
                return;
            }
            if (!CertificateChain::missingIssuerOnly(errors)) {
                return;
            }
            const auto peer = r->sslConfiguration().peerCertificateChain();
            if (CertificateChain::verify(
                    CertificateChain::cached(peer),
                    r->url().host()))
            {
                emit diagnostic(
                    "HTTPS: peer chain verified using cached intermediates");
                r->ignoreSslErrors(errors);
            } else {
                *missingChain = peer;
            }
        });
    connect(
        r,
        &QNetworkReply::readyRead,
        this,
        [this, r]
        {
            if (r->bytesAvailable() > 1024LL * 1024) {
                fail("BMC web response is too large");
            }
        });
    connect(
        r,
        &QNetworkReply::finished,
        this,
        [this, r, step, epoch, path, body, csrf, chainRetried, missingChain]
        {
            if (epoch != generation) {
                r->deleteLater();
                return;
            }
            reply = nullptr;
            r->deleteLater();
            if (r->error() == QNetworkReply::SslHandshakeFailedError &&
                !chainRetried && !missingChain->isEmpty())
            {
                // The TLS handshake failed before any HTTP credentials were
                // sent.
                issuerFetch = new QObject(this);
                CertificateChain::fetch(
                    issuerFetch,
                    *missingChain,
                    r->url().host(),
                    [this, epoch](const QString& message)
                    {
                        if (epoch == generation) {
                            emit diagnostic(message);
                        }
                    },
                    [this, epoch, path, body, step, csrf](bool ok)
                    {
                        if (epoch != generation) {
                            return;
                        }
                        issuerFetch->deleteLater();
                        issuerFetch = nullptr;
                        if (ok) {
                            requestWeb(path, body, step, csrf, true);
                        } else {
                            const auto error = tlsFailure;
                            fail(error);
                        }
                    });
                return;
            }
            if (r->error() != QNetworkReply::NoError) {
                fail(
                    tlsFailure.isEmpty()
                        ? "BMC web request failed: " + r->errorString()
                        : tlsFailure,
                    r->error() == QNetworkReply::ConnectionRefusedError ||
                        r->error() == QNetworkReply::RemoteHostClosedError ||
                        r->error() == QNetworkReply::HostNotFoundError ||
                        r->error() == QNetworkReply::TimeoutError ||
                        r->error() ==
                            QNetworkReply::TemporaryNetworkFailureError);
                return;
            }
            tlsFailure.clear();
            auto b = r->readAll();
            emit diagnostic(QString("Web response step %1: %2 bytes")
                                .arg(step)
                                .arg(b.size()));
            if (b.size() > 1024LL * 1024) {
                fail("BMC web response is too large");
                return;
            }
            try {
                handleWeb(b, step);
            } catch (const std::exception& e) {
                fail(QString::fromUtf8(e.what()));
            }
        });
}

void Session::handleWeb(const QByteArray& body, int step)
{
    const QString jnlpPath =
        "/cgi/url_redirect.cgi?url_name=ikvm&url_type=jwsk";
    if (step == 0) {
        if (web.cookieJar()->cookiesForUrl(baseUrl).isEmpty()) {
            throw Error(
                "BMC did not create a login session; check credentials");
        }
        webAuthenticated = true;
        setStatus("Requesting console credentials…");
        if (switchJava) {
            requestWeb("/cgi/url_redirect.cgi?url_name=topmenu", {}, 1);
        } else {
            requestWeb(jnlpPath, {}, 3);
        }
    } else if (step == 1) {
        static const QRegularExpression re(
            R"re(SmcCsrfInsert\s*\(\s*["']CSRF_TOKEN["']\s*,\s*["']([^"']+)["'])re",
            QRegularExpression::CaseInsensitiveOption);
        const auto m = re.match(QString::fromUtf8(body));
        if (m.hasMatch()) {
            requestWeb(
                "/cgi/op.cgi",
                "op=remote_console&ikvm_setting=0",
                2,
                m.captured(1).toLatin1());
        } else {
            requestWeb(jnlpPath, {}, 3);
        }
    } else if (step == 2) {
        requestWeb(jnlpPath, {}, 3);
    } else {
        const auto originalHost = launch.host;
        launch = parseJnlp(body);
        // Use the endpoint explicitly chosen by the user (JNLP may contain an
        // unroutable address).
        launch.host = originalHost;
        connectKvm();
    }
}

QSslCertificate Session::mediaTrustedCertificate() const
{
    // Only transfer explicit trust from the authenticated console on this host.
    if (!connected() || !socket || !socket->isEncrypted() ||
        consoleTrust.isEmpty() ||
        mediaLaunch.host.compare(launch.host, Qt::CaseInsensitive) != 0)
    {
        return QSslCertificate();
    }
    return socket->peerCertificate();
}

void Session::connectKvm()
{
    mediaLaunch = launch;
    tlsFailure.clear();
    emit diagnostic(QString("Console endpoint %1:%2, TLS=%3")
                        .arg(launch.host)
                        .arg(launch.port)
                        .arg(launch.tls));
    phase = Phase::Connecting;
    setStatus("Connecting to console…");
    decoder = std::make_unique<Decoder>();
    socket = new QSslSocket(this);
    socket->setReadBufferSize(MaxPacket + 4096);
    connect(socket, &QSslSocket::readyRead, this, &Session::process);
    connect(
        socket,
        &QSslSocket::disconnected,
        this,
        [this]
        {
            if (busy()) {
                fail("BMC disconnected", true);
            }
        });
    connect(
        socket,
        &QSslSocket::errorOccurred,
        this,
        [this](QAbstractSocket::SocketError error)
        {
            if (!socket) {
                return;
            }
            const auto candidate = consoleCandidate;
            const auto host = launch.host;
            const auto port = launch.port;
            fail(
                tlsFailure.isEmpty()
                    ? "Console connection failed: " + socket->errorString()
                    : tlsFailure,
                error != QAbstractSocket::SslHandshakeFailedError);
            if (error == QAbstractSocket::SslHandshakeFailedError &&
                !candidate.isNull())
            {
                emit consoleCertificateUntrusted(host, port, candidate);
            }
        });
    auto pinMatches = [this]
    {
        return consoleTrust.value("host").toString().compare(
                   launch.host,
                   Qt::CaseInsensitive) == 0 &&
               consoleTrust.value("port").toUInt() == launch.port &&
               consoleTrust.value("sha256").toString() ==
                   QString::fromLatin1(socket->peerCertificate()
                                           .digest(QCryptographicHash::Sha256)
                                           .toHex());
    };
    auto begin = [this, pinMatches]
    {
        if (!consoleTrust.isEmpty() && (!launch.tls || !pinMatches())) {
            fail(
                "Console certificate or endpoint changed. Connection blocked; "
                "review the saved profile's trusted console certificate.");
            return;
        }
        tlsFailure.clear();
        phase = Phase::Banner;
        deadline.start(15000);
        setStatus("Authenticating console…");
        process();
    };
    connect(
        socket,
        &QSslSocket::connected,
        this,
        [this, begin]
        {
            socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
            if (!launch.tls) {
                begin();
            }
        });
    deadline.start(15000);
    if (launch.tls) {
        if (!QSslSocket::supportsSsl()) {
            throw Error("No TLS backend available");
        }
        configureClientTls(socket);
        connect(
            socket,
            &QSslSocket::sslErrors,
            this,
            [this, pinMatches](const QList<QSslError>& errors)
            {
                reportTlsErrors("Console", errors);
                if (!consoleTrust.isEmpty() && !pinMatches()) {
                    tlsFailure =
                        "Console certificate or endpoint changed. Connection "
                        "blocked; review the saved profile's trusted console "
                        "certificate.";
                    return;
                }
                if (consoleTrust.isEmpty() && allowUntrusted) {
                    socket->ignoreSslErrors();
                    return;
                }
                // Explicit pinning permits identity/trust errors, not expiry or
                // invalid signatures.
                for (const auto& error: errors) {
                    switch (error.error()) {
                        case QSslError::HostNameMismatch:
                        case QSslError::SelfSignedCertificate:
                        case QSslError::SelfSignedCertificateInChain:
                        case QSslError::UnableToGetIssuerCertificate:
                        case QSslError::UnableToGetLocalIssuerCertificate:
                        case QSslError::UnableToVerifyFirstCertificate:
                            break;
                        default:
                            return;
                    }
                }
                if (!consoleTrust.isEmpty()) {
                    emit diagnostic(
                        "Console TLS: saved certificate fingerprint matched");
                    socket->ignoreSslErrors(errors);
                } else {
                    consoleCandidate = socket->peerCertificate();
                }
            });
        connect(socket, &QSslSocket::encrypted, this, begin);
        socket->connectToHostEncrypted(launch.host, launch.port);
    } else {
        socket->connectToHost(launch.host, launch.port);
    }
}

void Session::send(const QByteArray& bytes)
{
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    if (socket->bytesToWrite() > 1024LL * 1024) {
        fail("BMC stopped accepting input");
        return;
    }
    if (socket->write(bytes) != bytes.size()) {
        fail("Failed to send console message");
    }
}

void Session::process()
{
    if (!socket) {
        return;
    }
    buffer += socket->readAll();
    if (buffer.size() > MaxPacket + 4096) {
        fail("Console receive limit exceeded");
        return;
    }
    try {
        for (int work = 0; work < 128; ++work) {
            switch (phase) {
                case Phase::Banner:
                    if (buffer.size() < 12) {
                        return;
                    }
                    if (!QRegularExpression("^RFB [0-9]{3}\\.[0-9]{3}\\n$")
                             .match(QString::fromLatin1(buffer.first(12)))
                             .hasMatch())
                    {
                        throw Error("Invalid RFB banner");
                    }
                    send(buffer.first(12));
                    buffer.remove(0, 12);
                    phase = Phase::Security;
                    break;
                case Phase::Security: {
                    if (buffer.isEmpty()) {
                        return;
                    }
                    const int count = quint8(buffer[0]);
                    if (!count) {
                        throw Error("BMC refused security negotiation");
                    }
                    if (buffer.size() < count + 1) {
                        return;
                    }
                    const auto types = buffer.mid(1, count);
                    if (!types.contains(char(16))) {
                        throw Error(
                            "Only ATEN token authentication (type 16) is "
                            "supported");
                    }
                    send(QByteArray(1, char(16)));
                    buffer.remove(0, count + 1);
                    phase = Phase::Challenge;
                    break;
                }
                case Phase::Challenge:
                    if (buffer.size() < 24) {
                        return;
                    }
                    buffer.remove(0, 24);
                    send(credentials(launch.user, launch.password));
                    launch.user.clear();
                    launch.password.clear();
                    phase = Phase::Auth;
                    break;
                case Phase::Auth:
                    if (buffer.size() < 4) {
                        return;
                    }
                    if (be32(buffer, 0) != 0) {
                        throw Error(
                            "BMC rejected console credentials; obtain a fresh "
                            "token");
                    }
                    buffer.remove(0, 4);
                    send(QByteArray(1, 0));
                    phase = Phase::Init;
                    break;
                case Phase::Init: {
                    if (buffer.size() < 24) {
                        return;
                    }
                    const auto n = be32(buffer, 20);
                    if (n > 65536) {
                        throw Error("Server name is too long");
                    }
                    if (buffer.size() < 36 + n) {
                        return;
                    }
                    const QSize size(be16(buffer, 0), be16(buffer, 2));
                    decoder->resize(size);
                    buffer.remove(0, 36 + n);
                    phase = Phase::Messages;
                    established = true;
                    deadline.stop();
                    received.start();
                    fullFrame.start();
                    heartbeat.start();
                    uptime.start();
                    powerPoll.start();
                    videoBytes = frames = 0;
                    powerState = -1;
                    sessionUsers.clear();
                    userBurst.clear();
                    setStatus("Connected — click the console to send input");
                    send(QByteArray::fromHex("37070780360001"));
                    setDisplay(quality, imageMode);
                    forceFull = true;
                    requestFrame();
                    break;
                }
                case Phase::Messages:
                    if (!consumeMessage()) {
                        return;
                    }
                    received.restart();
                    retryCount = 0;
                    break;
                default:
                    return;
            }
        }
        QTimer::singleShot(0, this, &Session::process);
    } catch (const std::exception& e) {
        fail(QString::fromUtf8(e.what()));
    }
}

bool Session::consumeMessage()
{
    if (buffer.isEmpty()) {
        return false;
    }
    const auto type = quint8(buffer[0]);
    qsizetype length = 0;
    if (type == 0) {
        if (buffer.size() < 4) {
            return false;
        }
        const int count = be16(buffer, 2);
        if (count > 4096) {
            throw Error("Too many framebuffer rectangles");
        }
        length = 4;
        for (int i = 0; i < count; ++i) {
            if (buffer.size() < length + 20) {
                return false;
            }
            const auto n = be32(buffer, length + 16);
            if (n > MaxPacket || length + 20 + n > MaxPacket) {
                throw Error("Framebuffer update is too large");
            }
            length += 20 + n;
            if (buffer.size() < length) {
                return false;
            }
        }
        qsizetype pos = 4;
        bool changed = false, noSignal = false;
        for (int i = 0; i < count; ++i) {
            const int x = be16(buffer, pos), y = be16(buffer, pos + 2),
                      w = be16(buffer, pos + 4), h = be16(buffer, pos + 6);
            const auto encoding = be32(buffer, pos + 8),
                       n = be32(buffer, pos + 16);
            emit diagnostic(
                QString("Video rectangle %1x%2 at %3,%4, encoding %5, %6 bytes")
                    .arg(w)
                    .arg(h)
                    .arg(x)
                    .arg(y)
                    .arg(encoding)
                    .arg(n));
            if (!n) {
                noSignal |= w >= 0x8000 || h >= 0x8000;
            } else {
                if (x == 0 && y == 0 && decoder->image().size() != QSize(w, h))
                {
                    decoder->resize({w, h});
                    forceFull = true;
                }
                try {
                    const auto packet = buffer.mid(pos + 20, n);
                    emit videoPacket(packet, encoding);
                    if (!decoder) {
                        return false;
                    }
                    const bool decoded = decoder->decode(packet, encoding);
                    changed |= decoded;
                    if (videoTrace.active() &&
                        !videoTrace.record(
                            {{"type", "video"},
                             {"width", decoder->image().width()},
                             {"height", decoder->image().height()},
                             {"x", x},
                             {"y", y},
                             {"encoding", double(encoding)},
                             {"payload",
                              QString::fromLatin1(packet.toBase64())},
                             {"changed", decoded},
                             {"imageSha256",
                              QString::fromLatin1(
                                  imageHash(decoder->image()))}}))
                    {
                        emit diagnostic(
                            "Video trace stopped: size limit or write error");
                    }
                } catch (...) {
                    emit unsupportedFrame(buffer.mid(pos + 20, n));
                    throw;
                }
            }
            pos += 20 + n;
        }
        buffer.remove(0, length);
        pending = false;
        videoBytes += quint64(length);
        ++updates;
        if (noSignal) {
            emit clearFrame();
            setStatus("Connected — no video signal");
        } else if (changed) {
            ++frames;
            emit frameReady(decoder->image());
            setStatus(
                controlling ? "Connected — controlling"
                            : "Connected — view only");
        }
        nextFrame.start(frameInterval);
        return true;
    }
    switch (type) {
        case 4:
            if (buffer.size() < 21) {
                return false;
            }
            length = 21;
            if (be32(buffer, 17) == 1) {
                const quint64 pixels =
                    quint64(be32(buffer, 9)) * be32(buffer, 13);
                if (pixels > 1024LL * 1024) {
                    throw Error("Cursor bitmap is too large");
                }
                length = 25 + qsizetype(pixels) * 2;
            }
            break;
        case 0x16:
            length = 2;
            break;
        case 0x35:
            length = 6;
            break;
        case 0x37:
            length = 4;
            break;
        case 0x39:
            length = 265;
            break;
        case 0x3c:
            length = 9;
            break;
        default:
            throw Error(
                "Unknown server message; connection closed to preserve stream "
                "alignment");
    }
    if (buffer.size() < length) {
        return false;
    }
    if (type == 0x37) {
        powerState = buffer[3] ? 1 : 0;
        emit stateChanged();
    }
    if (type == 0x39) {
        const auto index = be32(buffer, 1), kind = be32(buffer, 5);
        const auto info =
            QString::fromLatin1(buffer.mid(9, 256).split('\0').first())
                .trimmed();
        const auto sid = info.section(' ', 0, 0);
        if (kind == 1) {
            if (index > 1) {
                userBurst.append(info);
            } else if (!userBurst.isEmpty()) {
                userBurst.append(info);
                sessionUsers = userBurst;
                userBurst.clear();
            } else {
                for (int i = sessionUsers.size() - 1; i >= 0; --i) {
                    if (sessionUsers[i].section(' ', 0, 0) == sid) {
                        sessionUsers.removeAt(i);
                    }
                }
                sessionUsers.append(info);
            }
        } else if (kind == 2) {
            for (int i = sessionUsers.size() - 1; i >= 0; --i) {
                if (sessionUsers[i].section(' ', 0, 0) == sid) {
                    sessionUsers.removeAt(i);
                }
            }
        } else if (kind != 7) {
            emit diagnostic(QString("BMC notice %1: %2").arg(kind).arg(info));
        }
        const bool grant = !(be32(buffer, 1) == 1 && be32(buffer, 5) == 4);
        if (!grant) {
            releaseInput();
        }
        controlling = grant;
        setStatus(grant ? "Connected — controlling" : "Connected — view only");
    }
    buffer.remove(0, length);
    return true;
}

void Session::requestFrame()
{
    if (!connected() || !streaming) {
        return;
    }
    const bool overdue = pending && frameRequested.isValid() &&
                         frameRequested.elapsed() >= 10000;
    const bool periodic =
        fullSeconds > 0 && fullFrame.elapsed() >= fullSeconds * 1000LL;
    const bool full = forceFull || duplicateEndpoint || fullSeconds < 0 ||
                      periodic || overdue;
    // Continuous-full mode still waits for replies; explicit/periodic refresh
    // and the watchdog may supersede an outstanding incremental request.
    if (pending && !forceFull && !periodic && !overdue) {
        return;
    }
    forceFull = false;
    pending = true;
    ++requests;
    if (full) {
        ++fullRequests;
    }
    if (videoTrace.active()) {
        videoTrace.record(
            {{"type", "request"},
             {"sequence", double(requests)},
             {"full", full}});
    }
    frameRequested.restart();
    send(updateRequest(decoder->image().size(), !full));
    if (full) {
        fullFrame.restart();
    }
}

void Session::refresh()
{
    forceFull = true;
    requestFrame();
}

void Session::key(quint32 usage, bool down)
{
    if (!connected() || !controlling || !usage) {
        ++rejectedInput;
        return;
    }
    ++keyEvents;
    if (down) {
        held.insert(usage);
    } else {
        held.remove(usage);
    }
    send(keyEvent(usage, down));
}

void Session::pointer(int x, int y, int mask)
{
    if (!connected() || !controlling) {
        ++rejectedInput;
        return;
    }
    ++pointerEvents;
    lastPointer = {
        qBound(0, x, decoder->image().width() - 1),
        qBound(0, y, decoder->image().height() - 1)};
    buttons = mask;
    send(pointerEvent(lastPointer.x(), lastPointer.y(), mask));
}

void Session::releaseInput()
{
    const auto keys = held;
    held.clear();
    const bool releaseButtons = buttons != 0;
    buttons = 0;
    for (auto usage: keys) {
        send(keyEvent(usage, false));
    }
    if (releaseButtons) {
        send(pointerEvent(lastPointer.x(), lastPointer.y(), 0));
    }
}

void Session::ctrlAltDelete()
{
    releaseInput();
    key(0xe0, true);
    key(0xe2, true);
    key(0x4c, true);
    key(0x4c, false);
    key(0xe2, false);
    key(0xe0, false);
}
}   // namespace aten

namespace aten {
void Session::power(int command)
{
    if (connected() && command >= 0 && command <= 3) {
        QByteArray b(2, 0);
        b[0] = 0x1a;
        b[1] = char(command);
        send(b);
    }
}

void Session::hotplug()
{
    if (connected()) {
        send(QByteArray(1, 0x3a));
    }
}

void Session::combo(const QList<quint32>& keys)
{
    releaseInput();
    for (auto k: keys) {
        key(k, true);
    }
    for (auto i = keys.crbegin(); i != keys.crend(); ++i) {
        key(*i, false);
    }
}

void Session::setDisplay(int q, int mode)
{
    quality = qBound(0, q, 11);
    imageMode = mode == 422 ? 422 : 444;
    if (connected()) {
        QByteArray b(5, 0);
        b[0] = 0x32;
        b[2] = char(quality);
        put16(b, 3, quint16(imageMode));
        send(b);
        refresh();
    }
}

void Session::setStreaming(bool active, int interval, int full)
{
    if (streaming != active || frameInterval != qBound(0, interval, 1000) ||
        fullSeconds != qBound(-1, full, 60))
    {
        emit diagnostic(
            QString(
                "Streaming: active=%1 intervalMs=%2 fullSeconds=%3 pending=%4")
                .arg(active)
                .arg(interval)
                .arg(full)
                .arg(pending));
    }
    const bool resumed = !streaming && active;
    streaming = active;
    frameInterval = qBound(0, interval, 1000);
    fullSeconds = qBound(-1, full, 60);
    if (!active) {
        nextFrame.stop();
    } else {
        if (resumed) {
            forceFull = true;
        }
        requestFrame();
    }
}

void Session::setDuplicateEndpoint(bool duplicate)
{
    if (duplicateEndpoint == duplicate) {
        return;
    }
    duplicateEndpoint = duplicate;
    forceFull = true;
    emit diagnostic(
        duplicate
            ? "Duplicate endpoint: using full frames for concurrent sessions"
            : "Duplicate endpoint cleared: restoring configured refresh "
              "policy");
    requestFrame();
}

bool Session::startVideoTrace(const QString& path)
{
    if (!videoTrace.start(path)) {
        return false;
    }
    emit diagnostic("Recording video packets (screen content): " + path);
    refresh();
    return true;
}

void Session::stopVideoTrace()
{
    videoTrace.stop();
    emit diagnostic("Video trace stopped");
}

QString Session::information() const
{
    const double seconds = uptime.isValid() ? uptime.elapsed() / 1000.0 : 0;
    const auto size = decoder ? decoder->image().size() : QSize();
    return QString(
               "Endpoint: %1:%2\nTransport: %3\nDisplay: %4 × %5\nInput: "
               "%6\nFrames: %7\nAverage FPS: %8\nVideo received: %9 "
               "MiB\nUptime: %10 seconds\n%11")
        .arg(mediaLaunch.host)
        .arg(mediaLaunch.port)
        .arg(mediaLaunch.tls ? "TLS 1.2" : "Plain TCP")
        .arg(size.width())
        .arg(size.height())
        .arg(controlling ? "Controlling" : "View only")
        .arg(frames)
        .arg(seconds > 0 ? frames / seconds : 0, 0, 'f', 1)
        .arg(videoBytes / 1048576.0, 0, 'f', 2)
        .arg(int(seconds))
        .arg(statusText);
}
}   // namespace aten

namespace aten {
void Session::setAutoReconnect(bool enabled, const QVariantMap& options)
{
    if (enabled && options.value("autoArm", true).toBool()) {
        reconnectOptions = options;
    } else {
        reconnectOptions.clear();
        if (phase == Phase::Reconnecting) {
            close();
        }
    }
}
}   // namespace aten

namespace aten {
Session::~Session()
{
    setKeepMedia(false);
    close();
}

void Session::setKeepMedia(bool value)
{
    keepMedia = value;
    if (!value) {
        const auto pending = std::move(deferredLogouts);
        deferredLogouts.clear();
        for (const auto& logout: pending) {
            logout();
        }
    }
}
}   // namespace aten
