#pragma once
#include <QByteArray>
#include <QImage>
#include <memory>

namespace aten {
class Decoder {
public:
    Decoder();
    ~Decoder();
    Decoder(const Decoder&)=delete;
    Decoder& operator=(const Decoder&)=delete;
    void resize(QSize size);
    bool decode(const QByteArray &packet, quint32 encoding);
    const QImage &image() const { return frame; }
private:
    QImage frame;
    QByteArray palette=QByteArray(1024,0);
    bool decodePlanes(const QByteArray &packet);
    void *context=nullptr;
};
}
