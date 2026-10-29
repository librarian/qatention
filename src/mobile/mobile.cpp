#include "mobile.h"
#include "core/input.h"
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QGuiApplication>
#include <QClipboard>
#include <QPainter>
#include <QUrl>

Mobile::Mobile():log(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/logs/qatention.log") {
    log.setEnabled(true);
    connect(&session,&aten::Session::diagnostic,&log,&aten::DebugLog::write);
    connect(&session,&aten::Session::consoleCertificateUntrusted,this,[this](const QString &host,quint16 port,const QSslCertificate &cert){
        const auto hash=QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex());
        pendingTrust={{"host",host},{"port",port},{"sha256",hash}};
        emit trustRequested(host+":"+QString::number(port),hash);
    });
    session.setStreaming(true,50,5);
}
QString Mobile::trustKey() const {
    return "consoleTrust/"+QString::fromLatin1(QCryptographicHash::hash(options.value("host").toString().toLower().toUtf8(),QCryptographicHash::Sha256).toHex());
}
void Mobile::connectTo(const QString &host,const QString &user,const QString &password,bool unverified) {
    disconnectSession();
    auto normalized=host.trimmed();if(normalized.contains("://")) normalized=QUrl(normalized).host();
    if(normalized.isEmpty() || user.isEmpty() || password.isEmpty()) {emit notice("Enter a host, username and password.");return;}
    options={{"host",normalized},{"user",user},{"password",password},{"autoArm",true},{"autoReconnect",false},{"allowUntrusted",unverified}};
    options["consoleTrust"]=settings.value(trustKey()).toMap();
    settings.setValue("host",normalized);settings.setValue("user",user);
    session.open(options);
}
void Mobile::disconnectSession() {
    pendingTrust.clear();session.close();options.clear();
}
void Mobile::trustConsole() {
    if(pendingTrust.isEmpty() || options.isEmpty() || session.busy()) return;
    settings.setValue(trustKey(),pendingTrust);settings.sync();
    if(settings.status()!=QSettings::NoError) {emit notice("Cannot save console certificate trust.");return;}
    options["consoleTrust"]=pendingTrust;pendingTrust.clear();session.open(options);
}
void Mobile::tapKey(int usage) {if(session.connected()) {session.key(usage,true);session.key(usage,false);}}
void Mobile::typeText(const QString &text) {
    if(!session.connected()) return;
    if(text.size()>256) {emit notice("Send at most 256 characters at a time.");return;}
    for(auto c:text) {bool shift=false;if(!aten::characterUsage(c,shift)) {emit notice("This preview supports US-layout ASCII text only.");return;}}
    // Send in order immediately; do not replace a pending character queue when
    // another touch arrives, or let Enter/Backspace overtake queued text.
    for(auto c:text) {
        bool shift=false;const auto usage=aten::characterUsage(c,shift);
        if(shift) session.key(0xe1,true);
        session.key(usage,true);session.key(usage,false);
        if(shift) session.key(0xe1,false);
    }
}
void Mobile::copyLogs() {QGuiApplication::clipboard()->setText(log.text());}
void Mobile::background(bool active) {
    session.releaseInput();session.setStreaming(active,50,5);
}
QRectF MobileVideo::imageRect() const {
    QSizeF size=frame.size();size.scale(boundingRect().size(),Qt::KeepAspectRatio);
    return {(width()-size.width())/2,(height()-size.height())/2,size.width(),size.height()};
}
void MobileVideo::setSession(aten::Session *s) {
    if(connection==s) return;
    release();if(connection) disconnect(connection,nullptr,this,nullptr);connection=s;frame={};
    if(s) {
        connect(s,&aten::Session::frameReady,this,[this](const QImage &image){frame=image;update();});
        connect(s,&aten::Session::clearFrame,this,[this]{frame={};update();});
    }
    update();emit sessionChanged();
}
void MobileVideo::paint(QPainter *p) {p->fillRect(boundingRect(),Qt::black);if(!frame.isNull()) p->drawImage(imageRect(),frame);}
void MobileVideo::pointer(qreal x,qreal y,bool down) {
    if(!connection || frame.isNull() || !connection->connected()) return;
    const auto r=imageRect();if(r.isEmpty()) return;
    const int px=qBound(0,int((x-r.x())*frame.width()/r.width()),frame.width()-1);
    const int py=qBound(0,int((y-r.y())*frame.height()/r.height()),frame.height()-1);
    connection->pointer(px,py,down?1:0);
}
void MobileVideo::release() {if(connection) connection->releaseInput();}
