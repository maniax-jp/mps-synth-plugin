// 状態を表示するだけの GUI(操作はしない。値の変更は DAW の汎用画面か制御サーバーで行う)。
// 制御ポート・トラック名・プリセット名・AI から届いたコマンド・既定値から変えたパラメータを表示する。
#pragma once
#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/cview.h"

namespace mpsvst {

class MpsController;

class StatusView : public VSTGUI::CView {
public:
    StatusView(const VSTGUI::CRect& size, MpsController* controller) : CView(size), controller_(controller) {}
    void draw(VSTGUI::CDrawContext* context) override;

private:
    MpsController* controller_;
};

class StatusEditor : public Steinberg::Vst::VSTGUIEditor {
public:
    explicit StatusEditor(MpsController* controller);
    bool PLUGIN_API open(void* parent, const VSTGUI::PlatformType& platformType) override;
    void PLUGIN_API close() override;

private:
    MpsController* controller_;
    StatusView* view_ = nullptr;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer_;
};

}  // namespace mpsvst
