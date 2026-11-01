#pragma once
#include <QObject>
#include <QSslCertificate>
#include <QSslError>

#include <functional>

namespace aten {
// Downloaded certificates are untrusted chain material, never CA trust anchors.
class CertificateChain
{
public:
    static bool available();
    static bool missingIssuerOnly(const QList<QSslError>& errors);
    static bool verify(
        const QList<QSslCertificate>& chain,
        const QString& host);
    static QList<QSslCertificate> cached(const QList<QSslCertificate>& peer);
    static void fetch(
        QObject* owner,
        const QList<QSslCertificate>& peer,
        const QString& host,
        std::function<void(const QString&)> log,
        std::function<void(bool)> done);
};
}   // namespace aten
