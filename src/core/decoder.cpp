#include "decoder.h"
#include "protocol.h"
#include <QMutex>
#include <QMutexLocker>
#include <cstring>
#include <mutex>
#include <vector>

extern "C" {
void aspeed_init(void);
void *aspeed_ctx_new(void);
void aspeed_ctx_free(void*);
void aspeed_decode_ctx_map(void*,uint32_t*,int,unsigned char*,int,int,unsigned,unsigned,unsigned,unsigned char*,int);
}
namespace aten {
static QMutex nativeMutex;
static std::once_flag initialized;
Decoder::Decoder() { resize({640,480}); }
Decoder::~Decoder() { QMutexLocker lock(&nativeMutex); aspeed_ctx_free(context); }
void Decoder::resize(QSize size) {
    if(size.width()<1 || size.height()<1 || size.width()>4096 || size.height()>4096)
        throw Error("Unsupported framebuffer dimensions");
    QMutexLocker lock(&nativeMutex);
    aspeed_ctx_free(context); context=nullptr;
    frame=QImage(size,QImage::Format_ARGB32);
    if(frame.isNull()) throw Error("Cannot allocate framebuffer");
    frame.fill(Qt::black);
}
static QRgb rgb555(const char *p) {
    const auto v=quint16(quint8(p[0]) | (quint16(quint8(p[1]))<<8));
    return qRgb(((v>>10)&31)<<3,((v>>5)&31)<<3,(v&31)<<3);
}
bool Decoder::decode(const QByteArray &p, quint32 encoding) {
    if(encoding==0x57) {
        if(p.size()<12 || quint8(p[0])>11 || quint8(p[1])>11) throw Error("Invalid ASPEED header");
        auto marker=be16(p,2);
        if(marker!=0x1a6 && marker!=0x1bc) throw Error("Unknown ASPEED chroma mode");
        if(frame.width()>1920 || frame.height()>1200) throw Error("ASPEED supports at most 1920x1200");
        // Native input is uint32_t*: provide aligned storage, including zero padding for partial words.
        std::vector<uint32_t> words(size_t((p.size()-4+3)/4),0);
        std::memcpy(words.data(),p.constData()+4,size_t(p.size()-4));
        const int block=marker==0x1a6 ? 16 : 8;
        QByteArray map(((frame.width()+block-1)/block)*((frame.height()+block-1)/block),0);
        QMutexLocker lock(&nativeMutex);
        std::call_once(initialized,aspeed_init);
        if(!context) context=aspeed_ctx_new();
        if(!context) throw Error("Cannot allocate ASPEED context");
        aspeed_decode_ctx_map(context,words.data(),int(p.size()-4),frame.bits(),frame.width(),frame.height(),
            marker==0x1a6,quint8(p[0]),quint8(p[1]),reinterpret_cast<unsigned char*>(map.data()),int(map.size()));
        return map.contains(char(1));
    }
    if(p.size()<10) throw Error("Truncated ATEN header");
    if(p[1]!=0) throw Error("This preview supports Hermon type 0 and ASPEED video; other ATEN codecs are pending");
    if(p[0]==1) {
        if(p.size()-10 < qsizetype(frame.width())*frame.height()*2) throw Error("Truncated Hermon keyframe");
        for(int y=0;y<frame.height();++y) {
            auto *row=reinterpret_cast<QRgb*>(frame.scanLine(y));
            for(int x=0;x<frame.width();++x) row[x]=rgb555(p.constData()+10+2*(y*frame.width()+x));
        }
        return true;
    }
    const auto count=be32(p,2);
    if(count>quint32((p.size()-10)/518)) throw Error("Truncated Hermon tile update");
    for(quint32 t=0;t<count;++t) {
        auto pos=10+qsizetype(t)*518;
        int top=quint8(p[pos+4])*16, left=quint8(p[pos+5])*16;
        for(int y=0;y<16 && y+top<frame.height();++y) {
            auto *row=reinterpret_cast<QRgb*>(frame.scanLine(top+y));
            for(int x=0;x<16 && x+left<frame.width();++x)
                row[left+x]=rgb555(p.constData()+pos+6+2*(y*16+x));
        }
    }
    return count!=0;
}
}
