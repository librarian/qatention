#pragma once
#include <QObject>
#include <QVariantList>

namespace aten {
class Profiles: public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit Profiles(QString path, QObject* parent = nullptr);

    QVariantList entries() const
    {
        return items;
    }

    QString error() const
    {
        return problem;
    }

    Q_INVOKABLE bool save(const QString& name, const QVariantMap& options);
    Q_INVOKABLE bool remove(const QString& name);
signals:
    void changed();

private:
    QString path, problem;
    QVariantList items;
    bool readable = true;
    bool write(const QVariantList& next);
    static QVariantMap sanitize(const QVariantMap& options);
};
}   // namespace aten
