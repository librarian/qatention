#include "core/certificatechain.h"

#include <QNetworkReply>
#include <QSslConfiguration>
#include <QSslKey>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>
using namespace aten;

static QSslCertificate cert(const char* name)
{
    QFile f(QString(":/tls/tests/certificates/") + name);
    f.open(QIODevice::ReadOnly);
    return QSslCertificate(f.readAll());
}

class CertificateChainTests: public QObject
{
    Q_OBJECT
    QSslConfiguration original;
private slots:

    void init()
    {
        original = QSslConfiguration::defaultConfiguration();
        auto config = original;
        config.setCaCertificates({cert("root.pem")});
        QSslConfiguration::setDefaultConfiguration(config);
    }

    void cleanup()
    {
        QSslConfiguration::setDefaultConfiguration(original);
    }

    void verification()
    {
        const auto leaf = cert("leaf.pem"), issuer = cert("issuer.pem");
        QVERIFY(!CertificateChain::verify({leaf}, "localhost"));
        QVERIFY(CertificateChain::verify({leaf, issuer}, "localhost"));
        QVERIFY(!CertificateChain::verify({leaf, issuer}, "wrong.example"));
        auto config = original;
        config.setCaCertificates({});
        QSslConfiguration::setDefaultConfiguration(config);
        QVERIFY(!CertificateChain::verify(
            {leaf, issuer, cert("root.pem")},
            "localhost"));
        QVERIFY(CertificateChain::missingIssuerOnly(
            {QSslError(QSslError::UnableToVerifyFirstCertificate)}));
        QVERIFY(!CertificateChain::missingIssuerOnly(
            {QSslError(QSslError::UnableToVerifyFirstCertificate),
             QSslError(QSslError::HostNameMismatch)}));
        QVERIFY(!CertificateChain::missingIssuerOnly(
            {QSslError(QSslError::CertificateExpired)}));
        QVERIFY(!CertificateChain::missingIssuerOnly(
            {QSslError(QSslError::SelfSignedCertificate)}));
        QCOMPARE(
            CertificateChain::issuerUrl(leaf),
            QUrl("http://127.0.0.1:18081/issuer.der"));
    }

    void cancellation()
    {
        bool called = false;
        auto* owner = new QObject;
        CertificateChain::fetch(
            owner,
            {cert("leaf.pem")},
            "localhost",
            [](const QString&) {},
            [&](bool) { called = true; });
        delete owner;
        QTest::qWait(50);
        QVERIFY(!called);
    }

    void fetchAndCache()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 18081));
        int downloads = 0;
        QByteArray response = "not a certificate", status = "200 OK", extra;
        connect(
            &server,
            &QTcpServer::newConnection,
            this,
            [&]
            {
                auto* s = server.nextPendingConnection();
                connect(
                    s,
                    &QTcpSocket::readyRead,
                    s,
                    [&, s]
                    {
                        s->readAll();
                        ++downloads;
                        s->write(
                            "HTTP/1.1 " + status + "\r\n" + extra +
                            "Content-Length: " +
                            QByteArray::number(response.size()) +
                            "\r\nConnection: close\r\n\r\n" + response);
                        s->disconnectFromHost();
                    });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            });
        bool finished = false, ok = false;
        auto run = [&]
        {
            finished = false;
            ok = false;
            CertificateChain::fetch(
                this,
                {cert("leaf.pem")},
                "localhost",
                [](const QString&) {},
                [&](bool result)
                {
                    ok = result;
                    finished = true;
                });
        };
        run();
        QTRY_VERIFY(finished);
        QVERIFY(!ok);
        QCOMPARE(downloads, 1);
        response = QByteArray(65537, 'x');
        run();
        QTRY_VERIFY(finished);
        QVERIFY(!ok);
        QCOMPARE(downloads, 2);
        status = "302 Found";
        extra = "Location: http://127.0.0.1:18081/other\r\n";
        response.clear();
        run();
        QTRY_VERIFY(finished);
        QVERIFY(!ok);
        QCOMPARE(downloads, 3);
        status = "200 OK";
        extra.clear();
        response = cert("issuer.pem").toDer();
        run();
        QTRY_VERIFY(finished);
        QVERIFY(ok);
        QCOMPARE(downloads, 4);
        const auto chain = CertificateChain::cached({cert("leaf.pem")});
        QVERIFY(CertificateChain::verify(chain, "localhost"));
        QVERIFY(!CertificateChain::verify(chain, "wrong.example"));
        run();
        QTRY_VERIFY(finished);
        QVERIFY(ok);
        QCOMPARE(downloads, 4);
    }
};
QTEST_GUILESS_MAIN(CertificateChainTests)
#include "certificatechain_tests.moc"
