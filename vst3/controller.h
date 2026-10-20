#pragma once
#include <mutex>
#include <string>

#include "pluginterfaces/vst/ivstchannelcontextinfo.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include "control.h"

namespace mpsvst {

// ParamDef の変換・表示をそのまま使う VST3 パラメータ
class MpsParameter : public Steinberg::Vst::Parameter {
public:
    MpsParameter(const mps::ParamDef& def, Steinberg::Vst::ParamID id);
    void toString(Steinberg::Vst::ParamValue normValue, Steinberg::Vst::String128 string) const override;
    bool fromString(const Steinberg::Vst::TChar* string, Steinberg::Vst::ParamValue& normValue) const override;
    Steinberg::Vst::ParamValue toPlain(Steinberg::Vst::ParamValue normValue) const override;
    Steinberg::Vst::ParamValue toNormalized(Steinberg::Vst::ParamValue plainValue) const override;

private:
    const mps::ParamDef& def_;
};

class MpsController : public Steinberg::Vst::EditController,
                      public Steinberg::Vst::IMidiMapping,
                      public Steinberg::Vst::ChannelContext::IInfoListener {
public:
    explicit MpsController(const mps::PluginSpec& spec) : spec_(spec) {}
    static Steinberg::FUnknown* createSynth(void*);
    static Steinberg::FUnknown* createFx(void*);

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) override;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;
    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* other) override;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

    Steinberg::tresult PLUGIN_API getMidiControllerAssignment(Steinberg::int32 busIndex, Steinberg::int16 channel,
                                                              Steinberg::Vst::CtrlNumber midiControllerNumber,
                                                              Steinberg::Vst::ParamID& id) override;
    Steinberg::tresult PLUGIN_API setChannelContextInfos(Steinberg::Vst::IAttributeList* list) override;

    // ステータス表示用
    const mps::PluginSpec& spec() const { return spec_; }
    void requestStatus();
    mps::json status();
    void setStatus(const mps::json& s);  // テスト用(gui_snapshot)
    std::string trackName();

    OBJ_METHODS(MpsController, EditController)
    DEFINE_INTERFACES
        DEF_INTERFACE(IMidiMapping)
        DEF_INTERFACE(Steinberg::Vst::ChannelContext::IInfoListener)
    END_DEFINE_INTERFACES(EditController)
    REFCOUNT_METHODS(EditController)

private:
    void sendBinary(const char* id, const std::string& data);
    const mps::PluginSpec& spec_;
    std::mutex mutex_;
    std::string track_;
    mps::json status_ = mps::json::object();
};

}  // namespace mpsvst
