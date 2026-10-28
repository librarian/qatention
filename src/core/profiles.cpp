#include "profiles.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
namespace aten {
QVariantMap Profiles::sanitize(const QVariantMap &o) {
    QVariantMap result;
    for(const auto &key : {"host","autoArm","port","tls","webPort","allowUntrusted","switchJava","frameInterval","autoReconnect"})
        if(o.contains(key)) result[key]=o[key];
    if(o.value("autoArm",true).toBool()) result["user"]=o.value("user");
    return result;
}
Profiles::Profiles(QString file,QObject *parent):QObject(parent),path(std::move(file)) {
    QFile f(path);
    if(!f.exists()) return;
    QJsonParseError e;
    if(!f.open(QIODevice::ReadOnly) || f.size()>1024*1024) {
        readable=false; problem="Cannot read profiles file: "+path; return;
    }
    auto doc=QJsonDocument::fromJson(f.readAll(),&e);
    if(e.error!=QJsonParseError::NoError || !doc.isObject() || doc.object().value("version").toInt()!=1 || !doc.object().value("profiles").isArray()) {
        readable=false; problem="Invalid profiles file; move it aside before saving: "+path; return;
    }
    for(auto entry:doc.object().value("profiles").toArray()) {
        auto o=entry.toObject().toVariantMap();
        auto clean=sanitize(o); clean["name"]=o.value("name"); items.append(clean);
    }
}
bool Profiles::write(const QVariantList &next) {
    if(!readable) { emit changed(); return false; }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    const auto bytes=QJsonDocument(QJsonObject{{"version",1},{"profiles",QJsonArray::fromVariantList(next)}}).toJson();
    if(!f.open(QIODevice::WriteOnly) || !f.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner) || f.write(bytes)!=bytes.size() || !f.commit()) {
        problem="Cannot save profiles: "+f.errorString(); emit changed(); return false;
    }
    items=next; problem.clear(); emit changed(); return true;
}
bool Profiles::save(const QString &name,const QVariantMap &o) {
    const auto label=name.trimmed();
    if(label.isEmpty() || label.size()>128 || o.value("host").toString().trimmed().isEmpty()) {
        problem="Enter a profile name and BMC host."; emit changed(); return false;
    }
    auto next=items; auto entry=sanitize(o); entry["name"]=label;
    for(auto &item:next) if(item.toMap().value("name")==label) { item=entry; return write(next); }
    if(next.size()>=100) { problem="Maximum of 100 profiles reached."; emit changed(); return false; }
    next.append(entry); return write(next);
}
bool Profiles::remove(const QString &name) {
    auto next=items;
    for(qsizetype i=0;i<next.size();++i) if(next[i].toMap().value("name")==name) { next.removeAt(i); break; }
    return write(next);
}
}
