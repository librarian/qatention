#include "protocol.h"
#include <QtEndian>
#include <QXmlStreamReader>

namespace aten {
static void require(const QByteArray &b, qsizetype p, qsizetype n) {
    if (p < 0 || p > b.size() || n > b.size() - p) throw Error("Truncated protocol record");
}
quint16 be16(const QByteArray &b, qsizetype p) { require(b,p,2); return qFromBigEndian<quint16>(b.constData()+p); }
quint32 be32(const QByteArray &b, qsizetype p) { require(b,p,4); return qFromBigEndian<quint32>(b.constData()+p); }
void put16(QByteArray &b, qsizetype p, quint16 v) { require(b,p,2); qToBigEndian(v,b.data()+p); }
void put32(QByteArray &b, qsizetype p, quint32 v) { require(b,p,4); qToBigEndian(v,b.data()+p); }
QByteArray keyEvent(quint32 usage, bool down) {
    QByteArray b(18,0); b[0]=4; b[2]=down ? 1 : 0; put32(b,5,usage); return b;
}
QByteArray pointerEvent(int x, int y, int buttons) {
    QByteArray b(18,0); b[0]=5; b[2]=char(buttons); put16(b,3,quint16(x)); put16(b,5,quint16(y)); return b;
}
QByteArray updateRequest(QSize size, bool incremental) {
    QByteArray b(10,0); b[0]=3; b[1]=incremental ? 1 : 0;
    put16(b,6,quint16(size.width())); put16(b,8,quint16(size.height())); return b;
}
QByteArray credentials(const QString &user, const QString &password) {
    auto field=[](const QString &s) {
        if (s.isEmpty() || s.size()>24) throw Error("KVM credentials must contain 1 to 24 ASCII characters");
        for (auto c:s) if(c.unicode()<32 || c.unicode()>126) throw Error("KVM credentials must be ASCII");
        auto b=s.toLatin1(); b.append(QByteArray(24-b.size(),0)); return b;
    };
    return field(user)+field(password);
}
Launch parseJnlp(const QByteArray &xml) {
    if (xml.size()>1024*1024) throw Error("JNLP is too large");
    QXmlStreamReader reader(xml); QStringList args;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isDTD()) throw Error("JNLP document types are not supported");
        if(reader.isStartElement() && reader.name()==u"argument") args << reader.readElementText().trimmed();
    }
    if(reader.hasError() || args.size()<10) throw Error("Invalid ATEN launch JNLP");
    if(args[8]!="0" && args[8]!="1") throw Error("Invalid JNLP TLS flag");
    Launch l; l.host=args[0]; l.user=args[1]; l.password=args[2]; l.tls=args[8]=="1";
    bool ok=false; int port=args[l.tls?9:4].toInt(&ok);
    if(!ok || port<1 || port>65535 || l.host.isEmpty()) throw Error("Invalid JNLP endpoint");
    l.port=quint16(port); credentials(l.user,l.password); return l;
}
}
