#include "LichessClient.h"
#include <chrono>
#include <functional>
#include <curl/curl.h>

namespace {
    struct CurlGlobal {
        CurlGlobal() {
            curl_global_init(CURL_GLOBAL_DEFAULT);
        }
        ~CurlGlobal() {
            curl_global_cleanup();
        }
    };

    void ensureCurl() {
        static CurlGlobal global;
    }

    size_t collect(char *data, size_t size, size_t count, void *user) {
        static_cast<std::string *>(user)->append(data, size * count);
        return size * count;
    }

    struct LineSink {
        std::string pending;
        std::function<void(std::string)> deliver;
    };

    size_t splitLines(char *data, size_t size, size_t count, void *user) {
        auto &sink = *static_cast<LineSink *>(user);
        sink.pending.append(data, size * count);
        size_t newline;
        while ((newline = sink.pending.find('\n')) != std::string::npos) {
            std::string line = sink.pending.substr(0, newline);
            sink.pending.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (!line.empty()) // Blank lines are keep-alives.
                sink.deliver(std::move(line));
        }
        return size * count;
    }

    int abortWhenCancelled(void *user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
        return static_cast<std::atomic<bool> *>(user)->load() ? 1 : 0;
    }

    struct Easy {
        CURL *handle = curl_easy_init();
        curl_slist *headers = nullptr;
        ~Easy() {
            curl_slist_free_all(headers);
            curl_easy_cleanup(handle);
        }
    };

