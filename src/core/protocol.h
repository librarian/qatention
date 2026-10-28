#pragma once
#include <QByteArray>
#include <QString>
#include <QSize>
#include <stdexcept>

namespace aten {
struct Error : std::runtime_error { using std::runtime_error::runtime_error; };
quint16 be16(const QByteArray &bytes, qsizetype offset);
quint32 be32(const QByteArray &bytes, qsizetype offset);
void put16(QByteArray &bytes, qsizetype offset, quint16 value);
void put32(QByteArray &bytes, qsizetype offset, quint32 value);
QByteArray keyEvent(quint32 usage, bool down);
QByteArray pointerEvent(int x, int y, int buttons);
QByteArray updateRequest(QSize size, bool incremental);
QByteArray credentials(const QString &user, const QString &password);
struct Launch {
    QString host, user, password;
    quint16 port = 5900, mediaPort = 623;
    bool tls = true;
};
Launch parseJnlp(const QByteArray &xml);
}
