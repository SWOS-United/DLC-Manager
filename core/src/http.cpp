// http.cpp -- see http.h.

#include "http.h"

#include "paths.h"

#include <curl/curl.h>

#include <cstdio>
#include <fstream>
#include <mutex>
#include <thread>

namespace swos {

namespace {

constexpr long kConnectTimeoutMs = 10000;
// No total timeout: a DLC pack is tens of megabytes and a slow line is not an
// error. Stalls are caught by the low-speed limit instead.
constexpr long kLowSpeedBytesPerSec = 128;
constexpr long kLowSpeedSeconds = 30;

struct WriteContext {
    std::ofstream *file = nullptr;
    std::string *buffer = nullptr;
    int64_t written = 0;
};

size_t write_callback(char *data, size_t size, size_t nmemb, void *userp)
{
    auto *ctx = static_cast<WriteContext *>(userp);
    const size_t bytes = size * nmemb;
    if (ctx->file)
        ctx->file->write(data, static_cast<std::streamsize>(bytes));
    else if (ctx->buffer)
        ctx->buffer->append(data, bytes);
    ctx->written += static_cast<int64_t>(bytes);
    return bytes;
}

struct ProgressContext {
    const std::function<void(DownloadProgress)> *callback = nullptr;
    const std::atomic<bool> *cancel = nullptr;
};

int progress_callback(void *userp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t)
{
    auto *ctx = static_cast<ProgressContext *>(userp);
    if (ctx->cancel && ctx->cancel->load()) return 1;  // non-zero aborts the transfer
    if (ctx->callback && *ctx->callback)
        (*ctx->callback)(DownloadProgress{static_cast<int64_t>(dlnow),
                                          static_cast<int64_t>(dltotal)});
    return 0;
}

void apply_common_options(CURL *curl, const std::string &url)
{
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, kConnectTimeoutMs);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, kLowSpeedBytesPerSec);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, kLowSpeedSeconds);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);  // 404 becomes an error, not a file
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "swos-dlc-manager/0.1");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
}

DownloadResult perform(CURL *curl, WriteContext &write_ctx, ProgressContext &progress_ctx)
{
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_ctx);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress_ctx);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    char error_buffer[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);

    DownloadResult result;
    const CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status_code);
    result.bytes = write_ctx.written;

    char *content_type = nullptr;
    if (curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &content_type) == CURLE_OK && content_type)
        result.content_type = content_type;
    char *effective_url = nullptr;
    if (curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective_url) == CURLE_OK && effective_url)
        result.effective_url = effective_url;

    if (code == CURLE_OK) {
        result.ok = true;
    } else if (code == CURLE_ABORTED_BY_CALLBACK) {
        result.error = "Cancelled";
    } else {
        result.error = error_buffer[0] ? error_buffer : curl_easy_strerror(code);
    }
    return result;
}

} // namespace

HttpSession::HttpSession()
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

HttpSession::~HttpSession()
{
    curl_global_cleanup();
}

std::string url_encode_segment(const std::string &segment)
{
    CURL *curl = curl_easy_init();
    if (!curl) return segment;

    char *encoded = curl_easy_escape(curl, segment.c_str(), static_cast<int>(segment.size()));
    std::string out = encoded ? encoded : segment;
    if (encoded) curl_free(encoded);
    curl_easy_cleanup(curl);
    return out;
}

DownloadResult download_file(const std::string &url, const std::string &destination,
                             const std::function<void(DownloadProgress)> &on_progress,
                             const std::atomic<bool> *cancel)
{
    DownloadResult result;

    if (!ensure_directory(parent_path(destination))) {
        result.error = "Cannot create " + parent_path(destination);
        return result;
    }

    // Write to a .part file and rename on success, so a cancelled or failed
    // transfer never leaves a plausible-looking truncated file behind.
    const std::string partial = destination + ".part";
    {
        std::ofstream file(partial, std::ios::binary | std::ios::trunc);
        if (!file) {
            result.error = "Cannot write " + partial;
            return result;
        }

        CURL *curl = curl_easy_init();
        if (!curl) {
            result.error = "curl_easy_init failed";
            return result;
        }
        apply_common_options(curl, url);

        WriteContext write_ctx{&file, nullptr, 0};
        ProgressContext progress_ctx{&on_progress, cancel};
        result = perform(curl, write_ctx, progress_ctx);
        curl_easy_cleanup(curl);
    }

    if (!result.ok || result.bytes == 0) {
        if (result.ok) result.error = "Server returned an empty file";
        result.ok = false;
        remove_file(partial);
        return result;
    }

    if (!rename_file(partial, destination)) {
        // Rename fails if the destination exists on some platforms.
        remove_file(destination);
        if (!rename_file(partial, destination)) {
            result.ok = false;
            result.error = "Cannot move " + partial + " into place";
            remove_file(partial);
        }
    }
    return result;
}

