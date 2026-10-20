// 1 つの MPS.vst3 に MPS Synth(音源)と MPS FX(エフェクト)の 2 つを入れる
#include "public.sdk/source/main/pluginfactory.h"

#include "cids.h"
#include "controller.h"
#include "engine.h"
#include "processor.h"

#define MPS_VENDOR "Maniax"
#define MPS_URL "https://github.com/maniax-jp/mps-synth-plugin"

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_FACTORY_DEF(MPS_VENDOR, MPS_URL, "")

DEF_CLASS2(INLINE_UID_FROM_FUID(mpsvst::kSynthProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass, "MPS Synth",
           Vst::kDistributable, PlugType::kInstrumentSynth, mps::kVersion, kVstVersionString, mpsvst::MpsProcessor::createSynth)
DEF_CLASS2(INLINE_UID_FROM_FUID(mpsvst::kSynthControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
           "MPS Synth Controller", 0, "", mps::kVersion, kVstVersionString, mpsvst::MpsController::createSynth)

DEF_CLASS2(INLINE_UID_FROM_FUID(mpsvst::kFxProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass, "MPS FX",
           Vst::kDistributable, PlugType::kFx, mps::kVersion, kVstVersionString, mpsvst::MpsProcessor::createFx)
DEF_CLASS2(INLINE_UID_FROM_FUID(mpsvst::kFxControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
           "MPS FX Controller", 0, "", mps::kVersion, kVstVersionString, mpsvst::MpsController::createFx)

END_FACTORY
