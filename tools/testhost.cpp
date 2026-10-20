// MPS.vst3 を DAW の代わりに読み込み、実時間で process() を回すテスト用ホスト。
// 制御サーバー経由の操作(set → ホストへの通知、play → 発音)を DAW なしで確かめるために使う。
//
//   mps_testhost PATH/MPS.vst3 "MPS Synth" SECONDS OUT.wav [--note MIDI] [--input IN.wav] [--track NAME]
//
// 出力パラメータの変更(プラグイン → ホストの通知)を受けたらコントローラーへ渡し、"host: ..." と表示する。
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstchannelcontextinfo.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "public.sdk/source/vst/utility/stringconvert.h"
#include "public.sdk/source/vst/vstpresetfile.h"

#include "control.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

int main(int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "usage: mps_testhost PATH/MPS.vst3 \"MPS Synth\"|\"MPS FX\" SECONDS OUT.wav [--note MIDI] [--input IN.wav] [--track NAME]\n";
        return 2;
    }
    std::string path = argv[1], name = argv[2], out = argv[4], input, track;
    double seconds = std::atof(argv[3]);
    int note = -1;
    for (int i = 5; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--note")) note = std::atoi(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--input")) input = argv[i + 1];
        else if (!std::strcmp(argv[i], "--track")) track = argv[i + 1];
    }

    auto hostApp = owned(new HostApplication());
    PluginContextFactory::instance().setPluginContext(hostApp);
    std::string err;
    auto module = VST3::Hosting::Module::create(path, err);
    if (!module) {
        std::cerr << "load failed: " << err << "\n";
        return 1;
    }
    auto factory = module->getFactory();
    IPtr<PlugProvider> provider;
    for (auto& ci : factory.classInfos())
        if (ci.category() == kVstAudioEffectClass && ci.name() == name) provider = owned(new PlugProvider(factory, ci, true));
    if (!provider || !provider->initialize()) {
        std::cerr << "class not found: " << name << "\n";
        return 1;
    }
    IPtr<IComponent> comp = provider->getComponentPtr();
    IPtr<IEditController> ctrl = provider->getControllerPtr();
    FUnknownPtr<IAudioProcessor> proc(comp);
    if (!track.empty()) {
        if (FUnknownPtr<ChannelContext::IInfoListener> il{ctrl}) {
            auto list = HostAttributeList::make();
            auto t16 = VST3::StringConvert::convert(track);
            list->setString(ChannelContext::kChannelNameKey, reinterpret_cast<const TChar*>(t16.data()));
            il->setChannelContextInfos(list);
        }
    }
    IPlugView* view = ctrl->createView(ViewType::kEditor);
    std::cout << "editor: " << (view ? "yes" : "no") << "\n";
    if (view) view->release();

    const double sr = 48000;
    const int block = 512;
    ProcessSetup setup{kRealtime, kSample32, block, sr};
    proc->setupProcessing(setup);
    comp->activateBus(kEvent, kInput, 0, true);
    comp->activateBus(kAudio, kOutput, 0, true);
    if (name == "MPS FX") comp->activateBus(kAudio, kInput, 0, true);
    comp->setActive(true);
    proc->setProcessing(true);

    std::vector<float> inL, inR;
    if (!input.empty()) {
        double isr;
        mps::readWav(input, inL, inR, isr);
    }
    HostProcessData data;
    data.prepare(*comp, block, kSample32);
    ParameterChanges inChanges, outChanges;
    EventList events;
    ProcessContext ctx{};
    ctx.sampleRate = sr;
    ctx.tempo = 160;
    ctx.state = ProcessContext::kPlaying | ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid;
    data.inputParameterChanges = &inChanges;
    data.outputParameterChanges = &outChanges;
    data.inputEvents = &events;
    data.processContext = &ctx;
    data.numSamples = block;

    std::vector<float> L, R;
    const long long total = static_cast<long long>(seconds * sr);
    auto start = std::chrono::steady_clock::now();
    for (long long pos = 0; pos < total; pos += block) {
        events.clear();
        outChanges.clearQueue();
        if (note >= 0 && pos == 0) {
            Event e{};
            e.type = Event::kNoteOnEvent;
            e.noteOn.pitch = static_cast<int16>(note);
            e.noteOn.velocity = 1.f;
            events.addEvent(e);
        }
        if (name == "MPS FX") {
            for (int i = 0; i < block; ++i) {
                size_t k = static_cast<size_t>(pos + i);
                data.inputs[0].channelBuffers32[0][i] = k < inL.size() ? inL[k] : 0.f;
                data.inputs[0].channelBuffers32[1][i] = k < inR.size() ? inR[k] : 0.f;
            }
        }
        ctx.projectTimeSamples = pos;
        ctx.projectTimeMusic = pos / sr * ctx.tempo / 60.0;
        proc->process(data);
        for (int i = 0; i < block; ++i) {
            L.push_back(data.outputs[0].channelBuffers32[0][i]);
            R.push_back(data.outputs[0].channelBuffers32[1][i]);
        }
        for (int32 i = 0; i < outChanges.getParameterCount(); ++i) {
            IParamValueQueue* q = outChanges.getParameterData(i);
            int32 off;
            ParamValue v;
            q->getPoint(q->getPointCount() - 1, off, v);
            ctrl->setParamNormalized(q->getParameterId(), v);
            String128 s;
            ctrl->getParamStringByValue(q->getParameterId(), v, s);
            std::cout << "host: param " << q->getParameterId() << " -> " << VST3::StringConvert::convert(s) << "\n" << std::flush;
        }
        // 実時間に合わせる
        std::this_thread::sleep_until(start + std::chrono::microseconds(static_cast<long long>((pos + block) / sr * 1e6)));
    }
    proc->setProcessing(false);
    comp->setActive(false);

    MemoryStream state;
    comp->getState(&state);
    std::string st(state.getData(), static_cast<size_t>(state.getSize()));
    std::cout << "state: " << st << "\n";
    mps::writeWav(out, L, R, sr);
    double peak = 0;
    for (size_t i = 0; i < L.size(); ++i) peak = std::max(peak, static_cast<double>(std::fabs(L[i])));
    std::cout << "wrote " << out << " peak " << peak << "\n";
    data.unprepare();
    provider = nullptr;
    PluginContextFactory::instance().setPluginContext(nullptr);
    return 0;
}
