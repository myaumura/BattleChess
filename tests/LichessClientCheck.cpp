#include "LichessClient.h"
#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

/* Local stand-in for lichess.org: enough HTTP/1.1 to exercise the client's headers,
 * request queue, rate-limit pause and NDJSON streams without a network or account. */
namespace {
    using Clock = std::chrono::steady_clock;
    int failures = 0;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::fprintf(stderr, "FAILED: %s\n", what);
            ++failures;
        }
    }

    std::string lower(std::string text) {
        for (char &c : text)
            if (c >= 'A' && c <= 'Z')
                c = char(c - 'A' + 'a');
        return text;
    }

    class FakeLichess {
      public:
        FakeLichess() {
            listener = socket(AF_INET, SOCK_STREAM, 0);
            int on = 1;
            setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0 ||
                listen(listener, 16) != 0) {
                std::perror("fake server");
                std::exit(EXIT_FAILURE);
            }
            socklen_t length = sizeof address;
            getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length);
            port = ntohs(address.sin_port);
            acceptor = std::thread([this] { acceptLoop(); });
        }

        ~FakeLichess() {
            stopping = true;
            shutdown(listener, SHUT_RDWR);
            close(listener);
            acceptor.join();
            for (auto &connection : connections)
                connection.join();
        }

        std::string url() const {
            return "http://127.0.0.1:" + std::to_string(port);
        }

        std::vector<std::string> bodies() {
            std::lock_guard<std::mutex> guard(lock);
            return recorded;
        }

        std::atomic<bool> streamClientGone{false};

      private:
        int listener = -1, port = 0;
        std::atomic<bool> stopping{false};
        std::thread acceptor;
        std::vector<std::thread> connections;
        std::mutex lock;
        std::vector<std::string> recorded;

        void acceptLoop() {
            for (;;) {
                int client = accept(listener, nullptr, nullptr);
                if (client < 0)
                    return;
                connections.emplace_back([this, client] { serve(client); });
            }
        }

        static bool sendAll(int client, const std::string &text) {
            size_t sent = 0;
            while (sent < text.size()) {
                ssize_t n = send(client, text.data() + sent, text.size() - sent, MSG_NOSIGNAL_FLAG);
                if (n <= 0)
                    return false;
                sent += size_t(n);
            }
            return true;
        }

#ifdef MSG_NOSIGNAL
        static constexpr int MSG_NOSIGNAL_FLAG = MSG_NOSIGNAL;
#else
        static constexpr int MSG_NOSIGNAL_FLAG = 0;
#endif

        static void respond(int client, int status, const std::string &body) {
            const char *reason = status == 200 ? "OK" : status == 401 ? "Unauthorized"
                                 : status == 429 ? "Too Many Requests" : "Not Found";
            sendAll(client, "HTTP/1.1 " + std::to_string(status) + " " + reason +
                                "\r\nContent-Type: application/json\r\nContent-Length: " +
                                std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" +
                                body);
        }

        void serve(int client) {
#ifdef SO_NOSIGPIPE
            int on = 1;
            setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on);
#endif
            std::string request;
            char buffer[4096];
            size_t headerEnd;
            while ((headerEnd = request.find("\r\n\r\n")) == std::string::npos) {
                ssize_t n = recv(client, buffer, sizeof buffer, 0);
                if (n <= 0) {
                    close(client);
                    return;
                }
                request.append(buffer, size_t(n));
            }
            std::string head = lower(request.substr(0, headerEnd));
            size_t length = 0;
            if (auto at = head.find("content-length:"); at != std::string::npos)
                length = std::strtoul(head.c_str() + at + 15, nullptr, 10);
            std::string body = request.substr(headerEnd + 4);
            while (body.size() < length) {
                ssize_t n = recv(client, buffer, sizeof buffer, 0);
                if (n <= 0)
                    break;
                body.append(buffer, size_t(n));
            }
            std::string line = request.substr(0, request.find("\r\n"));
            std::string path = line.substr(line.find(' ') + 1);
            path = path.substr(0, path.find(' '));
            bool agent = head.find("user-agent: battlechess-test/1.0") != std::string::npos;
            bool authorized = head.find("authorization: bearer good_token") != std::string::npos;
            {
                std::lock_guard<std::mutex> guard(lock);
                recorded.push_back(path + " " + body);
            }

            if (path == "/api/account") {
                if (authorized && agent)
                    respond(client, 200, R"({"id":"tester","username":"Tester"})");
                else
                    respond(client, 401, R"({"error":"No such token"})");
            } else if (path == "/api/import") {
                respond(client, 200, R"({"id":"imp12345","url":"https://lichess.org/imp12345"})");
            } else if (path == "/api/board/game/g1/move/e2e4") {
                respond(client, authorized ? 200 : 401, R"({"ok":true})");
            } else if (path == "/api/rate") {
                respond(client, 429, R"({"error":"Too many requests"})");
            } else if (path == "/api/finite") {
                sendAll(client, "HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\n"
                                "Connection: close\r\n\r\n{\"n\":1}\n\n{\"n\":2}\n");
            } else if (path == "/api/stream/event") {
                // Keep-alives around one event, then hold the connection open.
                bool open = sendAll(client, "HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson"
                                            "\r\nConnection: close\r\n\r\n\n");
                open = open && sendAll(client, "{\"type\":\"gameStart\",\"game\":{\"gameId\":"
                                               "\"g1\",\"color\":\"white\"}}\n\n");
                while (open && !stopping) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    open = sendAll(client, "\n");
                }
                if (!open)
                    streamClientGone = true;
            } else {
                respond(client, 404, R"({"error":"Not found"})");
            }
            close(client);
        }
    };

    template <class Done>
    std::vector<LichessMessage> collect(LichessClient &client, Done done, int seconds = 10) {
        std::vector<LichessMessage> all;
        auto deadline = Clock::now() + std::chrono::seconds(seconds);
        while (!done(all) && Clock::now() < deadline) {
            for (auto &message : client.poll())
                all.push_back(std::move(message));
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return all;
    }
}

