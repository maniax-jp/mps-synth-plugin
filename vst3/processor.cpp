#include "processor.h"

#include <chrono>
#include <cstring>
#include <ctime>

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include "cids.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace mpsvst {

MpsProcessor::MpsProcessor(const mps::PluginSpec& spec, const FUID& controllerUID) : spec_(spec), store_(spec) {
    setControllerClass(controllerUID);
    engineValues_.assign(store_.size(), -1e300);
}

MpsProcessor::~MpsProcessor() { server_.stop(); }

FUnknown* MpsProcessor::createSynth(void*) {
    return static_cast<IAudioProcessor*>(new MpsProcessor(mps::synthSpec(), kSynthControllerUID));
}
FUnknown* MpsProcessor::createFx(void*) {
    return static_cast<IAudioProcessor*>(new MpsProcessor(mps::fxSpec(), kFxControllerUID));
}

tresult PLUGIN_API MpsProcessor::initialize(FUnknown* context) {
    tresult r = AudioEffect::initialize(context);
    if (r != kResultOk) return r;
    if (!spec_.instrument) addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
    addEventInput(STR16("MIDI In"), 1);
    server_.start([this](const std::string& line) { return handle(line); }, spec_.id);
    server_.setMeta({{"plugin", spec_.name}, {"track", ""}, {"name", store_.instanceName()}});
    publishedName_ = store_.instanceName();
    return kResultOk;
}

tresult PLUGIN_API MpsProcessor::terminate() {
    server_.stop();
    return AudioEffect::terminate();
}

tresult PLUGIN_API MpsProcessor::setBusArrangements(SpeakerArrangement* inputs, int32 numIns, SpeakerArrangement* outputs,
                                                    int32 numOuts) {
    const int32 wantIns = spec_.instrument ? 0 : 1;
    if (numIns != wantIns || numOuts != 1 || outputs[0] != SpeakerArr::kStereo) return kResultFalse;
    if (wantIns && inputs[0] != SpeakerArr::kStereo) return kResultFalse;
    return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
}

tresult PLUGIN_API MpsProcessor::canProcessSampleSize(int32 s) { return s == kSample32 ? kResultTrue : kResultFalse; }

