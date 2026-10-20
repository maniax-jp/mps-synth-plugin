#include "controller.h"

#include <cstring>

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "public.sdk/source/vst/utility/stringconvert.h"

#include "cids.h"
#ifdef MPS_WITH_GUI
#include "status_editor.h"
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace mpsvst {

// ============================================================================ MpsParameter
MpsParameter::MpsParameter(const mps::ParamDef& def, ParamID id) : def_(def) {
    info.id = id;
    UString(info.title, str16BufferSize(String128)).fromAscii(def.name);
    UString(info.shortTitle, str16BufferSize(String128)).fromAscii(def.key);
    UString(info.units, str16BufferSize(String128)).fromAscii(def.unit);
    info.stepCount = mps::stepCount(def);
    info.defaultNormalizedValue = mps::toNorm(def, def.def);
    info.flags = ParameterInfo::kCanAutomate;
    if (def.scale == mps::Scale::Enum) info.flags |= ParameterInfo::kIsList;
    if (def.hidden) info.flags |= ParameterInfo::kIsHidden;
    setNormalized(info.defaultNormalizedValue);
}

void MpsParameter::toString(ParamValue normValue, String128 string) const {
    std::string s = mps::formatValue(def_, mps::toPlain(def_, normValue));
    UString(string, str16BufferSize(String128)).fromAscii(s.c_str());
}

bool MpsParameter::fromString(const TChar* string, ParamValue& normValue) const {
    std::string s = VST3::StringConvert::convert(string);
    double plain;
    if (!mps::parseValue(def_, s, plain)) return false;
    normValue = mps::toNorm(def_, plain);
    return true;
}

ParamValue MpsParameter::toPlain(ParamValue normValue) const { return mps::toPlain(def_, normValue); }
ParamValue MpsParameter::toNormalized(ParamValue plainValue) const { return mps::toNorm(def_, plainValue); }

// ============================================================================ MpsController
FUnknown* MpsController::createSynth(void*) { return static_cast<IEditController*>(new MpsController(mps::synthSpec())); }
FUnknown* MpsController::createFx(void*) { return static_cast<IEditController*>(new MpsController(mps::fxSpec())); }

tresult PLUGIN_API MpsController::initialize(FUnknown* context) {
    tresult r = EditController::initialize(context);
    if (r != kResultOk) return r;
    const auto& defs = *spec_.params;
    for (size_t i = 0; i < defs.size(); ++i) parameters.addParameter(new MpsParameter(defs[i], static_cast<ParamID>(i)));
    return kResultOk;
}

tresult PLUGIN_API MpsController::setComponentState(IBStream* state) {
    if (!state) return kResultFalse;
    std::string text;
    char buf[4096];
    int32 got = 0;
    while (state->read(buf, sizeof buf, &got) == kResultOk && got > 0) text.append(buf, got);
    mps::ParamStore tmp(spec_);
    try {
        tmp.loadState(mps::json::parse(text), false);
    } catch (...) {
        return kResultFalse;
    }
    for (int i = 0; i < tmp.size(); ++i) setParamNormalized(static_cast<ParamID>(i), mps::toNorm((*spec_.params)[i], tmp.get(i)));
    return kResultOk;
}

IPlugView* PLUGIN_API MpsController::createView(FIDString name) {
#ifdef MPS_WITH_GUI
    if (FIDStringsEqual(name, ViewType::kEditor)) return new StatusEditor(this);
#else
    (void)name;
#endif
    return nullptr;
}

void MpsController::sendBinary(const char* id, const std::string& data) {
    if (auto msg = owned(allocateMessage())) {
        msg->setMessageID(id);
        msg->getAttributes()->setBinary("data", data.data(), static_cast<uint32>(data.size()));
        sendMessage(msg);
    }
}

tresult PLUGIN_API MpsController::connect(IConnectionPoint* other) {
    tresult r = EditController::connect(other);
    std::string t = trackName();
    if (!t.empty()) sendBinary(kMsgTrack, t);
    return r;
}

tresult PLUGIN_API MpsController::notify(IMessage* message) {
    if (message && std::strcmp(message->getMessageID(), kMsgStatus) == 0) {
        const void* data = nullptr;
        uint32 size = 0;
        if (message->getAttributes()->getBinary("data", data, size) == kResultOk) {
            try {
                auto j = mps::json::parse(std::string(static_cast<const char*>(data), size));
                std::lock_guard<std::mutex> lk(mutex_);
                status_ = j;
            } catch (...) {
            }
        }
        return kResultOk;
    }
    return EditController::notify(message);
}

tresult PLUGIN_API MpsController::getMidiControllerAssignment(int32 busIndex, int16, CtrlNumber midiControllerNumber,
                                                              ParamID& id) {
    if (busIndex == 0 && midiControllerNumber == kPitchBend) {
        int i = spec_.find("pitch_bend");
        if (i >= 0) {
            id = static_cast<ParamID>(i);
            return kResultTrue;
        }
    }
    return kResultFalse;
}

tresult PLUGIN_API MpsController::setChannelContextInfos(IAttributeList* list) {
    if (!list) return kResultFalse;
    String128 name = {0};
    if (list->getString(ChannelContext::kChannelNameKey, name, sizeof(name)) == kResultTrue) {
        std::string s = VST3::StringConvert::convert(name);
        {
            std::lock_guard<std::mutex> lk(mutex_);
            track_ = s;
        }
        sendBinary(kMsgTrack, s);
    }
    return kResultTrue;
}

void MpsController::requestStatus() { sendBinary(kMsgStatusReq, ""); }

mps::json MpsController::status() {
    std::lock_guard<std::mutex> lk(mutex_);
    return status_;
}

void MpsController::setStatus(const mps::json& s) {
    std::lock_guard<std::mutex> lk(mutex_);
    status_ = s;
}

std::string MpsController::trackName() {
    std::lock_guard<std::mutex> lk(mutex_);
    return track_;
}

}  // namespace mpsvst
