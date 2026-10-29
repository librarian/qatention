#pragma once
#include <QFile>
#include <QObject>

namespace aten {
class DebugLog: public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QString path READ path CONSTANT)
public:
    explicit DebugLog(const QString& path, QObject* parent = nullptr);

    bool enabled() const
    {
        return active;
    }

    QString text() const
    {
        return lines;
    }

    QString path() const
    {
        return file.fileName();
    }

    void setEnabled(bool enabled);
    void write(const QString& message);
    void capture(const QByteArray& packet);
    Q_INVOKABLE void clear();
    static constexpr qint64 MaxFileBytes = 10LL * 1024 * 1024;
signals:
    void enabledChanged();
    void changed();

private:
    QFile file;
    QString lines;
    bool active = false, captured = false;
};
}   // namespace aten
