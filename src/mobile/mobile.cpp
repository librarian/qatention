#include "mobile.h"

#include "core/input.h"

#include <QClipboard>
#include <QCryptographicHash>
#include <QGuiApplication>
#include <QPainter>
#include <QStandardPaths>
#include <QUrl>
#ifdef Q_OS_ANDROID
#include <QJniObject>
#endif
namespace {
QString identity(const QString& host, const QString& user)
{
    auto name = host.trimmed();
    if (name.contains("://")) {
        name = QUrl(name).host();
    }
    return QString::fromLatin1(
        QCryptographicHash::hash(
            (name.toLower() + "\n" + user).toUtf8(),
            QCryptographicHash::Sha256)
            .toHex());
}
}   // namespace

Mobile::Mobile()
    : log(QStandardPaths::writableLocation(
              QStandardPaths::AppLocalDataLocation) +
          "/logs/qatention.log")
{
#ifdef Q_OS_ANDROID
    insetTimer.setInterval(100);
    connect(
        &insetTimer,
        &QTimer::timeout,
        this,
        [this]
        {
            const auto ratio = qGuiApp->devicePixelRatio();
            constexpr auto cls = "org/libc6/qatention/MainActivity";
            const QMarginsF next(
                QJniObject::getStaticField<jint>(cls, "insetLeft") / ratio,
                QJniObject::getStaticField<jint>(cls, "insetTop") / ratio,
                QJniObject::getStaticField<jint>(cls, "insetRight") / ratio,
                QJniObject::getStaticField<jint>(cls, "insetBottom") / ratio);
            if (next != insets) {
                insets = next;
                emit safeInsetsChanged();
            }
        });
    insetTimer.start();
#endif
    connect(
        &session,
        &aten::Session::stateChanged,
        this,
        [this]
        {
            if (!session.connected()) {
                releaseModifiers();
            }
            emit powerStatusChanged();
        });
    connect(&media, &aten::Media::diagnostic, &log, &aten::DebugLog::write);
    connect(
        &media,
        &aten::Media::changed,
        this,
        [this]
        {
            session.setKeepMedia(media.busy());
            emit mediaChanged();
        });
    connect(&session, &aten::Session::diagnostic, &log, &aten::DebugLog::write);
    connect(
        &session,
        &aten::Session::consoleCertificateUntrusted,
        this,
        [this](const QString& host, quint16 port, const QSslCertificate& cert)
        {
            const auto hash = QString::fromLatin1(
                cert.digest(QCryptographicHash::Sha256).toHex());
            pendingTrust = {{"host", host}, {"port", port}, {"sha256", hash}};
            emit trustRequested(host + ":" + QString::number(port), hash);
        });
    applyViewSettings();
}

QVariantMap Mobile::viewSettings() const
{
    QVariantMap result{
        {"fps", 20},
        {"fullSeconds", 5},
        {"quality", 11},
        {"mode", 444},
        {"smooth", false},
        {"keyboard", true},
        {"actualSize", false}};
    for (auto it = result.begin(); it != result.end(); ++it) {
        it.value() = settings.value("mobileView/" + it.key(), it.value());
    }
    return result;
}

void Mobile::setViewOption(const QString& key, const QVariant& value)
{
    const auto current = viewSettings();
    if (!current.contains(key)) {
        return;
    }
    if (key == "fps" && !QList<int>{0, 10, 20, 30, 60}.contains(value.toInt()))
    {
        return;
    }
    if (key == "fullSeconds" &&
        !QList<int>{-1, 0, 1, 2, 3, 5, 10}.contains(value.toInt()))
    {
        return;
    }
    if (key == "quality" && (value.toInt() < 0 || value.toInt() > 11)) {
        return;
    }
    if (key == "mode" && value.toInt() != 444 && value.toInt() != 422) {
        return;
    }
    if (current.value(key) == value) {
        return;
    }
    settings.setValue("mobileView/" + key, value);
    settings.sync();
    if (key == "keyboard" && !value.toBool()) {
        releaseModifiers();
    }
    applyViewSettings();
    emit viewSettingsChanged();
}

void Mobile::applyViewSettings()
{
    const auto view = viewSettings();
    const int fps = view.value("fps").toInt();
    const int quality = view.value("quality").toInt(),
              mode = view.value("mode").toInt();
    if (quality != appliedQuality || mode != appliedMode) {
        appliedQuality = quality;
        appliedMode = mode;
        session.setDisplay(quality, mode);
    }
    session.setStreaming(
        foreground,
        fps > 0 ? 1000 / fps : 0,
        view.value("fullSeconds").toInt());
}

