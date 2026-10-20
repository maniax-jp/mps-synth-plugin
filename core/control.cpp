#include "control.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace mps {

// ============================================================================ ParamStore
ParamStore::ParamStore(const PluginSpec& spec)
    : spec_(spec), n_(static_cast<int>(spec.params->size())),
      v_(new std::atomic<double>[spec.params->size()]), dirty_(new std::atomic<bool>[spec.params->size()]) {
    for (int i = 0; i < n_; ++i) {
        v_[i].store((*spec.params)[i].def);
        dirty_[i].store(false);
    }
}

void ParamStore::set(int i, double plain, bool markDirty) {
    if (i < 0 || i >= n_) return;
    double v = clampPlain((*spec_.params)[i], plain);
    double old = v_[i].exchange(v, std::memory_order_relaxed);
    if (markDirty && old != v) dirty_[i].store(true);
}

void ParamStore::resetDefaults(bool markDirty) {
    for (int i = 0; i < n_; ++i) set(i, (*spec_.params)[i].def, markDirty);
}

void ParamStore::applyJson(const json& values, bool markDirty, std::vector<std::string>* errors) {
    if (!values.is_object()) {
        if (errors) errors->push_back("values must be an object");
        return;
    }
    for (auto it = values.begin(); it != values.end(); ++it) {
        int id = spec_.find(it.key());
        if (id < 0) {
            if (errors) errors->push_back("unknown param: " + it.key());
            continue;
        }
        const ParamDef& d = (*spec_.params)[id];
        double plain;
        if (it->is_number()) plain = it->get<double>();
        else if (it->is_boolean()) plain = it->get<bool>() ? 1 : 0;
        else if (it->is_string()) {
            if (!parseValue(d, it->get<std::string>(), plain)) {
                if (errors) errors->push_back("bad value for " + it.key() + ": " + it->get<std::string>());
                continue;
            }
        } else {
            if (errors) errors->push_back("bad value for " + it.key());
            continue;
        }
        set(id, plain, markDirty);
    }
}

json ParamStore::valuesJson(bool onlyChanged) const {
    json j = json::object();
    for (int i = 0; i < n_; ++i) {
        const ParamDef& d = (*spec_.params)[i];
        double v = get(i);
        if (onlyChanged && std::fabs(v - d.def) < 1e-9) continue;
        if (d.scale == Scale::Enum || d.scale == Scale::Bool) j[d.key] = formatValue(d, v);
        else if (d.scale == Scale::Int) j[d.key] = static_cast<int>(std::lround(v));
        else j[d.key] = v;
    }
    return j;
}

json ParamStore::stateJson() const {
    return json{{"format", "mps-state"}, {"version", 1}, {"plugin", spec_.id}, {"preset", presetName()},
                {"params", valuesJson(false)}};
}

bool ParamStore::loadState(const json& state, bool markDirty) {
    if (!state.is_object()) return false;
    const json* params = &state;
    if (state.contains("params")) params = &state["params"];
    resetDefaults(markDirty);
    applyJson(*params, markDirty, nullptr);
    if (state.contains("preset") && state["preset"].is_string()) setPresetName(state["preset"].get<std::string>());
    return true;
}

bool ParamStore::loadPreset(const std::string& name, bool markDirty) {
    for (const auto& p : *spec_.presets) {
        if (name == p.name) {
            resetDefaults(markDirty);
            applyJson(json::parse(p.values), markDirty, nullptr);
            setPresetName(p.name);
            return true;
        }
    }
    return false;
}

std::string ParamStore::presetName() const {
    std::lock_guard<std::mutex> lk(nameMutex_);
    return presetName_;
}
void ParamStore::setPresetName(const std::string& n) {
    std::lock_guard<std::mutex> lk(nameMutex_);
    presetName_ = n;
}

// ============================================================================ WAV
static void put16(std::ofstream& f, uint16_t v) { f.put(static_cast<char>(v & 255)); f.put(static_cast<char>(v >> 8)); }
static void put32(std::ofstream& f, uint32_t v) { for (int i = 0; i < 4; ++i) f.put(static_cast<char>((v >> (8 * i)) & 255)); }