DownloadResult download_string(const std::string &url, std::string &out,
                               const std::atomic<bool> *cancel)
{
    out.clear();

    CURL *curl = curl_easy_init();
    if (!curl) {
        DownloadResult result;
        result.error = "curl_easy_init failed";
        return result;
    }
    apply_common_options(curl, url);

    WriteContext write_ctx{nullptr, &out, 0};
    const std::function<void(DownloadProgress)> no_progress;
    ProgressContext progress_ctx{&no_progress, cancel};
    DownloadResult result = perform(curl, write_ctx, progress_ctx);
    curl_easy_cleanup(curl);
    return result;
}

bool reachable(const std::string &url, long timeout_ms)
{
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    const CURLcode code = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return code == CURLE_OK;
}

// ---------------------------------------------------------------------------
// Download
// ---------------------------------------------------------------------------

struct Download::Impl {
    std::thread worker;
    std::atomic<bool> cancel{false};
    std::atomic<bool> running{false};
    std::atomic<bool> finished{false};

    mutable std::mutex mutex;
    DownloadProgress progress;
    DownloadResult result;
    std::string body;

    void join()
    {
        if (worker.joinable()) worker.join();
    }
};

Download::Download() : m_impl(std::make_unique<Impl>()) { }

Download::~Download()
{
    cancel();
    m_impl->join();
}

void Download::cancel()
{
    m_impl->cancel.store(true);
}

bool Download::running() const
{
    return m_impl->running.load();
}

bool Download::finished() const
{
    return m_impl->finished.load();
}

DownloadProgress Download::progress() const
{
    std::lock_guard lock(m_impl->mutex);
    return m_impl->progress;
}

DownloadResult Download::take_result()
{
    m_impl->join();
    m_impl->finished.store(false);

    std::lock_guard lock(m_impl->mutex);
    m_body = m_impl->body;
    return m_impl->result;
}

void Download::start(std::string url, std::string destination)
{
    cancel();
    m_impl->join();

    m_impl->cancel.store(false);
    m_impl->running.store(true);
    m_impl->finished.store(false);
    {
        std::lock_guard lock(m_impl->mutex);
        m_impl->progress = {};
        m_impl->result = {};
        m_impl->body.clear();
    }

    Impl *impl = m_impl.get();
    m_impl->worker = std::thread([impl, url = std::move(url), destination = std::move(destination)] {
        const auto on_progress = [impl](DownloadProgress p) {
            std::lock_guard lock(impl->mutex);
            impl->progress = p;
        };
        DownloadResult result = download_file(url, destination, on_progress, &impl->cancel);
        {
            std::lock_guard lock(impl->mutex);
            impl->result = std::move(result);
        }
        impl->running.store(false);
        impl->finished.store(true);
    });
}

void Download::start_to_memory(std::string url)
{
    cancel();
    m_impl->join();

    m_impl->cancel.store(false);
    m_impl->running.store(true);
    m_impl->finished.store(false);
    {
        std::lock_guard lock(m_impl->mutex);
        m_impl->progress = {};
        m_impl->result = {};
        m_impl->body.clear();
    }

    Impl *impl = m_impl.get();
    m_impl->worker = std::thread([impl, url = std::move(url)] {
        std::string body;
        DownloadResult result = download_string(url, body, &impl->cancel);
        {
            std::lock_guard lock(impl->mutex);
            impl->result = std::move(result);
            impl->body = std::move(body);
        }
        impl->running.store(false);
        impl->finished.store(true);
    });
}

} // namespace swos