tresult PLUGIN_API MpsProcessor::setupProcessing(ProcessSetup& setup) {
    sampleRate_ = setup.sampleRate;
    maxBlock_ = setup.maxSamplesPerBlock;
    return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API MpsProcessor::setActive(TBool state) {
    if (state) {
        if (!engine_) engine_ = spec_.create();
        engine_->prepare(sampleRate_, maxBlock_);
        std::fill(engineValues_.begin(), engineValues_.end(), -1e300);
        applyEngineParams();
        scheduledOffs_.clear();
        scheduledOffs_.reserve(512);
        std::lock_guard<std::mutex> lk(noteMutex_);
        pending_.reserve(512);
    }
    return AudioEffect::setActive(state);
}

void MpsProcessor::applyEngineParams() {
    if (!engine_) return;
    for (int i = 0; i < store_.size(); ++i) {
        double v = store_.get(i);
        if (v != engineValues_[i]) {
            engineValues_[i] = v;
            engine_->setParam(i, v);
        }
    }
}

void MpsProcessor::renderSegment(ProcessData& data, int start, int n, const mps::Transport& tr) {
    if (n <= 0) return;
    float* out[2] = {data.outputs[0].channelBuffers32[0] + start, data.outputs[0].channelBuffers32[1] + start};
    const float* in[2] = {out[0], out[1]};
    if (!spec_.instrument && data.numInputs > 0 && data.inputs[0].numChannels >= 2) {
        in[0] = data.inputs[0].channelBuffers32[0] + start;
        in[1] = data.inputs[0].channelBuffers32[1] + start;
    } else if (!spec_.instrument) {
        std::memset(out[0], 0, sizeof(float) * n);
        std::memset(out[1], 0, sizeof(float) * n);
    }
    mps::Transport t = tr;
    t.ppq = tr.ppq + start / sampleRate_ * tr.bpm / 60.0;
    engine_->process(in, out, n, t);
}

tresult PLUGIN_API MpsProcessor::process(ProcessData& data) {
    // ---- ホストからのパラメータ変更(ブロック内の最後の値)
    if (auto* changes = data.inputParameterChanges) {
        for (int32 i = 0; i < changes->getParameterCount(); ++i) {
            IParamValueQueue* q = changes->getParameterData(i);
            if (!q) continue;
            ParamID id = q->getParameterId();
            int32 offset;
            ParamValue v;
            if (id < static_cast<ParamID>(store_.size()) && q->getPoint(q->getPointCount() - 1, offset, v) == kResultTrue)
                store_.set(static_cast<int>(id), mps::toPlain((*spec_.params)[id], v), false);
        }
    }
    // ---- AI 接続で変えた値をホストへ知らせる(DAW の表示・自動化の記録)
    if (auto* outChanges = data.outputParameterChanges) {
        for (int i = 0; i < store_.size(); ++i) {
            if (!store_.takeDirty(i)) continue;
            int32 idx = 0;
            if (IParamValueQueue* q = outChanges->addParameterData(static_cast<ParamID>(i), idx)) {
                int32 pidx = 0;
                q->addPoint(0, mps::toNorm((*spec_.params)[i], store_.get(i)), pidx);
            }
        }
    }
    if (!engine_) return kResultOk;
    applyEngineParams();

    mps::Transport tr;
    if (data.processContext) {
        const ProcessContext& pc = *data.processContext;
        if (pc.state & ProcessContext::kTempoValid) tr.bpm = pc.tempo;
        if (pc.state & ProcessContext::kProjectTimeMusicValid) tr.ppq = pc.projectTimeMusic;
        tr.playing = (pc.state & ProcessContext::kPlaying) != 0;
    }
    bpm_ = tr.bpm;
    playing_ = tr.playing;
    if (data.numSamples <= 0 || data.numOutputs < 1 || data.outputs[0].numChannels < 2) return kResultOk;

    // ---- AI 接続からのノート(ブロック先頭で鳴らす)
    if (noteMutex_.try_lock()) {
        for (const auto& p : pending_) {
            if (p.off) engine_->noteOff(p.note);
            else {
                engine_->noteOn(p.note, p.vel);
                if (p.dur > 0) scheduledOffs_.emplace_back(sampleClock_ + static_cast<long long>(p.dur * sampleRate_), p.note);
            }
        }
        pending_.clear();
        noteMutex_.unlock();
    }
    for (size_t i = 0; i < scheduledOffs_.size();) {
        if (scheduledOffs_[i].first <= sampleClock_ + data.numSamples) {
            engine_->noteOff(scheduledOffs_[i].second);
            scheduledOffs_.erase(scheduledOffs_.begin() + static_cast<long>(i));
        } else ++i;
    }

    // ---- MIDI イベントで区切って処理
    int pos = 0;
    if (IEventList* events = data.inputEvents) {
        const int32 count = events->getEventCount();
        for (int32 i = 0; i < count; ++i) {
            Event e;
            if (events->getEvent(i, e) != kResultOk) continue;
            int at = std::max(pos, std::min<int>(e.sampleOffset, data.numSamples));
            renderSegment(data, pos, at - pos, tr);
            pos = at;
            if (e.type == Event::kNoteOnEvent) {
                if (e.noteOn.velocity <= 0) engine_->noteOff(e.noteOn.pitch);
                else engine_->noteOn(e.noteOn.pitch, e.noteOn.velocity);
                activeEvents_++;
            } else if (e.type == Event::kNoteOffEvent) {
                engine_->noteOff(e.noteOff.pitch);
            }
        }
    }
    renderSegment(data, pos, data.numSamples - pos, tr);
    data.outputs[0].silenceFlags = 0;
    sampleClock_ += data.numSamples;
    return kResultOk;
}

tresult PLUGIN_API MpsProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    std::string text;
    char buf[4096];
    int32 got = 0;
    while (state->read(buf, sizeof buf, &got) == kResultOk && got > 0) text.append(buf, got);
    try {
        store_.loadState(mps::json::parse(text), false);
    } catch (...) {
        return kResultFalse;
    }
    syncNameToRegistry();
    return kResultOk;
}

