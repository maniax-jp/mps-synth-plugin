#include "control_server.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
#pragma comment(lib, "ws2_32.lib")
using sock_t = SOCKET;
#define MPS_CLOSE closesocket
#define MPS_POLL WSAPoll
static int getPid() { return _getpid(); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
using sock_t = int;
#define MPS_CLOSE ::close
#define MPS_POLL ::poll
static int getPid() { return static_cast<int>(::getpid()); }
#endif
#ifdef MSG_NOSIGNAL
#define MPS_SEND_FLAGS MSG_NOSIGNAL
#else
#define MPS_SEND_FLAGS 0
#endif

namespace fs = std::filesystem;

namespace mps {

ControlServer::~ControlServer() { stop(); }

std::string ControlServer::registryDir() {
    if (const char* d = std::getenv("MPS_CTL_REGISTRY")) return d;
#ifdef _WIN32
    const char* base = std::getenv("LOCALAPPDATA");
    return std::string(base ? base : ".") + "\\mps-plugins\\instances";
#else
    const char* home = std::getenv("HOME");
    return std::string(home ? home : "/tmp") + "/.mps-plugins/instances";
#endif
}

bool ControlServer::start(Handler handler, const std::string& pluginId) {
    if (running_) return true;
    if (const char* dis = std::getenv("MPS_CTL_DISABLE"))
        if (std::string(dis) == "1") return false;
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
#endif
    handler_ = std::move(handler);
    pluginId_ = pluginId;
    int base = 47800;
    if (const char* b = std::getenv("MPS_CTL_PORT_BASE")) base = std::atoi(b);
    sock_t fd = socket(AF_INET, SOCK_STREAM, 0);
#ifdef _WIN32
    if (fd == INVALID_SOCKET) return false;
#else
    if (fd < 0) return false;
#endif
    // 同じポートを 2 つのインスタンスが取らないよう SO_REUSEADDR は使わない
    for (int p = base; p < base + 100; ++p) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(static_cast<unsigned short>(p));
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) == 0) {
            port_ = p;
            break;
        }
    }
    if (!port_ || listen(fd, 4) != 0) {
        MPS_CLOSE(fd);
        port_ = 0;
        return false;
    }
    listenFd_ = static_cast<long long>(fd);
    running_ = true;
    writeRegistry();
    thread_ = std::thread([this] { run(); });
    return true;
}

void ControlServer::stop() {
    if (!running_) return;
    running_ = false;
    if (thread_.joinable()) thread_.join();
    MPS_CLOSE(static_cast<sock_t>(listenFd_));
    listenFd_ = -1;
    std::error_code ec;
    if (!registryFile_.empty()) fs::remove(registryFile_, ec);
#ifdef _WIN32
    WSACleanup();
#endif
}

void ControlServer::setMeta(const nlohmann::json& meta) {
    {
        std::lock_guard<std::mutex> lk(metaMutex_);
        for (auto it = meta.begin(); it != meta.end(); ++it) meta_[it.key()] = it.value();
    }
    if (running_) writeRegistry();
}

void ControlServer::writeRegistry() {
    std::error_code ec;
    fs::create_directories(registryDir(), ec);
    registryFile_ = (fs::path(registryDir()) / (pluginId_ + "-" + std::to_string(getPid()) + "-" + std::to_string(port_) + ".json")).string();
    nlohmann::json j;
    {
        std::lock_guard<std::mutex> lk(metaMutex_);
        j = meta_;
    }
    j["plugin_id"] = pluginId_;
    j["port"] = port_;
    j["host"] = "127.0.0.1";
    j["pid"] = getPid();
    j["started"] = static_cast<long long>(std::time(nullptr));
    std::string tmp = registryFile_ + ".tmp";
    {
        std::ofstream f(tmp);
        f << j.dump(2);
    }
    fs::rename(tmp, registryFile_, ec);
}

void ControlServer::run() {
    sock_t lfd = static_cast<sock_t>(listenFd_);
    while (running_) {
        pollfd pf{};
        pf.fd = lfd;
        pf.events = POLLIN;
        if (MPS_POLL(&pf, 1, 200) <= 0) continue;
        sock_t c = accept(lfd, nullptr, nullptr);
#ifdef _WIN32
        if (c == INVALID_SOCKET) continue;
#else
        if (c < 0) continue;
#endif
#ifdef SO_NOSIGPIPE
        int one = 1;
        setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
        // 1 クライアントずつ。行ごとに応答し、切断されるまで続ける
        std::string buf;
        bool open = true;
        auto lastActivity = std::chrono::steady_clock::now();
        while (running_ && open) {
            pollfd cp{};
            cp.fd = c;
            cp.events = POLLIN;
            int r = MPS_POLL(&cp, 1, 200);
            if (r <= 0) {
                // 何も送ってこない接続が次のクライアントを塞がないように 30 秒で切る
                if (std::chrono::steady_clock::now() - lastActivity > std::chrono::seconds(30)) break;
                continue;
            }
            char tmp[4096];
            int n = static_cast<int>(recv(c, tmp, sizeof tmp, 0));
            if (n <= 0) break;
            lastActivity = std::chrono::steady_clock::now();
            buf.append(tmp, n);
            if (buf.size() > (16u << 20)) break;
            size_t nl;
            while ((nl = buf.find('\n')) != std::string::npos) {
                std::string line = buf.substr(0, nl);
                buf.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                std::string out = handler_(line) + "\n";
                size_t sent = 0;
                while (sent < out.size()) {
                    int s = static_cast<int>(send(c, out.data() + sent, static_cast<int>(out.size() - sent), MPS_SEND_FLAGS));
                    if (s <= 0) { open = false; break; }
                    sent += s;
                }
            }
        }
        MPS_CLOSE(c);
    }
}

}  // namespace mps
