#pragma once
#include "core/session.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QtTest>

// Local plaintext ATEN peer for input regressions; no BMC or credentials
// needed.
struct MockConsole
{
    QTcpServer listener;
    aten::Session session;
    QTcpSocket* peer = nullptr;

    void connectSession()
    {
        using namespace aten;
        QVERIFY(listener.listen(QHostAddress::LocalHost));
        session.open(
            {{"host", "127.0.0.1"},
             {"user", "fixture"},
             {"password", "fixture"},
             {"autoArm", false},
             {"tls", false},
             {"port", listener.serverPort()}});
        QTRY_VERIFY(listener.hasPendingConnections());
        peer = listener.nextPendingConnection();
        peer->write("RFB 003.008\n");
        QTRY_VERIFY(peer->bytesAvailable() >= 12);
        peer->readAll();
        // One offered security type (16), followed by its 24-byte challenge.
        peer->write(QByteArray::fromHex("0110") + QByteArray(24, 0));
        QTRY_VERIFY(peer->bytesAvailable() >= 49);
        peer->readAll();
        QByteArray initialization(36, 0);
        put16(initialization, 0, 640);
        put16(initialization, 2, 480);
        peer->write(
            QByteArray(4, 0) + initialization);   // Auth success + ServerInit.
        QTRY_VERIFY(session.connected());
        QTRY_VERIFY(peer->bytesAvailable() >= 22);
        peer->readAll();
        session.setStreaming(false, 100, 5);
    }
};
