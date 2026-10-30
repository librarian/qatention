#include "profiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#ifdef Q_OS_WIN
#include <windows.h>

#include <wincrypt.h>
#endif
namespace aten {
QVariantMap Profiles::sanitize(const QVariantMap& options)
{
    QVariantMap result;
    for (const auto& key:
         {"host",
          "user",
          "password",
          "autoArm",
          "port",
          "tls",
          "webPort",
          "allowUntrusted",
          "switchJava",
          "frameInterval",
          "autoReconnect",
          "consoleTrust"})
    {
        if (options.contains(key)) {
            result[key] = options[key];
        }
    }
    return result;
}

Profiles::Profiles(QString filename, QObject* parent)
    : QObject(parent)
    , path(std::move(filename))
{
    QFile file(path);
    if (!file.exists()) {
        return;
    }
    QJsonParseError parseError;
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024LL * 1024) {
        readable = false;
        problem = "Cannot read profiles file: " + path;
        return;
    }
    auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        document.object().value("version").toInt() != 1 ||
        !document.object().value("profiles").isArray())
    {
        readable = false;
        problem = "Invalid profiles file; move it aside before saving: " + path;
        return;
    }
    for (auto entry: document.object().value("profiles").toArray()) {
        auto options = entry.toObject().toVariantMap();
        auto clean = sanitize(options);
#ifdef Q_OS_WIN
        if (options.contains("protectedPassword")) {
            auto encrypted = QByteArray::fromBase64(
                options.value("protectedPassword").toString().toLatin1());
            DATA_BLOB input{
                DWORD(encrypted.size()),
                reinterpret_cast<BYTE*>(encrypted.data())},
                output{};
            if (CryptUnprotectData(
                    &input,
                    nullptr,
                    nullptr,
                    nullptr,
                    nullptr,
                    CRYPTPROTECT_UI_FORBIDDEN,
                    &output))
            {
                clean["password"] = QString::fromUtf8(
                    reinterpret_cast<char*>(output.pbData),
                    output.cbData);
                LocalFree(output.pbData);
            } else {
                clean["password"] = QString();
                problem =
                    "A saved password could not be decrypted for this Windows "
                    "account.";
            }
        }
#else
        if (options.contains("protectedPassword")) {
            clean["password"] = QString();
            problem =
                "Windows-protected passwords must be re-entered on this "
                "platform.";
        }
#endif
        clean["name"] = options.value("name");
        items.append(clean);
    }
}

bool Profiles::write(const QVariantList& next)
{
    if (!readable) {
        emit changed();
        return false;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
#ifdef Q_OS_WIN
    auto stored = next;
    for (auto& item: stored) {
        auto entry = item.toMap();
        auto secret = entry.take("password").toString().toUtf8();
        DATA_BLOB input{
            DWORD(secret.size()),
            reinterpret_cast<BYTE*>(secret.data())},
            output{};
        if (!CryptProtectData(
                &input,
                L"QATENtion profile",
                nullptr,
                nullptr,
                nullptr,
                CRYPTPROTECT_UI_FORBIDDEN,
                &output))
        {
            problem = "Windows could not protect the profile password.";
            emit changed();
            return false;
        }
        entry["protectedPassword"] = QString::fromLatin1(
            QByteArray(reinterpret_cast<char*>(output.pbData), output.cbData)
                .toBase64());
        LocalFree(output.pbData);
        item = entry;
    }
#else
    const auto& stored = next;
#endif
    QSaveFile file(path);
    const auto bytes =
        QJsonDocument(
            QJsonObject{
                {"version", 1},
                {"profiles", QJsonArray::fromVariantList(stored)}})
            .toJson();
    if (!file.open(QIODevice::WriteOnly) ||
        !file.setPermissions(
            QFileDevice::ReadOwner | QFileDevice::WriteOwner) ||
        file.write(bytes) != bytes.size() || !file.commit())
    {
        problem = "Cannot save profiles: " + file.errorString();
        emit changed();
        return false;
    }
    items = next;
    problem.clear();
    emit changed();
    return true;
}

bool Profiles::save(const QString& name, const QVariantMap& options)
{
    const auto label = name.trimmed();
    if (label.isEmpty() || label.size() > 128 ||
        options.value("host").toString().trimmed().isEmpty())
    {
        problem = "Enter a profile name and BMC host.";
        emit changed();
        return false;
    }
    auto next = items;
    auto entry = sanitize(options);
    entry["name"] = label;
    for (auto& item: next) {
        if (item.toMap().value("name") == label) {
            item = entry;
            return write(next);
        }
    }
    if (next.size() >= 100) {
        problem = "Maximum of 100 profiles reached.";
        emit changed();
        return false;
    }
    next.append(entry);
    return write(next);
}

bool Profiles::remove(const QString& name)
{
    auto next = items;
    for (qsizetype i = 0; i < next.size(); ++i) {
        if (next[i].toMap().value("name") == name) {
            next.removeAt(i);
            break;
        }
    }
    return write(next);
}
}   // namespace aten