void Mobile::mountIso(const QUrl& url)
{
    if (!session.connected() || media.busy()) {
        return;
    }
    if (!url.isLocalFile() && url.scheme() != "content") {
        emit notice("Select an ISO from the device's file picker.");
        return;
    }
    // Preserve Android content URIs; Qt's file engine opens the granted
    // document.
    const auto path = url.isLocalFile() ? url.toLocalFile()
                                        : url.toString(QUrl::FullyEncoded);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 ||
        file.isSequential() || !file.seek(file.size() - 1) || !file.seek(0))
    {
        emit notice(
            "The ISO must be a readable, seekable file. Download it to device "
            "storage first.");
        return;
    }
    file.close();
    media.mount(
        session.mediaEndpoint(),
        session.untrustedAllowed(),
        path,
        session.mediaTrustedCertificate());
}

void Mobile::unmountIso()
{
    media.unmount();
}

QString Mobile::powerStatus() const
{
    if (!session.connected() || session.hostPower() < 0) {
        return "Power state unknown";
    }
    return session.hostPower() ? "Host is powered on" : "Host is powered off";
}

void Mobile::power(int command)
{
    if (session.connected() && command >= 0 && command <= 3) {
        session.power(command);
    }
}

void Mobile::hotplug()
{
    if (session.connected()) {
        session.hotplug();
    }
}

QString Mobile::trustKey() const
{
    return "consoleTrust/" +
           QString::fromLatin1(
               QCryptographicHash::hash(
                   options.value("host").toString().toLower().toUtf8(),
                   QCryptographicHash::Sha256)
                   .toHex());
}

void Mobile::connectTo(
    const QString& host,
    const QString& user,
    const QString& password,
    bool unverified,
    bool remember)
{
    disconnectSession();
    auto normalized = host.trimmed();
    if (normalized.contains("://")) {
        normalized = QUrl(normalized).host();
    }
    if (normalized.isEmpty() || user.isEmpty() || password.isEmpty()) {
        emit notice("Enter a host, username and password.");
        return;
    }
    options = {
        {"host", normalized},
        {"user", user},
        {"password", password},
        {"autoArm", true},
        {"autoReconnect", false},
        {"allowUntrusted", unverified}};
    options["consoleTrust"] = settings.value(trustKey()).toMap();
    settings.setValue("host", normalized);
    settings.setValue("user", user);
#ifdef Q_OS_ANDROID
    const auto id = QJniObject::fromString(identity(normalized, user));
    const auto secret = QJniObject::fromString(remember ? password : QString());
    if (!QJniObject::callStaticMethod<jboolean>(
            "org/libc6/qatention/MainActivity",
            "savePassword",
            "(Ljava/lang/String;Ljava/lang/String;)Z",
            id.object<jstring>(),
            secret.object<jstring>()))
    {
        emit notice("Could not save the password on this device.");
    }
#else
    Q_UNUSED(remember);
#endif
    auto entries = profiles();
    const auto idKey = identity(normalized, user);
    const QVariantMap profile{
        {"id", idKey},
        {"host", normalized},
        {"user", user},
        {"label", normalized + " — " + user},
        {"remember", remember},
        {"unverified", unverified}};
    bool replaced = false;
    for (auto& entry: entries) {
        if (entry.toMap().value("id") == idKey) {
            entry = profile;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        entries.append(profile);
    }
    settings.setValue("mobileProfiles", entries);
    settings.sync();
    emit profilesChanged();
    session.open(options);
}

void Mobile::disconnectSession()
{
    media.unmount();
    releaseModifiers();
    pendingTrust.clear();
    session.close();
    options.clear();
}

void Mobile::trustConsole()
{
    if (pendingTrust.isEmpty() || options.isEmpty() || session.busy()) {
        return;
    }
    settings.setValue(trustKey(), pendingTrust);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        emit notice("Cannot save console certificate trust.");
        return;
    }
    options["consoleTrust"] = pendingTrust;
    pendingTrust.clear();
    session.open(options);
}

void Mobile::removeProfile(const QString& host, const QString& user)
{
    const auto idKey = identity(host, user);
#ifdef Q_OS_ANDROID
    const auto id = QJniObject::fromString(idKey),
               empty = QJniObject::fromString(QString());
    if (!QJniObject::callStaticMethod<jboolean>(
            "org/libc6/qatention/MainActivity",
            "savePassword",
            "(Ljava/lang/String;Ljava/lang/String;)Z",
            id.object<jstring>(),
            empty.object<jstring>()))
    {
        emit notice("Could not remove the saved password.");
        return;
    }
#endif
    auto entries = profiles();
    for (qsizetype i = entries.size(); i > 0; --i) {
        if (entries[i - 1].toMap().value("id") == idKey) {
            entries.removeAt(i - 1);
        }
    }
    settings.setValue("mobileProfiles", entries);
    settings.sync();
    emit profilesChanged();
}

QString Mobile::savedPassword(const QString& host, const QString& user)
{
#ifdef Q_OS_ANDROID
    const auto id = QJniObject::fromString(identity(host, user));
    return QJniObject::callStaticObjectMethod(
               "org/libc6/qatention/MainActivity",
               "loadPassword",
               "(Ljava/lang/String;)Ljava/lang/String;",
               id.object<jstring>())
        .toString();
#else
    Q_UNUSED(host);
    Q_UNUSED(user);
    return {};
#endif
}

