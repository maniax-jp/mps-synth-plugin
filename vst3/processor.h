#pragma once
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "public.sdk/source/vst/vstaudioeffect.h"

#include "control.h"
#include "control_server.h"

namespace mpsvst {

class MpsProcessor : public Steinberg::Vst::AudioEffect {
public:
    MpsProcessor(const mps::PluginSpec& spec, const Steinberg::FUID& controllerUID);
    ~MpsProcessor() override;
    static Steinberg::FUnknown* createSynth(void*);
    static Steinberg::FUnknown* createFx(void*);

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
                                                     Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

private:
    struct PendingNote {
        int note;
        double vel, dur;
        bool off;
    };
    std::string handle(const std::string& line);
    mps::json statusJson();
    void applyEngineParams();
    void syncNameToRegistry();
    void renderSegment(Steinberg::Vst::ProcessData& data, int start, int n, const mps::Transport& tr);

    const mps::PluginSpec& spec_;
    mps::ParamStore store_;
    std::unique_ptr<mps::Engine> engine_;
    std::vector<double> engineValues_;
    mps::ControlServer server_;
    double sampleRate_ = 48000;
    int maxBlock_ = 1024;
    long long sampleClock_ = 0;

    std::mutex noteMutex_;
    std::vector<PendingNote> pending_;
    std::vector<std::pair<long long, int>> scheduledOffs_;  // 音を止めるサンプル時刻(audio スレッドだけが触る)

    std::mutex infoMutex_;
    std::string track_;
    std::string publishedName_;
    std::deque<std::string> log_;
    long long commandCount_ = 0;
    std::atomic<double> bpm_{120.0};
    std::atomic<bool> playing_{false};
    std::atomic<int> activeEvents_{0};
};

}  // namespace mpsvst