bool writeWav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R, double sr, int bits) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const bool fl = bits == 32;
    const uint32_t n = static_cast<uint32_t>(L.size()), bps = fl ? 4 : 3, data = n * 2 * bps;
    f.write("RIFF", 4); put32(f, 36 + data); f.write("WAVE", 4);
    f.write("fmt ", 4); put32(f, 16); put16(f, fl ? 3 : 1); put16(f, 2);
    put32(f, static_cast<uint32_t>(sr)); put32(f, static_cast<uint32_t>(sr) * 2 * bps); put16(f, static_cast<uint16_t>(2 * bps));
    put16(f, static_cast<uint16_t>(bps * 8));
    f.write("data", 4); put32(f, data);
    for (uint32_t i = 0; i < n; ++i) {
        for (float s : {L[i], R[i]}) {
            if (fl) {
                f.write(reinterpret_cast<const char*>(&s), 4);
            } else {
                double c = std::max(-1.0, std::min(1.0, static_cast<double>(s)));
                int32_t v = static_cast<int32_t>(std::lround(c * 8388607.0));
                f.put(static_cast<char>(v & 255)); f.put(static_cast<char>((v >> 8) & 255)); f.put(static_cast<char>((v >> 16) & 255));
            }
        }
    }
    return static_cast<bool>(f);
}

bool readWav(const std::string& path, std::vector<float>& L, std::vector<float>& R, double& sr) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::vector<char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(b.data() + 8, "WAVE", 4)) return false;
    auto u16 = [&](size_t o) { return static_cast<uint16_t>(static_cast<uint8_t>(b[o]) | (static_cast<uint8_t>(b[o + 1]) << 8)); };
    auto u32 = [&](size_t o) { return static_cast<uint32_t>(u16(o) | (static_cast<uint32_t>(u16(o + 2)) << 16)); };
    int fmt = 0, ch = 0, bits = 0;
    size_t pos = 12;
    while (pos + 8 <= b.size()) {
        uint32_t sz = u32(pos + 4);
        if (!std::memcmp(b.data() + pos, "fmt ", 4)) {
            fmt = u16(pos + 8); ch = u16(pos + 10); sr = u32(pos + 12); bits = u16(pos + 22);
            if (fmt == 0xFFFE && sz >= 26) fmt = u16(pos + 32);
        } else if (!std::memcmp(b.data() + pos, "data", 4)) {
            if (!ch || !bits) return false;
            size_t bpsamp = bits / 8, frames = std::min<size_t>(sz, b.size() - pos - 8) / (bpsamp * ch);
            L.resize(frames); R.resize(frames);
            const char* d = b.data() + pos + 8;
            for (size_t i = 0; i < frames; ++i) {
                float v[2] = {0, 0};
                for (int c = 0; c < std::min(ch, 2); ++c) {
                    const char* s = d + (i * ch + c) * bpsamp;
                    if (fmt == 3 && bits == 32) std::memcpy(&v[c], s, 4);
                    else if (bits == 16) v[c] = static_cast<int16_t>(static_cast<uint8_t>(s[0]) | (static_cast<uint8_t>(s[1]) << 8)) / 32768.f;
                    else if (bits == 24) {
                        int32_t x = static_cast<uint8_t>(s[0]) | (static_cast<uint8_t>(s[1]) << 8) | (static_cast<uint8_t>(s[2]) << 16);
                        if (x & 0x800000) x -= 0x1000000;
                        v[c] = x / 8388608.f;
                    } else if (bits == 32) {
                        int32_t x;
                        std::memcpy(&x, s, 4);
                        v[c] = static_cast<float>(x / 2147483648.0);
                    } else return false;
                }
                L[i] = v[0];
                R[i] = ch == 1 ? v[0] : v[1];
            }
            return true;
        }
        pos += 8 + sz + (sz & 1);
    }
    return false;
}

