#pragma once
#include <QFile>
#include <QImage>
#include <QJsonObject>

namespace aten {
QByteArray imageHash(const QImage& image);

class VideoTrace
{
public:
    bool start(const QString& path);

    void stop()
    {
        file.close();
    }

    bool active() const
    {
        return file.isOpen();
    }

    bool record(QJsonObject event);

private:
    QFile file;
};

// Replays a bounded trace offline and compares every recorded output hash.
QString replayVideoTrace(const QString& path, const QString& output = {});
}   // namespace aten
