#ifndef BC_LICHESS_CLIENT_H
#define BC_LICHESS_CLIENT_H

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

/* Native Lichess HTTPS transport (libcurl); no original entry point. Ordinary requests
 * run one at a time on a worker thread, as the API asks; NDJSON streams (events, a
 * game, a seek) run on their own threads. Results queue up for the SDL thread, which
 * owns all game state, to collect with poll(). The token is sent only as a Bearer
 * header and never logged. */
struct LichessMessage {
    enum class Kind { response, line, closed };
    Kind kind = Kind::response;
    std::string tag;  // Request tag or stream name.
    int status = 0;   // HTTP status, 0 when the transfer failed.
    std::string body; // Response body, or one NDJSON line.
    std::string error;
};

class LichessClient {
  public:
    // retryPause: how long to hold the request queue after HTTP 429 (the API asks a minute).
    LichessClient(std::string baseUrl, std::string userAgent, int retryPauseMilliseconds = 60000);
    ~LichessClient();
    LichessClient(const LichessClient &) = delete;
    LichessClient &operator=(const LichessClient &) = delete;

    void setToken(std::string token);
    const std::string &baseUrl() const {
        return base;
    }

    // Queue a request; form is an application/x-www-form-urlencoded body for POST.
    void request(std::string tag, std::string method, std::string path, std::string form = "");
    // Open a named stream, replacing a stream of the same name.
    void openStream(const std::string &name, std::string method, std::string path,
                    std::string form = "");
    void closeStream(const std::string &name);
    bool streamOpen(const std::string &name) const;
    void closeAll();

    std::vector<LichessMessage> poll();

  private:
    struct Job {
        std::string tag, method, path, form;
    };
    struct Stream {
        std::string name;
        std::atomic<bool> cancel{false}, done{false};
        std::thread thread;
    };

    std::string base, agent, token;
    int retryPause;
    mutable std::mutex lock;
    std::condition_variable wake;
    std::deque<Job> jobs;
    std::vector<LichessMessage> results;
    std::vector<std::unique_ptr<Stream>> streams;
    bool stopping = false;
    std::thread worker;

    void post(LichessMessage message);
    void work();
    void runStream(Stream &stream, Job job, std::string bearer);
    void reap();
};

// Native token storage: macOS Keychain; elsewhere nothing is stored.
std::string loadLichessToken();
bool saveLichessToken(const std::string &token);
void forgetLichessToken();
bool lichessTokenStorageAvailable();

#endif