void Mobile::releaseModifiers()
{
    session.releaseInput();
    if (modifierMask) {
        modifierMask = 0;
        emit modifiersChanged();
    }
}

void Mobile::toggleModifier(int usage)
{
    if (!session.connected() || usage < 0xe0 || usage > 0xe7) {
        return;
    }
    const auto bit = 1U << unsigned(usage - 0xe0);
    modifierMask ^= bit;
    emit modifiersChanged();
}

void Mobile::tapKey(int usage)
{
    if (!session.connected()) {
        return;
    }
    for (int i = 0; i < 8; ++i) {
        if (modifierMask & (1U << unsigned(i))) {
            session.key(0xe0 + i, true);
        }
    }
    session.key(usage, true);
    session.key(usage, false);
    for (int i = 7; i >= 0; --i) {
        if (modifierMask & (1U << unsigned(i))) {
            session.key(0xe0 + i, false);
        }
    }
}

void Mobile::typeText(const QString& text)
{
    if (!session.connected()) {
        return;
    }
    if (text.size() > 256) {
        emit notice("Send at most 256 characters at a time.");
        return;
    }
    for (auto c: text) {
        bool shift = false;
        if (!aten::characterUsage(c, shift)) {
            emit notice("This preview supports US-layout ASCII text only.");
            return;
        }
    }
    // Send in order immediately; do not replace a pending character queue when
    // another touch arrives, or let Enter/Backspace overtake queued text.
    for (auto c: text) {
        bool shift = false;
        const auto usage = aten::characterUsage(c, shift);
        if (shift) {
            session.key(0xe1, true);
        }
        session.key(usage, true);
        session.key(usage, false);
        if (shift) {
            session.key(0xe1, false);
        }
    }
}

void Mobile::copyLogs()
{
    QGuiApplication::clipboard()->setText(log.text());
}

void Mobile::background(bool active)
{
    releaseModifiers();
    foreground = active;
    applyViewSettings();
}

QRectF MobileVideo::imageRect() const
{
    QSizeF size = frame.size();
    size.scale(boundingRect().size(), Qt::KeepAspectRatio);
    size *= magnification;
    return {
        (width() - size.width()) / 2 + pan.x(),
        (height() - size.height()) / 2 + pan.y(),
        size.width(),
        size.height()};
}

void MobileVideo::setSession(aten::Session* s)
{
    if (connection == s) {
        return;
    }
    release();
    if (connection) {
        disconnect(connection, nullptr, this, nullptr);
    }
    connection = s;
    frame = {};
    if (s) {
        connect(
            s,
            &aten::Session::frameReady,
            this,
            [this](const QImage& image)
            {
                const bool resized = frame.size() != image.size();
                if (resized) {
                    fit();
                }
                frame = image;
                if (resized) {
                    emit frameSizeChanged();
                }
                constrainPan();
                update();
            });
        connect(
            s,
            &aten::Session::clearFrame,
            this,
            [this]
            {
                frame = {};
                update();
            });
    }
    update();
    emit sessionChanged();
}

void MobileVideo::fit()
{
    magnification = 1;
    pan = {};
    update();
    emit zoomChanged();
}

void MobileVideo::constrainPan()
{
    auto size = QSizeF(frame.size());
    size.scale(boundingRect().size(), Qt::KeepAspectRatio);
    size *= magnification;
    const auto x = qMax(0.0, (size.width() - width()) / 2),
               y = qMax(0.0, (size.height() - height()) / 2);
    pan = {qBound(-x, pan.x(), x), qBound(-y, pan.y(), y)};
}

void MobileVideo::adjustView(qreal scale, qreal dx, qreal dy)
{
    if (!qIsFinite(scale) || scale <= 0) {
        return;
    }
    const auto next = qBound(1.0, magnification * scale, 8.0);
    pan = pan * (next / magnification) + QPointF(dx, dy);
    magnification = next;
    constrainPan();
    update();
    emit zoomChanged();
}

void MobileVideo::geometryChange(const QRectF& n, const QRectF& o)
{
    QQuickPaintedItem::geometryChange(n, o);
    constrainPan();
    update();
}

void MobileVideo::paint(QPainter* p)
{
    p->fillRect(boundingRect(), Qt::black);
    p->setRenderHint(QPainter::SmoothPixmapTransform, smooth());
    if (!frame.isNull()) {
        p->drawImage(imageRect(), frame);
    }
}

void MobileVideo::pointer(qreal x, qreal y, bool down)
{
    if (!connection || frame.isNull() || !connection->connected()) {
        return;
    }
    const auto r = imageRect();
    if (r.isEmpty()) {
        return;
    }
    const int px = qBound(
        0,
        int((x - r.x()) * frame.width() / r.width()),
        frame.width() - 1);
    const int py = qBound(
        0,
        int((y - r.y()) * frame.height() / r.height()),
        frame.height() - 1);
    connection->pointer(px, py, down ? 1 : 0);
}

void MobileVideo::release()
{
    if (connection) {
        connection->releaseInput();
    }
}
