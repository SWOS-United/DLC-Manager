// http.h -- file downloads over HTTP(S).
//
// Replaces the Delphi launcher's mix of Indy (TIdHTTP), WinINet and
// TDownloadURL, none of which port. libcurl covers all three uses: a progress
// callback for the UI, a HEAD-style reachability probe, and a plain GET to a
// file.
//
// Downloads run on a worker thread and report progress through a callback that
// may therefore be invoked from that thread -- see Download::poll() for the
// thread-safe way to drive a UI from it.

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace swos {

struct DownloadProgress {
    int64_t bytes_received = 0;
    int64_t bytes_total = 0;  // 0 when the server does not send a length

    float fraction() const
    {
        if (bytes_total <= 0) return 0.0f;
        return static_cast<float>(bytes_received) / static_cast<float>(bytes_total);
    }
};

struct DownloadResult {
    bool ok = false;
    long status_code = 0;
    int64_t bytes = 0;
    std::string error;
    // The server's Content-Type, and the URL actually fetched after redirects.
    // Both matter because this server soft-404s: a missing file 301s to the
    // site root, which answers 200 with an HTML page. Without checking one of
    // these, a "successful" download is a web page.
    std::string content_type;
    std::string effective_url;
};

// One-shot blocking download. `destination` is written atomically: the body
// goes to `<destination>.part` and is renamed on success, so an interrupted
// download can never be mistaken for a complete file -- which the Delphi code
// guarded against after the fact by checking for a zero-length result.
DownloadResult download_file(const std::string &url, const std::string &destination,
                             const std::function<void(DownloadProgress)> &on_progress = {},
                             const std::atomic<bool> *cancel = nullptr);

// Blocking GET into memory. Used for the small *-info.csv catalogues.
DownloadResult download_string(const std::string &url, std::string &out,
                               const std::atomic<bool> *cancel = nullptr);

// Cheap connectivity probe, replacing TUtil.internetConnected. Does a HEAD
// against `url` with a short timeout.
bool reachable(const std::string &url, long timeout_ms = 4000);

// Percent-encodes a single path segment. DLC names contain spaces, brackets and
// apostrophes ("English - Original 96/97 (Jonathan Pearce)"), so the URL cannot
// be built by concatenation.
std::string url_encode_segment(const std::string &segment);

// Process-wide libcurl init/teardown. Construct one in main() before any
// download; curl_global_init is not thread-safe and must not race with it.
class HttpSession {
public:
    HttpSession();
    ~HttpSession();
    HttpSession(const HttpSession &) = delete;
    HttpSession &operator=(const HttpSession &) = delete;
};

// An asynchronous download, so the UI keeps painting while a 60 MB sprite pack
// comes down. Start it, call poll() once per frame from the UI thread, and read
// progress()/result() from there -- both are snapshots taken under a lock, so
// the worker can update them freely.
class Download {
public:
    Download();
    ~Download();
    Download(const Download &) = delete;
    Download &operator=(const Download &) = delete;

    // Begins a download to `destination`. Any previous one is cancelled and
    // joined first.
    void start(std::string url, std::string destination);
    // Same, but the body is kept in memory and handed back via result_body().
    void start_to_memory(std::string url);

    void cancel();

    bool running() const;
    bool finished() const;   // completed (or failed) and not yet acknowledged
    DownloadProgress progress() const;

    // Valid once finished(). Calling this clears the finished flag.
    DownloadResult take_result();
    const std::string &result_body() const { return m_body; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::string m_body;
};

} // namespace swos
