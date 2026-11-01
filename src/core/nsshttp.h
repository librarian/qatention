#pragma once
#include "nssheaders.h"

#include <QDeadlineTimer>
#include <QSet>
#include <QUrl>

#include <atomic>
#include <memory>

namespace aten {
// One verification's network budget. NSS chooses issuers and parses
// certificates; this adapter only transports bytes under the application's
// network limits.
struct IssuerDownloads
{
    std::shared_ptr<std::atomic_bool> cancelled;
    QDeadlineTimer deadline{15000};
    QSet<QUrl> visited;
};

class IssuerDownloadScope
{
public:
    explicit IssuerDownloadScope(IssuerDownloads* downloads);
    ~IssuerDownloadScope();
    IssuerDownloadScope(const IssuerDownloadScope&) = delete;
    IssuerDownloadScope& operator=(const IssuerDownloadScope&) = delete;

private:
    IssuerDownloads* previous;
};

const SEC_HttpClientFcn* nssHttpClient();
}   // namespace aten