int main() {
    FakeLichess server;
    {
        LichessClient client(server.url(), "BattleChess-test/1.0", 300);
        client.setToken("good_token");

        // Requests run one at a time, in order; 429 holds the queue before the next one.
        client.request("account", "GET", "/api/account");
        client.request("import", "POST", "/api/import", "pgn=1.+e4+%2A");
        client.request("move", "POST", "/api/board/game/g1/move/e2e4");
        client.request("rate", "GET", "/api/rate");
        client.request("after", "GET", "/api/account");
        Clock::time_point rateAt{}, afterAt{};
        std::vector<LichessMessage> responses;
        auto deadline = Clock::now() + std::chrono::seconds(10);
        while (responses.size() < 5 && Clock::now() < deadline) {
            for (auto &message : client.poll()) {
                if (message.tag == "rate")
                    rateAt = Clock::now();
                if (message.tag == "after")
                    afterAt = Clock::now();
                responses.push_back(std::move(message));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        expect(responses.size() == 5, "all queued requests answered");
        if (responses.size() == 5) {
            expect(responses[0].tag == "account" && responses[1].tag == "import" &&
                       responses[2].tag == "move" && responses[3].tag == "rate" &&
                       responses[4].tag == "after",
                   "responses keep request order");
            expect(responses[0].status == 200 &&
                       responses[0].body.find("\"username\":\"Tester\"") != std::string::npos,
                   "Bearer token and User-Agent sent");
            expect(responses[1].status == 200 && responses[2].status == 200, "POST requests");
            expect(responses[3].status == 429, "rate limit reported");
            expect(afterAt - rateAt >= std::chrono::milliseconds(250),
                   "queue pauses after HTTP 429");
        }
        auto bodies = server.bodies();
        bool importBody = false;
        for (const auto &body : bodies)
            importBody |= body == "/api/import pgn=1.+e4+%2A";
        expect(importBody, "form body delivered");

        client.setToken("bad_token");
        client.request("bad", "GET", "/api/account");
        auto bad = collect(client, [](auto &all) { return !all.empty(); });
        expect(bad.size() == 1 && bad[0].status == 401, "rejected token reported");

        // Streams: keep-alive blank lines are skipped; closing cancels without a message.
        client.setToken("good_token");
        client.openStream("events", "GET", "/api/stream/event");
        auto events = collect(client, [](auto &all) { return !all.empty(); });
        expect(events.size() == 1 && events[0].kind == LichessMessage::Kind::line &&
                   events[0].tag == "events" && events[0].status == 200 &&
                   events[0].body.find("gameStart") != std::string::npos,
               "stream delivers one event line");
        expect(client.streamOpen("events"), "stream reported open");
        client.closeStream("events");
        expect(!client.streamOpen("events"), "closed stream reported closed");
        auto quiet = collect(client, [&](auto &) { return server.streamClientGone.load(); }, 5);
        expect(server.streamClientGone, "server sees the stream disconnect");
        expect(quiet.empty(), "cancelled stream posts nothing");

        client.openStream("finite", "GET", "/api/finite");
        auto finite = collect(client, [](auto &all) {
            return !all.empty() && all.back().kind == LichessMessage::Kind::closed;
        });
        expect(finite.size() == 3 && finite[0].body == "{\"n\":1}" && finite[1].body == "{\"n\":2}" &&
                   finite[2].kind == LichessMessage::Kind::closed && finite[2].status == 200 &&
                   finite[2].error.empty(),
               "server-closed stream reports its end");

        // Destroying the client with a live stream must not hang.
        client.openStream("events", "GET", "/api/stream/event");
        collect(client, [](auto &all) { return !all.empty(); });
    }

    LichessClient offline("http://127.0.0.1:1", "BattleChess-test/1.0", 10);
    offline.request("down", "GET", "/api/account");
    auto down = collect(offline, [](auto &all) { return !all.empty(); });
    expect(down.size() == 1 && down[0].status == 0 && !down[0].error.empty(),
           "unreachable server reported");

    if (failures)
        return EXIT_FAILURE;
    std::puts("Lichess client checks passed");
    return EXIT_SUCCESS;
}