    void configure(Easy &easy, const std::string &url, const std::string &method,
                   const std::string &form, const std::string &agent, const std::string &bearer) {
        curl_easy_setopt(easy.handle, CURLOPT_URL, url.c_str());
        curl_easy_setopt(easy.handle, CURLOPT_USERAGENT, agent.c_str());
        curl_easy_setopt(easy.handle, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(easy.handle, CURLOPT_CONNECTTIMEOUT, 15L);
        curl_easy_setopt(easy.handle, CURLOPT_TCP_KEEPALIVE, 1L);
        curl_easy_setopt(easy.handle, CURLOPT_PROTOCOLS_STR, "https,http");
        if (!bearer.empty())
            easy.headers = curl_slist_append(easy.headers, ("Authorization: Bearer " + bearer).c_str());
        easy.headers = curl_slist_append(easy.headers, "Accept: application/json, application/x-ndjson");
        curl_easy_setopt(easy.handle, CURLOPT_HTTPHEADER, easy.headers);
        if (method == "POST") {
            curl_easy_setopt(easy.handle, CURLOPT_POST, 1L);
            curl_easy_setopt(easy.handle, CURLOPT_POSTFIELDSIZE, long(form.size()));
            curl_easy_setopt(easy.handle, CURLOPT_COPYPOSTFIELDS, form.c_str());
        }
    }

    std::string transferError(CURLcode code, const char *detail) {
        std::string message = curl_easy_strerror(code);
        if (detail && *detail)
            message += std::string(": ") + detail;
        return message;
    }
}

LichessClient::LichessClient(std::string baseUrl, std::string userAgent, int retryPauseMs)
    : base(std::move(baseUrl)), agent(std::move(userAgent)), retryPause(retryPauseMs) {
    ensureCurl();
    while (!base.empty() && base.back() == '/')
        base.pop_back();
    worker = std::thread([this] { work(); });
}

LichessClient::~LichessClient() {
    {
        std::lock_guard<std::mutex> guard(lock);
        stopping = true;
        jobs.clear();
    }
    wake.notify_all();
    closeAll();
    worker.join();
    std::vector<std::unique_ptr<Stream>> remaining;
    {
        std::lock_guard<std::mutex> guard(lock);
        remaining.swap(streams);
    }
    for (auto &stream : remaining)
        if (stream->thread.joinable())
            stream->thread.join();
}

void LichessClient::setToken(std::string value) {
    std::lock_guard<std::mutex> guard(lock);
    token = std::move(value);
}

void LichessClient::post(LichessMessage message) {
    std::lock_guard<std::mutex> guard(lock);
    results.push_back(std::move(message));
}

void LichessClient::request(std::string tag, std::string method, std::string path,
                            std::string form) {
    {
        std::lock_guard<std::mutex> guard(lock);
        jobs.push_back({std::move(tag), std::move(method), std::move(path), std::move(form)});
    }
    wake.notify_all();
}

void LichessClient::work() {
    for (;;) {
        Job job;
        std::string bearer;
        {
            std::unique_lock<std::mutex> guard(lock);
            wake.wait(guard, [this] { return stopping || !jobs.empty(); });
            if (stopping)
                return;
            job = std::move(jobs.front());
            jobs.pop_front();
            bearer = token;
        }
        Easy easy;
        std::string body;
        char detail[CURL_ERROR_SIZE] = {};
        configure(easy, base + job.path, job.method, job.form, agent, bearer);
        curl_easy_setopt(easy.handle, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(easy.handle, CURLOPT_WRITEFUNCTION, collect);
        curl_easy_setopt(easy.handle, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(easy.handle, CURLOPT_ERRORBUFFER, detail);
        CURLcode code = curl_easy_perform(easy.handle);
        LichessMessage message;
        message.tag = job.tag;
        message.body = std::move(body);
        if (code == CURLE_OK) {
            long status = 0;
            curl_easy_getinfo(easy.handle, CURLINFO_RESPONSE_CODE, &status);
            message.status = int(status);
        } else {
            message.error = transferError(code, detail);
        }
        bool limited = message.status == 429;
        post(std::move(message));
        if (limited) {
            // Rate limited: hold every further request, as the API asks.
            std::unique_lock<std::mutex> guard(lock);
            wake.wait_for(guard, std::chrono::milliseconds(retryPause), [this] { return stopping; });
            if (stopping)
                return;
        }
    }
}

void LichessClient::openStream(const std::string &name, std::string method, std::string path,
                               std::string form) {
    closeStream(name);
    auto stream = std::make_unique<Stream>();
    stream->name = name;
    std::string bearer;
    {
        std::lock_guard<std::mutex> guard(lock);
        bearer = token;
    }
    Stream &ref = *stream;
    Job job{name, std::move(method), std::move(path), std::move(form)};
    stream->thread = std::thread([this, &ref, job, bearer] { runStream(ref, job, bearer); });
    std::lock_guard<std::mutex> guard(lock);
    streams.push_back(std::move(stream));
}

void LichessClient::runStream(Stream &stream, Job job, std::string bearer) {
    Easy easy;
    LineSink sink;
    sink.deliver = [&](std::string line) {
        if (stream.cancel)
            return;
        LichessMessage message;
        message.kind = LichessMessage::Kind::line;
        message.tag = stream.name;
        message.body = std::move(line);
        long status = 0;
        curl_easy_getinfo(easy.handle, CURLINFO_RESPONSE_CODE, &status);
        message.status = int(status);
        post(std::move(message));
    };
    char detail[CURL_ERROR_SIZE] = {};
    configure(easy, base + job.path, job.method, job.form, agent, bearer);
    curl_easy_setopt(easy.handle, CURLOPT_WRITEFUNCTION, splitLines);
    curl_easy_setopt(easy.handle, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(easy.handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(easy.handle, CURLOPT_XFERINFOFUNCTION, abortWhenCancelled);
    curl_easy_setopt(easy.handle, CURLOPT_XFERINFODATA, &stream.cancel);
    curl_easy_setopt(easy.handle, CURLOPT_ERRORBUFFER, detail);
    CURLcode code = curl_easy_perform(easy.handle);
    if (!sink.pending.empty() && !stream.cancel)
        sink.deliver(std::move(sink.pending));
    if (!stream.cancel) {
        LichessMessage message;
        message.kind = LichessMessage::Kind::closed;
        message.tag = stream.name;
        long status = 0;
        curl_easy_getinfo(easy.handle, CURLINFO_RESPONSE_CODE, &status);
        message.status = int(status);
        if (code != CURLE_OK)
            message.error = transferError(code, detail);
        post(std::move(message));
    }
    stream.done = true;
}

void LichessClient::closeStream(const std::string &name) {
    std::lock_guard<std::mutex> guard(lock);
    for (auto &stream : streams)
        if (stream->name == name)
            stream->cancel = true;
}

void LichessClient::closeAll() {
    std::lock_guard<std::mutex> guard(lock);
    for (auto &stream : streams)
        stream->cancel = true;
}

bool LichessClient::streamOpen(const std::string &name) const {
    std::lock_guard<std::mutex> guard(lock);
    for (const auto &stream : streams)
        if (stream->name == name && !stream->cancel && !stream->done)
            return true;
    return false;
}

// Join finished stream threads without blocking on live ones.
void LichessClient::reap() {
    std::vector<std::unique_ptr<Stream>> finished;
    {
        std::lock_guard<std::mutex> guard(lock);
        for (auto it = streams.begin(); it != streams.end();)
            if ((*it)->done) {
                finished.push_back(std::move(*it));
                it = streams.erase(it);
            } else {
                ++it;
            }
    }
    for (auto &stream : finished)
        stream->thread.join();
}

std::vector<LichessMessage> LichessClient::poll() {
    reap();
    std::lock_guard<std::mutex> guard(lock);
    std::vector<LichessMessage> out;
    out.swap(results);
    return out;
}
