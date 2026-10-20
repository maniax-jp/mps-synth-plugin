// ステータス表示 GUI をオフスクリーンで描いて PNG に書き出す(Linux の開発用。DAW なしで見た目を確かめる)
//   mps_gui_snapshot OUT.png
#include <dlfcn.h>

#include <cstdio>

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/coffscreencontext.h"
#include "vstgui/lib/platform/platformfactory.h"
#include "vstgui/lib/vstguiinit.h"

#include "controller.h"
#include "status_editor.h"

using namespace VSTGUI;

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    VSTGUI::init(dlopen(nullptr, RTLD_LAZY));
    auto* ctrl = new mpsvst::MpsController(mps::synthSpec());
    ctrl->initialize(nullptr);
    mps::ParamStore st(mps::synthSpec());
    st.loadPreset("Slap Bass", false);
    st.set(mps::synthSpec().find("cutoff"), 900, false);
    st.set(mps::synthSpec().find("drive"), 5, false);
    for (int i = 0; i < st.size(); ++i)
        ctrl->setParamNormalized(i, mps::toNorm((*mps::synthSpec().params)[i], st.get(i)));
    ctrl->setStatus({{"port", 47800}, {"track", "Bass"}, {"name", "bass-synth"}, {"preset", "Slap Bass *"}, {"bpm", 160.0}, {"playing", true},
                     {"midi_notes", 128}, {"commands", 5},
                     {"log", {"21:04:12  set", "21:04:11  load_preset", "21:04:09  set_name"}}});
    CRect r(0, 0, 560, 400);
    auto* view = new mpsvst::StatusView(r, ctrl);
    auto off = COffscreenContext::create({r.getWidth(), r.getHeight()}, 1.0);
    off->beginDraw();
    view->draw(off);
    off->endDraw();
    auto bmp = off->getBitmap();
    auto png = getPlatformFactory().createBitmapMemoryPNGRepresentation(bmp->getPlatformBitmap());
    FILE* f = std::fopen(argv[1], "wb");
    std::fwrite(png.data(), 1, png.size(), f);
    std::fclose(f);
    view->forget();
    ctrl->terminate();
    ctrl->release();
    std::printf("wrote %s\n", argv[1]);
    return 0;
}