// ============================================================================ オフライン書き出し
bool parseRenderRequest(const json& j, RenderRequest& req, std::string& err) {
    req.sampleRate = j.value("sample_rate", 48000.0);
    req.bpm = j.value("bpm", 120.0);
    req.length = j.value("length", 0.0);
    const bool beats = j.value("unit", std::string("seconds")) == "beats";
    const double k = beats ? 60.0 / req.bpm : 1.0;
    if (beats) req.length *= k;
    if (j.contains("notes")) {
        for (const auto& n : j["notes"]) {
            RenderNote rn{};
            if (n.is_array() && n.size() >= 3) {
                rn.t = n[0].get<double>() * k;
                rn.dur = n[1].get<double>() * k;
                rn.note = n[2].get<int>();
                rn.vel = n.size() >= 4 ? n[3].get<double>() : 1.0;
            } else if (n.is_object()) {
                rn.t = n.value("t", 0.0) * k;
                rn.dur = n.value("dur", 0.5) * k;
                rn.note = n.value("note", 60);
                rn.vel = n.value("vel", 1.0);
            } else {
                err = "notes must be [t, dur, note, vel] or {t, dur, note, vel}";
                return false;
            }
            if (rn.vel > 1.0) rn.vel /= 127.0;
            req.notes.push_back(rn);
        }
    }
    if (j.contains("automation")) {
        for (const auto& a : j["automation"]) {
            if (!a.is_array() || a.size() != 2 || !a[1].is_object()) {
                err = "automation must be [[t, {key: value}], ...]";
                return false;
            }
            req.automation.emplace_back(a[0].get<double>() * k, a[1]);
        }
        std::stable_sort(req.automation.begin(), req.automation.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
    }
    if (j.contains("input")) {
        double sr = 0;
        if (!readWav(j["input"].get<std::string>(), req.inL, req.inR, sr)) {
            err = "cannot read input wav: " + j["input"].get<std::string>();
            return false;
        }
        if (!j.contains("sample_rate")) req.sampleRate = sr;
    }
    return true;
}

RenderResult render(const PluginSpec& spec, const ParamStore& params, const RenderRequest& req) {
    RenderResult res;
    const double sr = req.sampleRate;
    double len = req.length;
    if (len <= 0) {
        if (!req.inL.empty()) len = req.inL.size() / sr;
        else {
            len = 1.0;
            for (const auto& n : req.notes) len = std::max(len, n.t + n.dur + 2.0);
        }
    }
    const size_t total = static_cast<size_t>(len * sr);
    res.L.assign(total, 0.f);
    res.R.assign(total, 0.f);

    auto eng = spec.create();
    eng->prepare(sr, 64);
    ParamStore local(spec);
    for (int i = 0; i < params.size(); ++i) {
        local.set(i, params.get(i), false);
        eng->setParam(i, params.get(i));
    }
    struct Ev { size_t pos; int kind; int note; double vel; size_t autoIdx; };  // kind: 0 off, 1 on, 2 automation
    std::vector<Ev> ev;
    for (const auto& n : req.notes) {
        ev.push_back({static_cast<size_t>(std::max(0.0, n.t) * sr), 1, n.note, n.vel, 0});
        ev.push_back({static_cast<size_t>(std::max(0.0, n.t + n.dur) * sr), 0, n.note, 0, 0});
    }
    for (size_t a = 0; a < req.automation.size(); ++a)
        ev.push_back({static_cast<size_t>(std::max(0.0, req.automation[a].first) * sr), 2, 0, 0, a});
    // 同じ時刻ではノートオフ → オートメーション → ノートオン
    std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) {
        static const int order[3] = {0, 2, 1};
        return a.pos != b.pos ? a.pos < b.pos : order[a.kind] < order[b.kind];
    });

    std::vector<float> zero(64, 0.f), bl(64), br(64);
    size_t pos = 0, e = 0;
    while (pos < total) {
        while (e < ev.size() && ev[e].pos <= pos) {
            const Ev& x = ev[e++];
            if (x.kind == 1) eng->noteOn(x.note, x.vel);
            else if (x.kind == 0) eng->noteOff(x.note);
            else {
                local.applyJson(req.automation[x.autoIdx].second, false, nullptr);
                for (int i = 0; i < local.size(); ++i) eng->setParam(i, local.get(i));
            }
        }
        size_t n = std::min<size_t>(64, total - pos);
        if (e < ev.size()) n = std::min(n, std::max<size_t>(1, ev[e].pos - pos));
        Transport tr;
        tr.bpm = req.bpm;
        tr.ppq = pos / sr * req.bpm / 60.0;
        tr.playing = true;
        const float* in[2] = {zero.data(), zero.data()};
        if (!req.inL.empty()) {
            for (size_t i = 0; i < n; ++i) {
                size_t k = pos + i;
                bl[i] = k < req.inL.size() ? req.inL[k] : 0.f;
                br[i] = k < req.inR.size() ? req.inR[k] : 0.f;
            }
            in[0] = bl.data();
            in[1] = br.data();
        }
        float* out[2] = {res.L.data() + pos, res.R.data() + pos};
        eng->process(in, out, static_cast<int>(n), tr);
        pos += n;
    }
    double sq = 0;
    for (size_t i = 0; i < total; ++i) {
        res.peak = std::max({res.peak, std::fabs(static_cast<double>(res.L[i])), std::fabs(static_cast<double>(res.R[i]))});
        sq += 0.5 * (res.L[i] * res.L[i] + res.R[i] * res.R[i]);
    }
    res.rms = total ? std::sqrt(sq / total) : 0;
    return res;
}

