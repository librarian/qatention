#include "videotrace.h"
#include "decoder.h"
#include "protocol.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <memory>

namespace aten {
static constexpr qint64 Limit=64*1024*1024;
QByteArray imageHash(const QImage &image) {
    return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256).toHex();
}
bool VideoTrace::start(const QString &path) {
    stop();file.setFileName(path);
    if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly)) return false;
    file.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner);
    return record({{"type","header"},{"version",1}});
}
bool VideoTrace::record(QJsonObject event) {
    if(!active()) return false;
    event["utc"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const auto bytes=QJsonDocument(event).toJson(QJsonDocument::Compact)+'\n';
    if(file.size()+bytes.size()>Limit || file.write(bytes)!=bytes.size() || !file.flush()) {stop();return false;}
    return true;
}
QString replayVideoTrace(const QString &path,const QString &output) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly) || file.size()>Limit) throw Error("Cannot read bounded video trace");
    std::unique_ptr<Decoder> decoder;
    QImage lastImage;
    int packets=0,compared=0,mismatches=0;bool header=false;
    while(!file.atEnd()) {
        const auto line=file.readLine(Limit+1);QJsonParseError error;
        const auto doc=QJsonDocument::fromJson(line,&error);
        if(error.error!=QJsonParseError::NoError || !doc.isObject()) throw Error("Invalid trace JSON");
        const auto e=doc.object();const auto type=e["type"].toString();
        if(!header) {if(type!="header" || e["version"].toInt()!=1) throw Error("Unsupported trace header");header=true;continue;}
        if(type=="reset") {decoder.reset();continue;}
        if(type!="video") continue;
        const QSize size(e["width"].toInt(),e["height"].toInt());
        if(!decoder) decoder=std::make_unique<Decoder>();
        if(decoder->image().size()!=size) decoder->resize(size);
        const auto packet=QByteArray::fromBase64Encoding(e["payload"].toString().toLatin1(),QByteArray::AbortOnBase64DecodingErrors);
        if(!packet) throw Error("Invalid trace packet base64");
        decoder->decode(packet.decoded,quint32(e["encoding"].toDouble()));++packets;
        lastImage=decoder->image();
        if(e.contains("imageSha256")) {++compared;if(imageHash(decoder->image())!=e["imageSha256"].toString().toLatin1()) ++mismatches;}
    }
    if(!header || !packets) throw Error("Trace has no video packets");
    if(!output.isEmpty() && (QFile::exists(output) || !lastImage.save(output,"PNG"))) throw Error("Cannot save replay PNG (output must not exist)");
    return QString("packets=%1 compared=%2 mismatches=%3 finalSha256=%4").arg(packets).arg(compared).arg(mismatches).arg(QString::fromLatin1(imageHash(lastImage)));
}
}
