#include "debuglog.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

namespace aten {
DebugLog::DebugLog(const QString& path, QObject* parent)
    : QObject(parent)
    , file(path)
{}

void DebugLog::setEnabled(bool enabled)
{
    if (active == enabled) {
        return;
    }
    active = enabled;
    if (active) {
        write("Debug logging enabled");
    } else {
        file.close();
    }
    emit enabledChanged();
}

void DebugLog::write(const QString& message)
{
    if (!active) {
        return;
    }
    auto line = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) +
                "  " + message + "\n";
    lines += line;
    if (lines.size() > 65536) {
        lines.remove(0, lines.indexOf('\n', lines.size() - 65536) + 1);
    }
    if (!file.isOpen()) {
        QDir().mkpath(QFileInfo(path()).absolutePath());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Append)) {
            lines += "Cannot open log file: " + file.errorString() + "\n";
            emit changed();
            return;
        }
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    if (file.size() >= MaxFileBytes) {
        file.close();
        const auto previous = path() + ".old";
        if ((!QFile::exists(previous) || QFile::remove(previous)) &&
            QFile::rename(path(), previous))
        {
            file.open(QIODevice::WriteOnly);
        } else {
            file.open(QIODevice::WriteOnly | QIODevice::Truncate);
        }
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    const auto bytes = line.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.flush()) {
        lines += "Cannot write log file: " + file.errorString() + "\n";
        file.close();
    }
    emit changed();
}

void DebugLog::clear()
{
    file.close();
    lines.clear();
    if (QFile::exists(path()) && !QFile::remove(path())) {
        lines = "Cannot clear log file.\n";
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