// ============================================================================ 制御コマンド
json commandHelp() {
    return json::array({
        {{"cmd", "help"}, {"desc", "コマンド一覧"}},
        {{"cmd", "info"}, {"desc", "プラグイン名・ポート・トラック名・サンプルレート・テンポ・プリセット名"}},
        {{"cmd", "list_params"}, {"desc", "全パラメータの定義と現在値(key, name, unit, scale, min, max, default, value, labels, help)"}},
        {{"cmd", "get"}, {"args", "keys?: [key]"}, {"desc", "現在値(プレーン値。enum/bool はラベル)。changed_only: true で既定値から変えたものだけ"}},
        {{"cmd", "set"}, {"args", "params: {key: 値 or ラベル}, normalized?: bool"}, {"desc", "値を変えてホストに通知(DAW の表示と自動化記録にも反映)"}},
        {{"cmd", "list_presets"}, {"desc", "ファクトリープリセット(name, desc)"}},
        {{"cmd", "load_preset"}, {"args", "name, params?: {key: 値}"}, {"desc", "プリセットを読み込み、params で上書き"}},
        {{"cmd", "get_state"}, {"desc", "状態 JSON(DAW のプロジェクトに保存されるものと同じ)"}},
        {{"cmd", "set_state"}, {"args", "state"}, {"desc", "状態 JSON を読み込む(書いていないキーは既定値)"}},
        {{"cmd", "reset"}, {"desc", "全パラメータを既定値に"}},
        {{"cmd", "play"}, {"args", "note, velocity?: 0..1, duration?: 秒"}, {"desc", "その場で鳴らす(DAW の出力に出る)"}},
        {{"cmd", "note_on / note_off"}, {"args", "note, velocity?"}, {"desc", "押しっぱなし/離す"}},
        {{"cmd", "panic"}, {"desc", "全ノートオフ"}},
        {{"cmd", "render"},
         {"args", "out: wav パス, notes: [[t, dur, note, vel]], unit?: seconds|beats, bpm?, length?, sample_rate?, "
                  "params?: {一時的な上書き}, preset?, automation?: [[t, {key: 値}]], input?: wav(エフェクト)"},
         {"desc", "今の設定で DAW を通さずに WAV を書き出す(24bit)。音程のプローブ用"}},
    });
}

static json paramList(const ParamStore& store) {
    json arr = json::array();
    const auto& defs = *store.spec().params;
    for (int i = 0; i < store.size(); ++i) {
        const ParamDef& d = defs[i];
        json p{{"id", i}, {"key", d.key}, {"name", d.name}, {"unit", d.unit}, {"scale", scaleName(d.scale)},
               {"min", d.min}, {"max", d.max}, {"default", d.def}, {"value", store.get(i)},
               {"display", formatValue(d, store.get(i))}};
        if (!d.labels.empty()) p["labels"] = d.labels;
        if (d.help && *d.help) p["help"] = d.help;
        arr.push_back(p);
    }
    return arr;
}

