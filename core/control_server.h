// プラグインの各インスタンスが開く操作サーバー(127.0.0.1 の TCP、1 行 1 JSON)。
// 起動中のインスタンスはレジストリ(~/.mps-plugins/instances/*.json)に書き出し、AI はそこからポートを探す。
#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

namespace mps {

class ControlServer {
public:
    using Handler = std::function<std::string(const std::string&)>;
    ~ControlServer();
    // 環境変数 MPS_CTL_PORT_BASE(既定 47800)から 100 個の範囲で空いているポートを使う。MPS_CTL_DISABLE=1 で無効
    bool start(Handler handler, const std::string& pluginId);
    void stop();
    int port() const { return port_; }
    // レジストリに載せる追加情報(トラック名など)を更新
    void setMeta(const nlohmann::json& meta);
    static std::string registryDir();

private:
    void run();
    void writeRegistry();
    Handler handler_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    long long listenFd_ = -1;
    int port_ = 0;
    std::string pluginId_, registryFile_;
    std::mutex metaMutex_;
    nlohmann::json meta_ = nlohmann::json::object();
};

}  // namespace mps
