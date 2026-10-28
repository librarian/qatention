#include <QtTest>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtEndian>
#include "core/protocol.h"
#include "core/decoder.h"
#include "core/session.h"

using namespace aten;
static QByteArray hermon(int w,int h,quint16 pixel) {
    QByteArray p(10+w*h*2,0);p[0]=1;put32(p,6,quint32(p.size()));
    for(int i=10;i<p.size();i+=2) qToLittleEndian(pixel,p.data()+i);
    return p;
}
static QByteArray frameMessage(const QByteArray &p,int w,int h) {
    QByteArray b(24,0);put16(b,2,1);put16(b,8,w);put16(b,10,h);put32(b,20,quint32(p.size()));return b+p;
}
class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void wireVectors() {
        QCOMPARE(keyEvent(0x41beef,true).toHex(),QByteArray("04000100000041beef000000000000000000"));
        QCOMPARE(pointerEvent(0x1234,0x5678,2).toHex(),QByteArray("050002123456780000000000000000000000"));
        QCOMPARE(updateRequest({640,480},true).toHex(),QByteArray("030100000000028001e0"));
        auto c=credentials("user","pass");QCOMPARE(c.size(),48);QCOMPARE(c.first(4),QByteArray("user"));QCOMPARE(c.mid(24,4),QByteArray("pass"));
        QVERIFY_EXCEPTION_THROWN(credentials(QString(25,'x'),"p"),Error);
        QVERIFY_EXCEPTION_THROWN(be32(QByteArray(3,0),0),Error);
    }
    void jnlp() {
        const auto xml=QByteArray("<jnlp><application-desc><argument>example.test</argument><argument>u&amp;x</argument><argument>p</argument><argument>host</argument><argument>63630</argument><argument>623</argument><argument>1</argument><argument>1</argument><argument>1</argument><argument>5900</argument></application-desc></jnlp>");
        auto l=parseJnlp(xml);QCOMPARE(l.port,5900);QVERIFY(l.tls);QCOMPARE(l.user,QString("u&x"));
        auto plain=xml;plain.replace("<argument>1</argument><argument>5900","<argument>0</argument><argument>5900");
        l=parseJnlp(plain);QCOMPARE(l.port,63630);QVERIFY(!l.tls);
        QVERIFY_EXCEPTION_THROWN(parseJnlp("<html>login failed</html>"),Error);
        QVERIFY_EXCEPTION_THROWN(parseJnlp("<!DOCTYPE jnlp><jnlp/>"),Error);
    }
    void hermonFramesAndClipping() {
        Decoder d;d.resize({17,17});QVERIFY(d.decode(hermon(17,17,0x7c00),0));
        QCOMPARE(d.image().pixel(0,0),qRgb(248,0,0));
        QByteArray tile(528,0);put32(tile,2,1);tile[14]=1;tile[15]=1;
        for(int p=16;p<528;p+=2) qToLittleEndian<quint16>(0x03e0,tile.data()+p);
        QVERIFY(d.decode(tile,0));QCOMPARE(d.image().pixel(16,16),qRgb(0,248,0));
        QCOMPARE(d.image().pixel(15,16),qRgb(248,0,0));
        tile.chop(1);QVERIFY_EXCEPTION_THROWN(d.decode(tile,0),Error);
        QVERIFY_EXCEPTION_THROWN(d.resize({65535,65535}),Error);
    }
    void aspeedVqAndSessionIsolation() {
        Decoder first,second;first.resize({8,8});second.resize({8,8});
        // 1-colour VQ using cache entry 1 (white), followed by frame-end. Bits are LE words.
        QByteArray white=QByteArray::fromHex("000001bc0000205300000000");
        QVERIFY(first.decode(white,0x57));
        QCOMPARE(first.image().pixel(0,0),qRgb(255,255,255));
        // Cache entry 0 is black. Ensure another context cannot overwrite the first image.
        QByteArray black=QByteArray::fromHex("000001bc0000205100000000");
        QVERIFY(second.decode(black,0x57));QCOMPARE(second.image().pixel(0,0),qRgb(0,0,0));
        QCOMPARE(first.image().pixel(0,0),qRgb(255,255,255));
        auto snapshot=first.image();QVERIFY(first.decode(black,0x57));
        QCOMPARE(snapshot.pixel(0,0),qRgb(255,255,255));
        auto end=QByteArray::fromHex("000001bc0000009000000000");
        QVERIFY(!first.decode(end,0x57));
        auto bad=white;bad[0]=12;QVERIFY_EXCEPTION_THROWN(first.decode(bad,0x57),Error);
    }
    void fragmentedSession() {
        QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));
        Session s;QSignalSpy frames(&s,&Session::frameReady);
        s.open({{"host","127.0.0.1"},{"user","u"},{"password","p"},{"autoArm",false},{"tls",false},{"port",server.serverPort()}});
        QTRY_VERIFY(server.hasPendingConnections());auto *peer=server.nextPendingConnection();
        peer->write("RFB 003.");QTest::qWait(5);QCOMPARE(peer->bytesAvailable(),0);
        peer->write("008\n");QTRY_VERIFY(peer->bytesAvailable()>=12);QCOMPARE(peer->read(12),QByteArray("RFB 003.008\n"));
        peer->write(QByteArray::fromHex("0110"));QTRY_VERIFY(peer->bytesAvailable()>=1);QCOMPARE(peer->read(1),QByteArray(1,16));
        peer->write(QByteArray(23,0));QTest::qWait(5);QCOMPARE(peer->bytesAvailable(),0);
        peer->write(QByteArray(1,0));QTRY_VERIFY(peer->bytesAvailable()>=48);QCOMPARE(peer->read(48),credentials("u","p"));
        peer->write(QByteArray(4,0));QTRY_VERIFY(peer->bytesAvailable()>=1);QCOMPARE(peer->read(1),QByteArray(1,0));
        QByteArray init(36,0);put16(init,0,2);put16(init,2,2);
        peer->write(init.first(35));QTest::qWait(5);QVERIFY(!s.connected());
        peer->write(init.last(1));QTRY_VERIFY(s.connected());
        QTRY_VERIFY(peer->bytesAvailable()>=22);peer->readAll();
        const auto update=frameMessage(hermon(2,2,0x1f),2,2);
        peer->write(update.first(update.size()-1));QTest::qWait(5);QCOMPARE(frames.size(),0);
        peer->write(update.last(1));QTRY_COMPARE(frames.size(),1);
        QCOMPARE(qvariant_cast<QImage>(frames[0][0]).pixel(0,0),qRgb(0,0,248));
        s.key(0xe0,true);s.releaseInput();
        QTRY_VERIFY(peer->bytesAvailable()>=36);const auto tx=peer->readAll();
        QVERIFY(tx.contains(keyEvent(0xe0,true)+keyEvent(0xe0,false)));
        QByteArray deny(265,0);deny[0]=0x39;put32(deny,1,1);put32(deny,5,4);
        peer->write(deny);QTRY_VERIFY(s.status().contains("view only"));
        peer->readAll();s.key(4,true);QTest::qWait(5);QVERIFY(!peer->readAll().contains(keyEvent(4,true)));
        // Reject a hostile payload length before allocating or waiting for its body.
        QByteArray huge(24,0);put16(huge,2,1);put32(huge,20,0xffffffff);peer->write(huge);
        QTRY_VERIFY(!s.busy());QVERIFY(s.status().contains("too large"));
    }
    void cancelConnection() {
        QTcpServer server;QVERIFY(server.listen(QHostAddress::LocalHost));
        Session s;s.open({{"host","127.0.0.1"},{"user","u"},{"password","p"},{"autoArm",false},{"tls",false},{"port",server.serverPort()}});
        QTRY_VERIFY(server.hasPendingConnections());s.close();QVERIFY(!s.busy());
        QTest::qWait(10);QCOMPARE(s.status(),QString("Disconnected"));
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "core_tests.moc"