std::string handleCommand(ParamStore& store, const std::string& line, const LiveHooks& hooks) {
    json resp;
    json req;
    try {
        req = json::parse(line);
    } catch (const std::exception& e) {
        return json{{"ok", false}, {"error", std::string("invalid json: ") + e.what()}}.dump();
    }
    if (req.contains("id")) resp["id"] = req["id"];
    const std::string cmd = req.value("cmd", std::string());
    const PluginSpec& spec = store.spec();
    try {
        json result;
        if (cmd == "help" || cmd.empty()) {
            result = commandHelp();
        } else if (cmd == "info") {
            result = hooks.info ? hooks.info() : json::object();
            result["plugin"] = spec.name;
            result["plugin_id"] = spec.id;
            result["version"] = kVersion;
            result["preset"] = store.presetName();
        } else if (cmd == "list_params") {
            result = paramList(store);
        } else if (cmd == "get") {
            json all = store.valuesJson(req.value("changed_only", false));
            if (req.contains("keys")) {
                result = json::object();
                for (const auto& k : req["keys"]) {
                    std::string key = k.get<std::string>();
                    if (spec.find(key) < 0) throw std::runtime_error("unknown param: " + key);
                    result[key] = all[key];
                }
            } else result = all;
        } else if (cmd == "set") {
            if (!req.contains("params")) throw std::runtime_error("set needs params");
            std::vector<std::string> errors;
            if (req.value("normalized", false)) {
                for (auto it = req["params"].begin(); it != req["params"].end(); ++it) {
                    int id = spec.find(it.key());
                    if (id < 0) { errors.push_back("unknown param: " + it.key()); continue; }
                    store.set(id, toPlain((*spec.params)[id], it->get<double>()), true);
                }
            } else store.applyJson(req["params"], true, &errors);
            if (!errors.empty() && errors.size() >= req["params"].size()) throw std::runtime_error(errors.front());
            if (!errors.empty()) resp["warnings"] = errors;
            std::string pn = store.presetName();
            if (pn.size() < 2 || pn.compare(pn.size() - 2, 2, " *") != 0) store.setPresetName(pn + " *");
            json changed = json::object(), all = store.valuesJson();
            for (auto it = req["params"].begin(); it != req["params"].end(); ++it)
                if (all.contains(it.key())) changed[it.key()] = all[it.key()];
            result = changed;
        } else if (cmd == "list_presets") {
            result = json::array();
            for (const auto& p : *spec.presets) result.push_back({{"name", p.name}, {"desc", p.desc}});
        } else if (cmd == "load_preset") {
            std::string name = req.value("name", std::string());
            if (!store.loadPreset(name, true)) throw std::runtime_error("unknown preset: " + name);
            if (req.contains("params")) {
                std::vector<std::string> errors;
                store.applyJson(req["params"], true, &errors);
                if (!errors.empty()) resp["warnings"] = errors;
            }
            result = store.valuesJson(true);
        } else if (cmd == "get_state") {
            result = store.stateJson();
        } else if (cmd == "set_state") {
            if (!req.contains("state") || !store.loadState(req["state"], true)) throw std::runtime_error("set_state needs state");
            result = store.valuesJson(true);
        } else if (cmd == "reset") {
            store.resetDefaults(true);
            store.setPresetName("Init");
            result = true;
        } else if (cmd == "play" || cmd == "note_on") {
            if (!hooks.play) throw std::runtime_error("live playback is not available here");
            int note = req.value("note", 60);
            double vel = req.value("velocity", 1.0);
            if (vel > 1.0) vel /= 127.0;
            hooks.play(note, vel, cmd == "play" ? req.value("duration", 0.5) : 0.0);
            result = true;
        } else if (cmd == "note_off") {
            if (!hooks.noteOff) throw std::runtime_error("live playback is not available here");
            hooks.noteOff(req.value("note", 60));
            result = true;
        } else if (cmd == "panic") {
            if (hooks.panic) hooks.panic();
            result = true;
        } else if (cmd == "render") {
            RenderRequest rr;
            std::string err;
            if (!parseRenderRequest(req, rr, err)) throw std::runtime_error(err);
            ParamStore tmp(spec);
            for (int i = 0; i < store.size(); ++i) tmp.set(i, store.get(i), false);
            if (req.contains("preset") && !tmp.loadPreset(req["preset"].get<std::string>(), false))
                throw std::runtime_error("unknown preset");
            if (req.contains("params")) tmp.applyJson(req["params"], false, nullptr);
            if (!spec.instrument && rr.inL.empty()) throw std::runtime_error("render of an effect needs input (wav path)");
            RenderResult r = render(spec, tmp, rr);
            std::string out = req.value("out", std::string());
            if (out.empty()) throw std::runtime_error("render needs out (wav path)");
            if (!writeWav(out, r.L, r.R, rr.sampleRate, req.value("bits", 24))) throw std::runtime_error("cannot write " + out);
            result = {{"out", out}, {"seconds", r.L.size() / rr.sampleRate}, {"peak", r.peak}, {"rms", r.rms},
                      {"peak_db", 20 * std::log10(r.peak + 1e-12)}};
        } else {
            throw std::runtime_error("unknown cmd: " + cmd + "(help で一覧)");
        }
        resp["ok"] = true;
        resp["result"] = result;
    } catch (const std::exception& e) {
        resp["ok"] = false;
        resp["error"] = e.what();
    }
    return resp.dump();
}

}  // namespace mps