tresult PLUGIN_API MpsProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    std::string text = store_.stateJson().dump();
    int32 written = 0;
    return state->write(const_cast<char*>(text.data()), static_cast<int32>(text.size()), &written);
}

static std::string nowString() {
    std::time_t t = std::time(nullptr);
    char b[16];
    std::strftime(b, sizeof b, "%H:%M:%S", std::localtime(&t));
    return b;
}

std::string MpsProcessor::handle(const std::string& line) {
    mps::LiveHooks hooks;
    hooks.play = [this](int note, double vel, double dur) {
        std::lock_guard<std::mutex> lk(noteMutex_);
        pending_.push_back({note, vel, dur, false});
    };
    hooks.noteOff = [this](int note) {
        std::lock_guard<std::mutex> lk(noteMutex_);
        pending_.push_back({note, 0, 0, true});
    };
    hooks.panic = [this] {
        std::lock_guard<std::mutex> lk(noteMutex_);
        for (int n = 0; n < 128; ++n) pending_.push_back({n, 0, 0, true});
    };
    hooks.info = [this] {
        mps::json j = statusJson();
        j.erase("log");
        return j;
    };
    std::string resp = mps::handleCommand(store_, line, hooks);
    syncNameToRegistry();
    std::string cmd = "?";
    bool ok = false;
    try {
        cmd = mps::json::parse(line).value("cmd", std::string("?"));
        ok = mps::json::parse(resp).value("ok", false);
    } catch (...) {
    }
    {
        std::lock_guard<std::mutex> lk(infoMutex_);
        commandCount_++;
        log_.push_front(nowString() + "  " + cmd + (ok ? "" : "  (error)"));
        while (log_.size() > 6) log_.pop_back();
    }
    return resp;
}

void MpsProcessor::syncNameToRegistry() {
    std::string n = store_.instanceName();
    {
        std::lock_guard<std::mutex> lk(infoMutex_);
        if (n == publishedName_) return;
        publishedName_ = n;
    }
    server_.setMeta({{"name", n}});
}

mps::json MpsProcessor::statusJson() {
    mps::json j{{"port", server_.port()}, {"sample_rate", sampleRate_}, {"bpm", bpm_.load()},
                {"playing", playing_.load()}, {"midi_notes", activeEvents_.load()}};
    std::lock_guard<std::mutex> lk(infoMutex_);
    j["track"] = track_;
    j["commands"] = commandCount_;
    j["log"] = log_;
    j["registry"] = mps::ControlServer::registryDir();
    j["preset"] = store_.presetName();
    j["name"] = store_.instanceName();
    return j;
}

tresult PLUGIN_API MpsProcessor::notify(IMessage* message) {
    if (!message) return kInvalidArgument;
    const char* id = message->getMessageID();
    if (std::strcmp(id, kMsgTrack) == 0) {
        const void* data = nullptr;
        uint32 size = 0;
        if (message->getAttributes()->getBinary("data", data, size) == kResultOk) {
            std::string name(static_cast<const char*>(data), size);
            {
                std::lock_guard<std::mutex> lk(infoMutex_);
                track_ = name;
            }
            server_.setMeta({{"track", name}});
        }
        return kResultOk;
    }
    if (std::strcmp(id, kMsgStatusReq) == 0) {
        std::string s = statusJson().dump();
        if (auto msg = owned(allocateMessage())) {
            msg->setMessageID(kMsgStatus);
            msg->getAttributes()->setBinary("data", s.data(), static_cast<uint32>(s.size()));
            sendMessage(msg);
        }
        return kResultOk;
    }
    return AudioEffect::notify(message);
}

}  // namespace mpsvst
