// DAW を通さずに MPS Synth / MPS FX と同じ DSP で WAV を書き出す CLI。
//
//   mps_render SPEC.json            # SPEC は 1 件のオブジェクトか、その配列(まとめて書き出す)
//   mps_render -                    # 標準入力から
//   mps_render --list-presets synth|fx
//   mps_render --list-params synth|fx
//
// SPEC: {"plugin": "synth", "preset": "Slap Bass", "params": {"cutoff": 900}, "state": {...},
//        "notes": [[0, 0.25, 41, 1.0]], "unit": "seconds"|"beats", "bpm": 160, "length": 2,
//        "sample_rate": 48000, "automation": [[t, {"key": 値}]], "input": "in.wav", "out": "out.wav", "bits": 24}
#include <fstream>
#include <iostream>
#include <sstream>

#include "../core/control.h"

using namespace mps;

static int renderOne(const json& spec, json& report) {
    const PluginSpec* ps = specById(spec.value("plugin", std::string("synth")));
    if (!ps) {
        report = {{"ok", false}, {"error", "unknown plugin (synth | fx)"}};
        return 1;
    }
    ParamStore store(*ps);
    if (spec.contains("state")) store.loadState(spec["state"], false);
    if (spec.contains("preset") && !store.loadPreset(spec["preset"].get<std::string>(), false)) {
        report = {{"ok", false}, {"error", "unknown preset: " + spec["preset"].get<std::string>()}};
        return 1;
    }
    json req = spec;
    req["cmd"] = "render";
    req.erase("preset");
    std::string line = handleCommand(store, req.dump(), LiveHooks{});
    report = json::parse(line);
    return report.value("ok", false) ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: mps_render SPEC.json | - | --list-presets synth|fx | --list-params synth|fx\n";
        return 2;
    }
    std::string a1 = argv[1];
    if (a1 == "--list-presets" || a1 == "--list-params") {
        const PluginSpec* ps = specById(argc > 2 ? argv[2] : "synth");
        if (!ps) return 2;
        ParamStore store(*ps);
        std::string cmd = a1 == "--list-presets" ? "list_presets" : "list_params";
        std::cout << json::parse(handleCommand(store, json{{"cmd", cmd}}.dump(), LiveHooks{}))["result"].dump(2) << "\n";
        return 0;
    }
    std::stringstream ss;
    if (a1 == "-") ss << std::cin.rdbuf();
    else {
        std::ifstream f(a1);
        if (!f) {
            std::cerr << "cannot open " << a1 << "\n";
            return 2;
        }
        ss << f.rdbuf();
    }
    json spec;
    try {
        spec = json::parse(ss.str());
    } catch (const std::exception& e) {
        std::cerr << "invalid json: " << e.what() << "\n";
        return 2;
    }
    int rc = 0;
    json out = json::array();
    for (const auto& s : spec.is_array() ? spec : json::array({spec})) {
        json r;
        rc |= renderOne(s, r);
        out.push_back(r);
    }
    std::cout << (spec.is_array() ? out : out[0]).dump() << "\n";
    return rc;
}
