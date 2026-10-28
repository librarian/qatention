#include "session.h"
#include <QNetworkReply>
#include <QNetworkCookieJar>
#include <QNetworkCookie>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <QFile>
#include <QSslKey>
#include <QSslConfiguration>
#include <QRegularExpression>
#include <QScopedValueRollback>

static void initCertificateResource() { Q_INIT_RESOURCE(vendor_certificate); }
namespace aten {
static constexpr qsizetype MaxPacket=32*1024*1024;
Session::Session(QObject *parent):QObject(parent) {
    initCertificateResource();
    deadline.setSingleShot(true); nextFrame.setSingleShot(true);
    connect(&deadline,&QTimer::timeout,this,[this]{fail("Connection or handshake timed out");});
    connect(&nextFrame,&QTimer::timeout,this,&Session::requestFrame);
    heartbeat.setInterval(3000);
    connect(&heartbeat,&QTimer::timeout,this,[this]{
        if(!connected()) return;
        if(received.elapsed()>30000) { fail("BMC stopped responding"); return; }
        send(QByteArray::fromHex("150000000100000000"));
        if(!pending) requestFrame();
    });
}
void Session::setStatus(const QString &text) { statusText=text; emit stateChanged(); }
void Session::close() {
    if(closing) return;
    QScopedValueRollback guard(closing,true);
    releaseInput();
    ++generation;
    deadline.stop(); heartbeat.stop(); nextFrame.stop();
    if(reply) { reply->disconnect(this); reply->abort(); reply->deleteLater(); reply=nullptr; }
    if(socket) { socket->disconnect(this); socket->abort(); socket->deleteLater(); socket=nullptr; }
    if(webAuthenticated) {
        // Separate manager keeps logout cookies alive without holding up a new connection.
        auto *logout=new QNetworkAccessManager(this);
        auto *jar=new QNetworkCookieJar(logout);
        jar->setCookiesFromUrl(web.cookieJar()->cookiesForUrl(baseUrl),baseUrl);
        logout->setCookieJar(jar);
        QNetworkRequest request(baseUrl.resolved(QUrl("/cgi/logout.cgi")));
        request.setTransferTimeout(3000);
        auto *r=logout->get(request);
        const bool trust=allowUntrusted;
        connect(r,&QNetworkReply::sslErrors,logout,[r,trust](const QList<QSslError>&){if(trust) r->ignoreSslErrors();});
        connect(r,&QNetworkReply::finished,logout,&QObject::deleteLater);
        webAuthenticated=false;
    }
    web.setCookieJar(new QNetworkCookieJar(&web));
    buffer.clear(); launch={}; decoder.reset(); pending=false; forceFull=false; controlling=true;
    phase=Phase::Idle; emit clearFrame(); setStatus("Disconnected");
}
void Session::fail(const QString &text) { close(); setStatus(text); }
void Session::open(const QVariantMap &o) {
    close();
    try {
        allowUntrusted=o.value("allowUntrusted").toBool(); switchJava=o.value("switchJava").toBool();
        frameInterval=qBound(33,o.value("frameInterval",100).toInt(),1000);
        launch.host=o.value("host").toString().trimmed();
        if(launch.host.isEmpty() || launch.host.contains('/') || launch.host.contains('@')) throw Error("Enter a BMC hostname or IP address");
        launch.user=o.value("user").toString(); launch.password=o.value("password").toString();
        if(launch.user.isEmpty() || launch.password.isEmpty()) throw Error("Enter credentials");
        if(o.value("autoArm",true).toBool()) {
            phase=Phase::Web;
            baseUrl=QUrl(); baseUrl.setScheme("https"); baseUrl.setHost(launch.host);
            auto webPort=o.value("webPort",443).toInt();
            if(webPort<1 || webPort>65535) throw Error("Invalid HTTPS port");
            baseUrl.setPort(webPort);
            // QUrlQuery leaves '+' literal; form-urlencoded servers interpret that as a space.
            QByteArray body="name="+QUrl::toPercentEncoding(launch.user)+"&pwd="+QUrl::toPercentEncoding(launch.password);
            launch.user.clear(); launch.password.clear();
            setStatus("Logging in to BMC…"); requestWeb("/cgi/login.cgi",body,0);
        } else {
            auto port=o.value("port",5900).toInt();
            if(port<1 || port>65535) throw Error("Invalid KVM port");
            launch.port=quint16(port); launch.tls=o.value("tls",true).toBool();
            credentials(launch.user,launch.password); connectKvm();
        }
    } catch(const std::exception &e) { fail(QString::fromUtf8(e.what())); }
}
void Session::requestWeb(const QString &path,const QByteArray &body,int step,const QByteArray &csrf) {
    QNetworkRequest request(baseUrl.resolved(QUrl(path)));
    request.setHeader(QNetworkRequest::UserAgentHeader,"Mozilla/5.0 QATENtion");
    request.setRawHeader("Referer",baseUrl.toEncoded());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::SameOriginRedirectPolicy);
    request.setTransferTimeout(15000);
    auto ssl=request.sslConfiguration(); ssl.setProtocol(QSsl::TlsV1_2OrLater); request.setSslConfiguration(ssl);
    if(!csrf.isEmpty()) request.setRawHeader("CSRF_TOKEN",csrf);
    if(!body.isEmpty()) request.setHeader(QNetworkRequest::ContentTypeHeader,"application/x-www-form-urlencoded");
    auto *r=body.isEmpty()? web.get(request) : web.post(request,body);
    reply=r; const auto epoch=generation;
    connect(r,&QNetworkReply::sslErrors,this,[this,r](const QList<QSslError>&){if(allowUntrusted) r->ignoreSslErrors();});
    connect(r,&QNetworkReply::readyRead,this,[this,r]{
        if(r->bytesAvailable()>1024*1024) fail("BMC web response is too large");
    });
    connect(r,&QNetworkReply::finished,this,[this,r,step,epoch]{
        if(epoch!=generation) { r->deleteLater(); return; }
        reply=nullptr; r->deleteLater();
        if(r->error()!=QNetworkReply::NoError) { fail("BMC web request failed: "+r->errorString()); return; }
        auto b=r->readAll();
        if(b.size()>1024*1024) { fail("BMC web response is too large"); return; }
        try { handleWeb(b,step); } catch(const std::exception &e) { fail(QString::fromUtf8(e.what())); }
    });
}
void Session::handleWeb(const QByteArray &body,int step) {
    const QString jnlpPath="/cgi/url_redirect.cgi?url_name=ikvm&url_type=jwsk";
    if(step==0) {
        if(web.cookieJar()->cookiesForUrl(baseUrl).isEmpty()) throw Error("BMC did not create a login session; check credentials");
        webAuthenticated=true;
        setStatus("Requesting console credentials…");
        if(switchJava) requestWeb("/cgi/url_redirect.cgi?url_name=topmenu",{},1);
        else requestWeb(jnlpPath,{},3);
    } else if(step==1) {
        static const QRegularExpression re(R"re(SmcCsrfInsert\s*\(\s*["']CSRF_TOKEN["']\s*,\s*["']([^"']+)["'])re",QRegularExpression::CaseInsensitiveOption);
        const auto m=re.match(QString::fromUtf8(body));
        if(m.hasMatch()) requestWeb("/cgi/op.cgi","op=remote_console&ikvm_setting=0",2,m.captured(1).toLatin1());
        else requestWeb(jnlpPath,{},3);
    } else if(step==2) requestWeb(jnlpPath,{},3);
    else {
        const auto originalHost=launch.host;
        launch=parseJnlp(body);
        // Use the endpoint explicitly chosen by the user (JNLP may contain an unroutable address).
        launch.host=originalHost;
        connectKvm();
    }
}
void Session::connectKvm() {
    phase=Phase::Connecting; setStatus("Connecting to console…");
    decoder=std::make_unique<Decoder>(); socket=new QSslSocket(this);
    socket->setReadBufferSize(MaxPacket+4096);
    connect(socket,&QSslSocket::readyRead,this,&Session::process);
    connect(socket,&QSslSocket::disconnected,this,[this]{if(busy()) fail("BMC disconnected");});
    connect(socket,&QSslSocket::errorOccurred,this,[this](QAbstractSocket::SocketError){
        if(socket) fail("Console connection failed: "+socket->errorString());
    });
    auto begin=[this]{phase=Phase::Banner; deadline.start(15000); setStatus("Authenticating console…"); process();};
    connect(socket,&QSslSocket::connected,this,[this,begin]{
        socket->setSocketOption(QAbstractSocket::LowDelayOption,1);
        if(!launch.tls) begin();
    });
    deadline.start(15000);
    if(launch.tls) {
        if(!QSslSocket::supportsSsl()) throw Error("No TLS backend available");
        QFile pfx(":/resources/client.pfx"); pfx.open(QIODevice::ReadOnly);
        QSslKey key; QSslCertificate cert;
        if(!QSslCertificate::importPkcs12(&pfx,&key,&cert)) throw Error("Cannot load bundled vendor client certificate with this TLS backend");
        socket->setLocalCertificate(cert); socket->setPrivateKey(key);
        socket->setProtocol(QSsl::TlsV1_2);
        connect(socket,&QSslSocket::sslErrors,this,[this](const QList<QSslError>&){if(allowUntrusted) socket->ignoreSslErrors();});
        connect(socket,&QSslSocket::encrypted,this,begin);
        socket->connectToHostEncrypted(launch.host,launch.port);
    } else socket->connectToHost(launch.host,launch.port);
}
void Session::send(const QByteArray &bytes) {
    if(!socket || socket->state()!=QAbstractSocket::ConnectedState) return;
    if(socket->bytesToWrite()>1024*1024) { fail("BMC stopped accepting input"); return; }
    if(socket->write(bytes)!=bytes.size()) fail("Failed to send console message");
}
void Session::process() {
    if(!socket) return;
    buffer+=socket->readAll();
    if(buffer.size()>MaxPacket+4096) { fail("Console receive limit exceeded"); return; }
    try {
        for(int work=0;work<128;++work) {
            switch(phase) {
            case Phase::Banner:
                if(buffer.size()<12) return;
                if(!QRegularExpression("^RFB [0-9]{3}\\.[0-9]{3}\\n$").match(QString::fromLatin1(buffer.first(12))).hasMatch()) throw Error("Invalid RFB banner");
                send(buffer.first(12)); buffer.remove(0,12); phase=Phase::Security; break;
            case Phase::Security: {
                if(buffer.isEmpty()) return;
                const int count=quint8(buffer[0]);
                if(!count) throw Error("BMC refused security negotiation");
                if(buffer.size()<count+1) return;
                const auto types=buffer.mid(1,count);
                if(!types.contains(char(16))) throw Error("Only ATEN token authentication (type 16) is supported");
                send(QByteArray(1,char(16))); buffer.remove(0,count+1); phase=Phase::Challenge; break;
            }
            case Phase::Challenge:
                if(buffer.size()<24) return;
                buffer.remove(0,24); send(credentials(launch.user,launch.password));
                launch.user.clear(); launch.password.clear(); phase=Phase::Auth; break;
            case Phase::Auth:
                if(buffer.size()<4) return;
                if(be32(buffer,0)!=0) throw Error("BMC rejected console credentials; obtain a fresh token");
                buffer.remove(0,4); send(QByteArray(1,0)); phase=Phase::Init; break;
            case Phase::Init: {
                if(buffer.size()<24) return;
                const auto n=be32(buffer,20);
                if(n>65536) throw Error("Server name is too long");
                if(buffer.size()<36+n) return;
                const QSize size(be16(buffer,0),be16(buffer,2)); decoder->resize(size);
                buffer.remove(0,36+n); phase=Phase::Messages; deadline.stop();
                received.start(); fullFrame.start(); heartbeat.start();
                setStatus("Connected — click the console to send input");
                send(QByteArray::fromHex("3732000b01bc070780360001"));
                forceFull=true; requestFrame(); break;
            }
            case Phase::Messages:
                if(!consumeMessage()) return;
                received.restart(); break;
            default: return;
            }
        }
        QTimer::singleShot(0,this,&Session::process);
    } catch(const std::exception &e) { fail(QString::fromUtf8(e.what())); }
}
bool Session::consumeMessage() {
    if(buffer.isEmpty()) return false;
    const auto type=quint8(buffer[0]); qsizetype length=0;
    if(type==0) {
        if(buffer.size()<4) return false;
        const int count=be16(buffer,2);
        if(count>4096) throw Error("Too many framebuffer rectangles");
        length=4;
        for(int i=0;i<count;++i) {
            if(buffer.size()<length+20) return false;
            const auto n=be32(buffer,length+16);
            if(n>MaxPacket || length+20+n>MaxPacket) throw Error("Framebuffer update is too large");
            length+=20+n;
            if(buffer.size()<length) return false;
        }
        qsizetype pos=4; bool changed=false, noSignal=false;
        for(int i=0;i<count;++i) {
            const int x=be16(buffer,pos),y=be16(buffer,pos+2),w=be16(buffer,pos+4),h=be16(buffer,pos+6);
            const auto encoding=be32(buffer,pos+8), n=be32(buffer,pos+16);
            if(!n) noSignal|=w>=0x8000 || h>=0x8000;
            else {
                if(x==0 && y==0 && decoder->image().size()!=QSize(w,h)) { decoder->resize({w,h}); forceFull=true; }
                changed|=decoder->decode(buffer.mid(pos+20,n),encoding);
            }
            pos+=20+n;
        }
        buffer.remove(0,length); pending=false;
        if(noSignal) { emit clearFrame(); setStatus("Connected — no video signal"); }
        else if(changed) { emit frameReady(decoder->image()); setStatus(controlling?"Connected — controlling":"Connected — view only"); }
        nextFrame.start(frameInterval); return true;
    }
    switch(type) {
    case 4:
        if(buffer.size()<21) return false;
        length=21;
        if(be32(buffer,17)==1) {
            const quint64 pixels=quint64(be32(buffer,9))*be32(buffer,13);
            if(pixels>1024*1024) throw Error("Cursor bitmap is too large");
            length=25+qsizetype(pixels)*2;
        }
        break;
    case 0x16:length=2;break;
    case 0x35:length=6;break;
    case 0x37:length=4;break;
    case 0x39:length=265;break;
    case 0x3c:length=9;break;
    default:throw Error("Unknown server message; connection closed to preserve stream alignment");
    }
    if(buffer.size()<length) return false;
    if(type==0x39) {
        const bool grant=!(be32(buffer,1)==1 && be32(buffer,5)==4);
        if(!grant) releaseInput();
        controlling=grant; setStatus(grant?"Connected — controlling":"Connected — view only");
    }
    buffer.remove(0,length); return true;
}
void Session::requestFrame() {
    if(!connected() || pending) return;
    const bool full=forceFull || frameInterval<=33 || fullFrame.elapsed()>=5000;
    forceFull=false; pending=true;
    send(updateRequest(decoder->image().size(),!full));
    if(full) fullFrame.restart();
}
void Session::refresh() { forceFull=true; requestFrame(); }
void Session::key(quint32 usage,bool down) {
    if(!connected() || !controlling || !usage) return;
    if(down) held.insert(usage); else held.remove(usage);
    send(keyEvent(usage,down));
}
void Session::pointer(int x,int y,int mask) {
    if(!connected() || !controlling) return;
    lastPointer={qBound(0,x,decoder->image().width()-1),qBound(0,y,decoder->image().height()-1)};
    buttons=mask; send(pointerEvent(lastPointer.x(),lastPointer.y(),mask));
}
void Session::releaseInput() {
    const auto keys=held; held.clear();
    const bool releaseButtons=buttons!=0; buttons=0;
    for(auto usage:keys) send(keyEvent(usage,false));
    if(releaseButtons) send(pointerEvent(lastPointer.x(),lastPointer.y(),0));
}
void Session::ctrlAltDelete() {
    releaseInput();
    key(0xe0,true); key(0xe2,true); key(0x4c,true);
    key(0x4c,false); key(0xe2,false); key(0xe0,false);
}
}
