#pragma once

#include "decoder.h"

#include <QJsonObject>
#include <QSet>
#include <QWebSocket>

#include <optional>

namespace aten {

class MegaRac: public QObject
{
    Q_OBJECT
public:
    explicit MegaRac(const QString& origin, QObject* parent);
    void open(const QNetworkRequest& request, const QJsonObject& config);
    void close();
    void refresh();
    void setStreaming(bool active);
    void key(quint32 usage, bool down);
    void pointer(int x, int y, unsigned mask);
    void releaseInput();

    QSize size() const
    {
        return decoder.image().size();
    }

signals:
    void activity();
    void ready(bool controlling);
    void failed(const QString& message, bool transient);
    void frameReady(const QImage& frame);

private:
    QWebSocket socket;
    QJsonObject config;
    Decoder decoder;
    QByteArray input, compressed;
    QSize frameSize;
    qsizetype compressedSize = 0;
    quint8 quality = 0, advancedQuality = 0;
    bool chroma420 = false;
    bool validationSent = false, validated = false, controlling = false;
    bool streaming = true;
    static constexpr quint8 AbsoluteMouse = 2;
    quint8 mouseMode = AbsoluteMouse;
    quint32 sequence = 0;
    QSet<quint32> held;
    std::optional<QPoint> mousePosition;
    bool mousePressed = false;

    void receive(const QByteArray& bytes);
    void handle(quint16 type, quint16 status, QByteArrayView payload);
    void validate();
    void video(QByteArrayView payload);
    void setControl(bool allowed);
    void send(quint16 type, quint16 status = 0, QByteArrayView payload = {});
    void hid(
        quint8 device,
        quint8 protocol,
        quint8 interfaceNumber,
        QByteArrayView report);
    void keyboard();
};

}   // namespace aten
