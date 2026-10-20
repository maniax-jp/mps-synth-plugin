// プラグイン本体(音源/エフェクト)の共通インタフェースと、プラグインごとの仕様。
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "params.h"

namespace mps {

struct Transport {
    double bpm = 120.0;
    double ppq = 0.0;     // ブロック先頭の位置(4 分音符単位)
    bool playing = false;
};

class Engine {
public:
    virtual ~Engine() = default;
    virtual void prepare(double sampleRate, int maxBlock) = 0;
    virtual void setParam(int id, double plain) = 0;
    virtual void noteOn(int note, double velocity) = 0;
    virtual void noteOff(int note) = 0;
    virtual void allNotesOff() = 0;
    // in はエフェクトのみ使う(音源では nullptr)。out は 2ch
    virtual void process(const float* const* in, float* const* out, int n, const Transport& tr) = 0;
};

struct Preset {
    const char* name;
    const char* desc;    // 何の音か・鳴らす前提の音程
    const char* values;  // 状態と同じ JSON(省略したキーは既定値)
};

struct PluginSpec {
    const char* name;     // "MPS Synth"
    const char* id;       // "synth" / "fx"
    bool instrument;
    const std::vector<ParamDef>* params;
    const std::vector<Preset>* presets;
    std::unique_ptr<Engine> (*create)();
    int find(const std::string& key) const {
        for (size_t i = 0; i < params->size(); ++i)
            if (key == (*params)[i].key) return static_cast<int>(i);
        return -1;
    }
};

const PluginSpec& synthSpec();
const PluginSpec& fxSpec();
const PluginSpec* specById(const std::string& id);

constexpr const char* kVersion = "0.1.0";

}  // namespace mps
