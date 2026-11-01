#include "certificatechain.h"

#include "nsshttp.h"

#include <QCryptographicHash>
#include <QFutureWatcher>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QSslConfiguration>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <QtConcurrentRun>

#include <array>
#include <memory>
#include <vector>

namespace aten {
namespace {
using Certificate =
    std::unique_ptr<CERTCertificate, decltype(&CERT_DestroyCertificate)>;
using CertificateList =
    std::unique_ptr<CERTCertList, decltype(&CERT_DestroyCertList)>;

class NssRuntime
{
public:
    NssRuntime()
    {
        // No persistent NSS database or separate root store. Every verification
        // receives the same trust anchors as Qt's current TLS configuration.
        context = NSS_InitContext(
            "",
            "",
            "",
            "",
            nullptr,
            NSS_INIT_READONLY | NSS_INIT_NOCERTDB | NSS_INIT_NOMODDB |
                NSS_INIT_NOROOTINIT | NSS_INIT_OPTIMIZESPACE);
        ready = context &&
                CERT_SetUsePKIXForValidation(PR_TRUE) == SECSuccess &&
                SEC_RegisterDefaultHttpClient(nssHttpClient()) == SECSuccess;
        workers.setMaxThreadCount(2);
    }

    ~NssRuntime()
    {
        // NSS and the HTTP callback table must outlive every verification.
        workers.waitForDone();
        if (context) {
            NSS_ShutdownContext(context);
        }
    }

