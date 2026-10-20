// 状態(パラメータ値)の保持、状態 JSON、制御コマンド、オフライン書き出し。
// VST3 のプロセッサー・制御サーバー・mps_render CLI で共通に使う。
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "engine.h"

namespace mps {

using json = nlohmann::json;

// スレッド間で共有するパラメータ値(プレーン値)。dirty はホストへ変更を知らせる印
class ParamStore {
public:
    explicit ParamStore(const PluginSpec& spec);
    const PluginSpec& spec() const { return spec_; }
    int size() const { return n_; }
    double get(int i) const { return v_[i].load(std::memory_order_relaxed); }
    void set(int i, double plain, bool markDirty);
    bool takeDirty(int i) { return dirty_[i].exchange(false); }
    void resetDefaults(bool markDirty);
    // {"key": 数値 or ラベル}。不正なキーは errors に入れる
    void applyJson(const json& values, bool markDirty, std::vector<std::string>* errors);
    json valuesJson(bool onlyChanged = false) const;
    json stateJson() const;  // {"format":"mps-state", ...}
    bool loadState(const json& state, bool markDirty);
    bool loadPreset(const std::string& name, bool markDirty);
    std::string presetName() const;
    // インスタンスの名前(同じトラックに同じプラグインを複数挿したときに呼び分ける。状態に保存される)
    std::string instanceName() const;
    void setInstanceName(const std::string& n);
    static bool validInstanceName(const std::string& n, std::string& err);
    void setPresetName(const std::string& n);

private:
    const PluginSpec& spec_;
    int n_;
    std::unique_ptr<std::atomic<double>[]> v_;
    std::unique_ptr<std::atomic<bool>[]> dirty_;
    mutable std::mutex nameMutex_;
    std::string presetName_ = "Init";
    std::string instanceName_;
};

struct RenderNote {
    double t, dur;
    int note;
    double vel;
};

struct RenderRequest {
    std::vector<RenderNote> notes;
    double length = 0;  // 0 なら最後のノート + 2 秒(エフェクトは入力の長さ)
    double sampleRate = 48000;
    double bpm = 120;
    std::vector<float> inL, inR;  // エフェクトの入力
    std::vector<std::pair<double, json>> automation;  // (秒, {"key": 値}) を時刻順に
};

struct RenderResult {
    std::vector<float> L, R;
    double peak = 0, rms = 0;
};

RenderResult render(const PluginSpec& spec, const ParamStore& params, const RenderRequest& req);
bool writeWav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R, double sr, int bits = 24);
bool readWav(const std::string& path, std::vector<float>& L, std::vector<float>& R, double& sr);
// JSON(制御サーバー/CLI 共通の書式)から RenderRequest を作る
bool parseRenderRequest(const json& j, RenderRequest& req, std::string& err);

// 実機(プロセッサー)側でしかできない操作
struct LiveHooks {
    std::function<void(int note, double vel, double dur)> play;  // dur <= 0 ならノートオンのみ
    std::function<void(int note)> noteOff;
    std::function<void()> panic;
    std::function<json()> info;
};

// 1 行の JSON リクエストを処理して 1 行の JSON 応答を返す
std::string handleCommand(ParamStore& store, const std::string& line, const LiveHooks& hooks);
json commandHelp();

}  // namespace mps
