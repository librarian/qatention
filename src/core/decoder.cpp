#include "decoder.h"
#include "protocol.h"
#include <QtEndian>
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
    if(p[1]!=0) return decodePlanes(p);
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

namespace aten {
static QByteArray expandRle(const QByteArray &input,int size) {
    QByteArray out(size,0);qsizetype pos=0,dest=0;
    auto write=[&](char value,int count) {
        if(count>out.size()-dest) throw Error("RLE output exceeds frame");
        std::memset(out.data()+dest,quint8(value),size_t(count));dest+=count;
    };
    while(pos<input.size()) {
        pos=(pos+3)&~qsizetype(3);if(pos==input.size()) break;
        if(input.size()-pos<4) throw Error("Truncated RLE chunk header");
        const auto n=qFromLittleEndian<quint32>(input.constData()+pos);pos+=4;
        if(n>input.size()-pos) throw Error("Truncated RLE chunk");
        const qsizetype end=pos+n;
        while(pos<end) {
            const auto lead=quint8(input[pos++]);
            if(pos==end) {write(char(lead),1);continue;}
            if(lead==0x55) {
                const auto count=quint8(input[pos++]);
                if(count<=1) write(count==0?char(0x55):char(0xaa),1);
                else {if(pos==end) throw Error("Truncated RLE run");write(input[pos++],count+1);}
            } else if(lead==0xaa) write(input[pos++],3);
            else write(char(lead),1);
        }
    }
    // Zero the unwritten tail rather than reusing pixels from an earlier frame.
    return out;
}
static QRgb planePixel(const QByteArray &planes,int base,int pixels,int n,bool type4) {
    const int stride=(pixels+7)/8;
    auto assemble=[&](int group) {int v=0;for(int k=0;k<8;++k) v|=((quint8(planes[base+(group+7-k)*stride+(n>>3)])>>(n&7))&1)<<k;return v;};
    if(type4) return qRgb(assemble(24),assemble(0),assemble(8));
    const int low=assemble(8),high=assemble(0);
    // Type 2 uses the vendor's packed conversion, distinct from raw Hermon RGB555.
    return qRgb(high&0xf8,((low>>3&0x1c)|(high<<5))&255,(low<<3)&255);
}
bool Decoder::decodePlanes(const QByteArray &p) {
    const int type=quint8(p[1]),subtype=quint8(p[2]);
    const bool hasPalette=(type==1 && subtype==0) || type==5;
    const int offset=hasPalette?1034:10;const quint32 total=be32(p,6);
    if(total<quint32(offset) || total>quint32(p.size())) throw Error("Invalid video payload length");
    if(hasPalette) palette=p.mid(10,1024);
    const auto payload=p.mid(offset,total-offset);
    const int pixels=frame.width()*frame.height();
    if(p[0]==1) {
        if(subtype==2 || (subtype==0 && type==1)) {
            const bool nibble=subtype==2;
            if(payload.size()<(pixels+(nibble?1:0))/(nibble?2:1)) throw Error("Truncated palette frame");
            for(int n=0;n<pixels;++n) {
                const auto v=quint8(payload[n/(nibble?2:1)]);
                const int index=nibble?((v>>((n%2)*4))&15):v;
                reinterpret_cast<QRgb*>(frame.scanLine(n/frame.width()))[n%frame.width()]=qRgb(quint8(palette[index*4+2]),quint8(palette[index*4+1]),quint8(palette[index*4]));
            }
            return true;
        }
        if(subtype!=0 || (type!=2 && type!=4)) throw Error("Unsupported ATEN full-frame encoding");
        const auto planes=expandRle(payload,(type==4?32:16)*((pixels+7)/8));
        for(int n=0;n<pixels;++n) reinterpret_cast<QRgb*>(frame.scanLine(n/frame.width()))[n%frame.width()]=planePixel(planes,0,pixels,n,type==4);
        return true;
    }
    if(type!=2 && type!=4) throw Error("Unsupported ATEN incremental encoding");
    if(payload.size()<4) throw Error("Truncated tile table");
    const int count=qFromLittleEndian<quint16>(payload.constData()),start=count*4+4;
    if(start>payload.size()) throw Error("Truncated tile table");
    int totalPlanes=0;const int planeSlots=type==4?32:16;
    for(int t=0;t<count;++t) {
        const int at=2+t*4,w=quint8(payload[at+2]),h=quint8(payload[at+3]);
        const int bytes=planeSlots*((w*h+7)/8);
        if(totalPlanes>64*1024*1024-bytes) throw Error("Tile expansion limit exceeded");
        totalPlanes+=bytes;
    }
    const auto planes=expandRle(payload.mid(start),totalPlanes);int pos=0;
    for(int t=0;t<count;++t) {
        const int at=2+t*4,y=quint8(payload[at]),x=quint8(payload[at+1]),w=quint8(payload[at+2]),h=quint8(payload[at+3]);
        for(int row=0;row<h && y+row<frame.height();++row) {
            auto *line=reinterpret_cast<QRgb*>(frame.scanLine(y+row));
            for(int col=0;col<w && x+col<frame.width();++col) line[x+col]=planePixel(planes,pos,w*h,row*w+col,type==4);
        }
        pos+=planeSlots*((w*h+7)/8);
    }
    return count>0;
}
}