    bool ready = false;
    QThreadPool workers;

private:
    NSSInitContext* context = nullptr;
};

NssRuntime& runtime()
{
    static NssRuntime instance;
    return instance;
}

struct CertificateCache
{
    QMutex mutex;
    QHash<QByteArray, QList<QSslCertificate>> chains;
};

CertificateCache& certificateCache()
{
    static CertificateCache cache;
    return cache;
}

QByteArray identity(const QSslCertificate& certificate)
{
    return certificate.digest(QCryptographicHash::Sha256);
}

Certificate importCertificate(const QSslCertificate& certificate)
{
    auto der = certificate.toDer();
    if (der.isEmpty() || der.size() > 1024LL * 1024 ||
        certificate.isBlacklisted())
    {
        return {nullptr, CERT_DestroyCertificate};
    }
    SECItem encoded{
        siBuffer,
        reinterpret_cast<unsigned char*>(der.data()),
        static_cast<unsigned int>(der.size())};
    // copyDER ensures NSS owns its bytes after this QByteArray is destroyed.
    return {
        CERT_NewTempCertificate(
            CERT_GetDefaultCertDB(),
            &encoded,
            nullptr,
            PR_FALSE,
            PR_TRUE),
        CERT_DestroyCertificate};
}

struct Verification
{
    bool trusted = false;
    QList<QSslCertificate> chain;
};

Verification verifyWithNss(
    const QList<QSslCertificate>& peer,
    const QString& host,
    const QList<QSslCertificate>& roots,
    IssuerDownloads* downloads = nullptr)
{
    if (peer.isEmpty() || peer.size() > 8 || roots.isEmpty() ||
        host.isEmpty() || host.contains(QChar::Null) || !runtime().ready ||
        (downloads &&
         (downloads->cancelled->load() || downloads->deadline.hasExpired())))
    {
        return {};
    }
    IssuerDownloadScope networkScope(downloads);
    std::vector<Certificate> certificates;
    for (const auto& certificate: peer) {
        auto imported = importCertificate(certificate);
        if (!imported) {
            return {};
        }
        certificates.push_back(std::move(imported));
    }
    const auto hostname = QUrl::toAce(host);
    if (hostname.isEmpty() ||
        CERT_VerifyCertName(certificates.front().get(), hostname.constData()) !=
            SECSuccess)
    {
        return {};
    }
    CertificateList anchors(CERT_NewCertList(), CERT_DestroyCertList);
    if (!anchors) {
        return {};
    }
    for (const auto& root: roots) {
        auto imported = importCertificate(root);
        if (!imported) {
            continue;
        }
        // The list takes ownership on successful insertion.
        auto* anchor = imported.release();
        if (CERT_AddCertToListTail(anchors.get(), anchor) != SECSuccess) {
            CERT_DestroyCertificate(anchor);
            return {};
        }
    }
    if (CERT_LIST_EMPTY(anchors.get())) {
        return {};
    }
    // certificateUsageSSLServer selects NSS's TLS-server key usage and EKU
    // checks, in addition to signatures, validity and CA constraints.
    std::array<CERTValInParam, 5> parameters{};
    parameters[0].type = cert_pi_trustAnchors;
    parameters[0].value.pointer.chain = anchors.get();
    parameters[1].type = cert_pi_useOnlyTrustAnchors;
    parameters[1].value.scalar.b = PR_TRUE;
    parameters[2].type = cert_pi_useAIACertFetch;
    parameters[2].value.scalar.b = downloads ? PR_TRUE : PR_FALSE;
    parameters[3].type = cert_pi_revocationFlags;
    parameters[3].value.pointer.revocation =
        CERT_GetClassicOCSPDisabledPolicy();
    parameters[4].type = cert_pi_end;
    std::array<CERTValOutParam, 2> output{};
    output[0].type = cert_po_certList;
    output[1].type = cert_po_end;
    const auto status = CERT_PKIXVerifyCert(
        certificates.front().get(),
        certificateUsageSSLServer,
        parameters.data(),
        output.data(),
        nullptr);
    CertificateList verified(
        output[0].value.pointer.chain,
        CERT_DestroyCertList);
    if (status != SECSuccess || !verified ||
        (downloads &&
         (downloads->cancelled->load() || downloads->deadline.hasExpired())))
    {
        return {};
    }
    Verification result;
    for (auto* node = CERT_LIST_HEAD(verified.get());
         !CERT_LIST_END(node, verified.get());
         node = CERT_LIST_NEXT(node))
    {
        const auto& der = node->cert->derCert;
        const QSslCertificate certificate(
            QByteArray(
                reinterpret_cast<const char*>(der.data),
                static_cast<qsizetype>(der.len)),
            QSsl::Der);
        if (certificate.isNull() || certificate.isBlacklisted() ||
            result.chain.size() >= 8)
        {
            return {};
        }
        result.chain.append(certificate);
    }
    result.trusted = !result.chain.isEmpty();
    return result;
}

class VerificationJob: public QObject
{
public:
    VerificationJob(
        QObject* owner,
        QList<QSslCertificate> peer,
        QString host,
        std::function<void(const QString&)> log,
        std::function<void(bool)> done)
        : QObject(owner)
        , hostname(std::move(host))
        , report(std::move(log))
        , complete(std::move(done))
    {
        connect(
            this,
            &QObject::destroyed,
            [cancelled = cancellation] { cancelled->store(true); });
        connect(&deadline, &QTimer::timeout, this, [this] { finish(false); });
        connect(
            &watcher,
            &QFutureWatcher<Verification>::finished,
            this,
            [this]
            {
                const auto result = watcher.result();
                // Trust settings might have changed while the worker was
                // fetching.
                const bool trusted =
                    result.trusted &&
                    CertificateChain::verify(result.chain, hostname);
                if (trusted) {
                    auto& cache = certificateCache();
                    QMutexLocker lock(&cache.mutex);
                    if (cache.chains.size() >= 32) {
                        cache.chains.clear();
                    }
                    cache.chains.insert(
                        identity(result.chain.first()),
                        result.chain.mid(1));
                }
                finish(trusted);
            });
        const auto roots =
            QSslConfiguration::defaultConfiguration().caCertificates();
        // The deadline includes time waiting for a free worker.
        IssuerDownloads downloads{cancellation, QDeadlineTimer(15000), {}};
        deadline.setSingleShot(true);
        deadline.start(15000);
        watcher.setFuture(
            QtConcurrent::run(
                &runtime().workers,
                [peer = std::move(peer),
                 host = hostname,
                 roots,
                 downloads = std::move(downloads)]() mutable
                { return verifyWithNss(peer, host, roots, &downloads); }));
    }

private:
    void finish(bool trusted)
    {
        if (finished) {
            return;
        }
        finished = true;
        cancellation->store(true);
        deadline.stop();
        const auto reportResult = report;
        const auto completeResult = complete;
        const QPointer<VerificationJob> guard(this);
        deleteLater();
        reportResult(
            trusted
                ? "HTTPS: NSS verified certificate chain and hostname"
                : "HTTPS: NSS could not complete a trusted certificate chain");
        if (guard) {
            completeResult(trusted);
        }
    }

    QString hostname;
    std::function<void(const QString&)> report;
    std::function<void(bool)> complete;
    std::shared_ptr<std::atomic_bool> cancellation =
        std::make_shared<std::atomic_bool>(false);
    QTimer deadline{this};
    QFutureWatcher<Verification> watcher{this};
    bool finished = false;
};
}   // namespace

bool CertificateChain::available()
{
    return runtime().ready;
}

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
    return verifyWithNss(
               chain,
               host,
               QSslConfiguration::defaultConfiguration().caCertificates())
        .trusted;
}

QList<QSslCertificate> CertificateChain::cached(
    const QList<QSslCertificate>& peer)
{
    if (peer.isEmpty()) {
        return peer;
    }
    auto& cache = certificateCache();
    QMutexLocker lock(&cache.mutex);
    auto result = peer;
    for (const auto& certificate: cache.chains.value(identity(peer.first()))) {
        if (!result.contains(certificate)) {
            result.append(certificate);
        }
    }
    return result;
}

void CertificateChain::fetch(
    QObject* owner,
    const QList<QSslCertificate>& peer,
    const QString& host,
    std::function<void(const QString&)> log,
    std::function<void(bool)> done)
{
    new VerificationJob(
        owner,
        cached(peer),
        host,
        std::move(log),
        std::move(done));
}
}   // namespace aten
