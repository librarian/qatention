#include "certificatechain.h"

#include <QCryptographicHash>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QSslCertificateExtension>
#include <QSslSocket>
#include <QTimer>
#include <QVariant>

#include <memory>

namespace aten {
namespace {
// Process-local, bounded cache. Each use revalidates against current
// roots/time/hostname.
QHash<QByteArray, QList<QSslCertificate>> chains;

QByteArray identity(const QSslCertificate& cert)
{
    return cert.digest(QCryptographicHash::Sha256);
}

constexpr qsizetype MaxDownload = 64LL * 1024;

class Fetch: public QObject
{
public:
    QList<QSslCertificate> chain;
    QString host;
    std::function<void(const QString&)> log;
    std::function<void(bool)> done;
    QNetworkAccessManager network{this};
    QSet<QUrl> visited;
    QTimer deadline{this};
    bool finished = false;

    Fetch(QObject* owner)
        : QObject(owner)
    {
        deadline.setSingleShot(true);
        connect(&deadline, &QTimer::timeout, this, [this] { finish(false); });
        connect(
            &network,
            &QNetworkAccessManager::authenticationRequired,
            this,
            [](QNetworkReply* r, QAuthenticator*) { r->abort(); });
    }

    void finish(bool ok)
    {
        if (finished) {
            return;
        }
        finished = true;
        deadline.stop();
        if (ok) {
            if (chains.size() >= 32) {
                chains.clear();
            }
            chains.insert(identity(chain.first()), chain.mid(1));
        }
        log(ok ? "HTTPS: completed certificate chain and hostname verified"
               : "HTTPS: could not complete a trusted certificate chain");
        deleteLater();
        done(ok);
    }

    void next()
    {
        if (CertificateChain::verify(chain, host)) {
            finish(true);
            return;
        }
        if (chain.isEmpty() || chain.size() >= 8 || visited.size() >= 6) {
            finish(false);
            return;
        }
        const auto url = CertificateChain::issuerUrl(chain.last());
        if (url.isEmpty() || visited.contains(url)) {
            finish(false);
            return;
        }
        visited.insert(url);
        log("HTTPS: fetching missing issuer from " + url.toString());
        QNetworkRequest request(url);
        request.setTransferTimeout(5000);
        request.setAttribute(
            QNetworkRequest::RedirectPolicyAttribute,
            QNetworkRequest::ManualRedirectPolicy);
        request.setAttribute(
            QNetworkRequest::CookieLoadControlAttribute,
            QNetworkRequest::Manual);
        request.setAttribute(
            QNetworkRequest::CookieSaveControlAttribute,
            QNetworkRequest::Manual);
        auto* reply = network.get(request);
        reply->setReadBufferSize(MaxDownload + 1);
        auto data = std::make_shared<QByteArray>();
        connect(
            reply,
            &QNetworkReply::readyRead,
            this,
            [reply, data]
            {
                data->append(reply->readAll());
                if (data->size() > MaxDownload) {
                    reply->abort();
                }
            });
        connect(
            reply,
            &QNetworkReply::finished,
            this,
            [this, reply, data]
            {
                reply->deleteLater();
                if (finished) {
                    return;
                }
                data->append(reply->readAll());
                if (reply->error() != QNetworkReply::NoError ||
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                            .toInt() != 200 ||
                    data->size() > MaxDownload)
                {
                    finish(false);
                    return;
                }
                auto certs = QSslCertificate::fromData(*data, QSsl::Der);
                if (certs.isEmpty()) {
                    certs = QSslCertificate::fromData(*data, QSsl::Pem);
                }
                // AIA caIssuers endpoints normally return one DER certificate.
                // Also accept PEM chains.
                if (certs.isEmpty() || chain.size() + certs.size() > 8) {
                    finish(false);
                    return;
                }
                for (const auto& cert: certs) {
                    if (cert.isNull() || chain.contains(cert)) {
                        finish(false);
                        return;
                    }
                    chain.append(cert);
                }
                next();
            });
    }
};
}   // namespace

bool CertificateChain::missingIssuerOnly(const QList<QSslError>& errors)
{
    if (errors.isEmpty()) {
        return false;
    }
    for (const auto& e: errors) {
        switch (e.error()) {
            case QSslError::UnableToGetIssuerCertificate:
            case QSslError::UnableToGetLocalIssuerCertificate:
            case QSslError::UnableToVerifyFirstCertificate:
                break;
            default:
                return false;
        }
    }
    return true;
}

bool CertificateChain::verify(
    const QList<QSslCertificate>& chain,
    const QString& host)
{
    if (chain.isEmpty() || host.isEmpty()) {
        return false;
    }
    // Qt's standalone OpenSSL chain verifier does not set sslserver purpose.
    // Enforce EKU on every supplied certificate as well as the handshake's
    // checks.
    for (const auto& cert: chain) {
        if (cert.isBlacklisted()) {
            return false;
        }
        for (const auto& ext: cert.extensions()) {
            if (ext.oid() != "2.5.29.37") {
                continue;
            }
            bool server = false;
            for (const auto& usage: ext.value().toList()) {
                const auto name = usage.toString();
                if (name == "TLS Web Server Authentication" ||
                    name == "1.3.6.1.5.5.7.3.1" ||
                    name == "Any Extended Key Usage" || name == "2.5.29.37.0")
                {
                    server = true;
                }
            }
            if (!server) {
                return false;
            }
        }
    }
    return QSslCertificate::verify(chain, host).isEmpty();
}

QList<QSslCertificate> CertificateChain::cached(
    const QList<QSslCertificate>& peer)
{
    if (peer.isEmpty()) {
        return peer;
    }
    auto result = peer;
    for (const auto& cert: chains.value(identity(peer.first()))) {
        if (!result.contains(cert)) {
            result.append(cert);
        }
    }
    return result;
}

QUrl CertificateChain::issuerUrl(const QSslCertificate& certificate)
{
    for (const auto& ext: certificate.extensions()) {
        if (ext.oid() != "1.3.6.1.5.5.7.1.1") {
            continue;
        }
        const QUrl url(
            ext.value().toMap().value("caIssuers").toString(),
            QUrl::StrictMode);
        if (url.isValid() &&
            (url.scheme() == "http" || url.scheme() == "https") &&
            !url.host().isEmpty() && url.userInfo().isEmpty() &&
            !url.hasFragment())
        {
            return url;
        }
    }
    return {};
}

void CertificateChain::fetch(
    QObject* owner,
    const QList<QSslCertificate>& peer,
    const QString& host,
    std::function<void(const QString&)> log,
    std::function<void(bool)> done)
{
    auto* job = new Fetch(owner);
    job->chain = cached(peer);
    job->host = host;
    job->log = std::move(log);
    job->done = std::move(done);
    job->deadline.start(15000);
    job->next();
}
}   // namespace aten
