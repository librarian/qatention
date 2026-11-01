#include "nsshttp.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QVariant>

#include <algorithm>
#include <cstring>
#include <prerror.h>
#include <prinrval.h>

namespace aten {
namespace {
constexpr qsizetype MaxIssuerBytes = 64LL * 1024;
constexpr qsizetype MaxIssuerRequests = 6;
thread_local IssuerDownloads* currentDownloads = nullptr;

struct HttpServer
{
    QString host;
    quint16 port;
};

struct HttpRequest
{
    QUrl url;
    QByteArray body;
    QByteArray contentType;
    int timeoutMs = 5000;
    bool cancelled = false;
};

SECStatus failed()
{
    PR_SetError(PR_IO_ERROR, 0);
    return SECFailure;
}

bool stopped()
{
    return !currentDownloads || currentDownloads->cancelled->load() ||
           currentDownloads->deadline.hasExpired();
}

SECStatus
createServer(const char* host, PRUint16 port, SEC_HTTP_SERVER_SESSION* result)
{
    if (stopped() || !host || !result) {
        return failed();
    }
    *result = new HttpServer{QString::fromUtf8(host), port};
    return SECSuccess;
}

SECStatus keepServer(SEC_HTTP_SERVER_SESSION, PRPollDesc** poll)
{
    if (poll) {
        *poll = nullptr;
    }
    return SECSuccess;
}

SECStatus freeServer(SEC_HTTP_SERVER_SESSION server)
{
    delete static_cast<HttpServer*>(server);
    return SECSuccess;
}

SECStatus createRequest(
    SEC_HTTP_SERVER_SESSION session,
    const char* scheme,
    const char* path,
    const char* method,
    PRIntervalTime timeout,
    SEC_HTTP_REQUEST_SESSION* result)
{
    if (stopped() || !session || !scheme || !path || !method || !result ||
        std::strcmp(method, "GET") != 0 ||
        (std::strcmp(scheme, "http") != 0 && std::strcmp(scheme, "https") != 0))
    {
        return failed();
    }
    const auto& server = *static_cast<HttpServer*>(session);
    QUrl origin;
    origin.setScheme(QString::fromLatin1(scheme));
    origin.setHost(server.host);
    origin.setPort(server.port);
    const auto relative = QUrl::fromEncoded(path, QUrl::StrictMode);
    // NSS supplies an origin-relative path and query. Reject authority changes,
    // credentials and fragments instead of letting URL resolution follow them.
    if (!relative.isRelative() || !relative.host().isEmpty() ||
        !relative.path().startsWith('/') || relative.hasFragment())
    {
        return failed();
    }
    const auto url = origin.resolved(relative);
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() ||
        currentDownloads->visited.contains(url) ||
        currentDownloads->visited.size() >= MaxIssuerRequests)
    {
        return failed();
    }
    currentDownloads->visited.insert(url);
    auto* request = new HttpRequest;
    request->url = url;
    if (timeout != PR_INTERVAL_NO_TIMEOUT) {
        request->timeoutMs = static_cast<int>(
            std::min<PRUint32>(5000, PR_IntervalToMilliseconds(timeout)));
    }
    *result = request;
    return SECSuccess;
}

SECStatus
rejectPost(SEC_HTTP_REQUEST_SESSION, const char*, PRUint32, const char*)
{
    return failed();
}

SECStatus ignoreHeader(SEC_HTTP_REQUEST_SESSION, const char*, const char*)
{
    // AIA GETs need no cookies, authorization, or application-provided headers.
    return SECSuccess;
}

SECStatus receive(
    SEC_HTTP_REQUEST_SESSION session,
    PRPollDesc** poll,
    PRUint16* responseCode,
    const char** contentType,
    const char** headers,
    const char** data,
    PRUint32* length)
{
    if (poll) {
        *poll = nullptr;
    }
    const auto callerLimit = length ? *length : 0;
    if (length) {
        *length = 0;
    }
    if (stopped() || !session) {
        return failed();
    }
    auto& request = *static_cast<HttpRequest*>(session);
    if (request.cancelled || request.timeoutMs <= 0) {
        return failed();
    }
    const auto byteLimit =
        callerLimit == 0 ? MaxIssuerBytes
                         : std::min<qsizetype>(MaxIssuerBytes, callerLimit);
    QNetworkAccessManager network;
    QNetworkRequest download(request.url);
    download.setTransferTimeout(request.timeoutMs);
    download.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::ManualRedirectPolicy);
    download.setAttribute(
        QNetworkRequest::CookieLoadControlAttribute,
        QNetworkRequest::Manual);
    download.setAttribute(
        QNetworkRequest::CookieSaveControlAttribute,
        QNetworkRequest::Manual);
    QObject::connect(
        &network,
        &QNetworkAccessManager::authenticationRequired,
        &network,
        [](QNetworkReply* reply, QAuthenticator*) { reply->abort(); });
    auto* reply = network.get(download);
    reply->setReadBufferSize(byteLimit + 1);
    QEventLoop loop;
    QTimer cancellation;
    QDeadlineTimer requestDeadline(request.timeoutMs);
    QObject::connect(
        &cancellation,
        &QTimer::timeout,
        reply,
        [&]
        {
            if (stopped() || request.cancelled ||
                requestDeadline.hasExpired()) {
                reply->abort();
            }
        });
    QObject::connect(
        reply,
        &QNetworkReply::readyRead,
        reply,
        [&]
        {
            request.body.append(reply->readAll());
            if (request.body.size() > byteLimit) {
                reply->abort();
            }
        });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    cancellation.start(25);
    if (!reply->isFinished()) {
        loop.exec();
    }
    if (reply->isOpen()) {
        request.body.append(reply->readAll());
    }
    if (request.body.size() > byteLimit) {
        if (length) {
            *length = static_cast<PRUint32>(request.body.size());
        }
        return failed();
    }
    const auto status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (stopped() || reply->error() != QNetworkReply::NoError || status != 200)
    {
        return failed();
    }
    request.contentType =
        reply->header(QNetworkRequest::ContentTypeHeader).toByteArray();
    if (responseCode) {
        *responseCode = 200;
    }
    if (contentType) {
        *contentType = request.contentType.constData();
    }
    if (headers) {
        *headers = "";
    }
    if (data) {
        *data = request.body.constData();
    }
    if (length) {
        *length = static_cast<PRUint32>(request.body.size());
    }
    return SECSuccess;
}

SECStatus cancelRequest(SEC_HTTP_REQUEST_SESSION session)
{
    static_cast<HttpRequest*>(session)->cancelled = true;
    return SECSuccess;
}

SECStatus freeRequest(SEC_HTTP_REQUEST_SESSION session)
{
    delete static_cast<HttpRequest*>(session);
    return SECSuccess;
}
}   // namespace

IssuerDownloadScope::IssuerDownloadScope(IssuerDownloads* downloads)
    : previous(currentDownloads)
{
    currentDownloads = downloads;
}

IssuerDownloadScope::~IssuerDownloadScope()
{
    currentDownloads = previous;
}

const SEC_HttpClientFcn* nssHttpClient()
{
    static const SEC_HttpClientFcn client = {
        1,
        {{createServer,
          keepServer,
          freeServer,
          createRequest,
          rejectPost,
          ignoreHeader,
          receive,
          cancelRequest,
          freeRequest}}};
    return &client;
}
}   // namespace aten
