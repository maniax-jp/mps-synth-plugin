#include "status_editor.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"

#include "controller.h"

using namespace VSTGUI;

namespace mpsvst {

static constexpr int kW = 560, kH = 400;

void StatusView::draw(CDrawContext* ctx) {
    const CRect r = getViewSize();
    ctx->setFillColor(CColor(24, 26, 31));
    ctx->drawRect(r, kDrawFilled);
    const mps::PluginSpec& spec = controller_->spec();
    mps::json st = controller_->status();

    CCoord x = r.left + 14, y = r.top + 12;
    auto line = [&](const std::string& s, const CColor& c, CFontRef font, CCoord h) {
        ctx->setFont(font);
        ctx->setFontColor(c);
        ctx->drawString(s.c_str(), CRect(x, y, r.right - 14, y + h), kLeftText);
        y += h;
    };
    const CColor white(235, 235, 240), dim(150, 155, 165), accent(110, 200, 255), ok(120, 220, 140), warn(255, 170, 90);

    std::string iname = st.value("name", std::string());
    line(std::string(spec.name) + "  " + mps::kVersion + (iname.empty() ? std::string() : "    [" + iname + "]"), white,
         kNormalFontBig, 26);
    int port = st.value("port", 0);
    if (st.empty()) line("AI 接続: 状態を取得中...", dim, kNormalFont, 20);
    else if (port > 0) line("AI 接続: 127.0.0.1:" + std::to_string(port), ok, kNormalFont, 20);
    else line("AI 接続: 無効(MPS_CTL_DISABLE かポートが空いていない)", warn, kNormalFont, 20);

    std::string track = st.value("track", controller_->trackName());
    std::string preset = st.value("preset", std::string("-"));
    char tempo[64];
    std::snprintf(tempo, sizeof tempo, "%.1f BPM%s", st.value("bpm", 120.0), st.value("playing", false) ? "  再生中" : "");
    line("トラック: " + (track.empty() ? std::string("-") : track) + "    名前: " + (iname.empty() ? std::string("-") : iname) +
             "    プリセット: " + preset,
         white, kNormalFont, 20);
    line(std::string(tempo) + "    受けたノート: " + std::to_string(st.value("midi_notes", 0)) + " 回    AI コマンド: " +
             std::to_string(st.value("commands", 0)) + " 回",
         dim, kNormalFontSmall, 18);

    y += 4;
    line("最近のコマンド", accent, kNormalFontSmall, 16);
    if (st.contains("log") && st["log"].is_array() && !st["log"].empty()) {
        int k = 0;
        for (const auto& l : st["log"]) {
            if (k++ >= 3) break;
            line("  " + l.get<std::string>(), dim, kNormalFontSmall, 15);
        }
    } else line("  (まだありません)", dim, kNormalFontSmall, 15);

    y += 6;
    line("初期値から変えたパラメータ", accent, kNormalFontSmall, 16);
    std::vector<std::string> items;
    const auto& defs = *spec.params;
    for (size_t i = 0; i < defs.size(); ++i) {
        const mps::ParamDef& d = defs[i];
        double plain = mps::toPlain(d, controller_->getParamNormalized(static_cast<Steinberg::Vst::ParamID>(i)));
        if (std::fabs(plain - d.def) < 1e-6 * std::max(1.0, std::fabs(d.max - d.min))) continue;
        std::string s = std::string(d.key) + " = " + mps::formatValue(d, plain);
        if (*d.unit) s += std::string(" ") + d.unit;
        items.push_back(s);
    }
    if (items.empty()) line("  (なし: すべて初期値)", dim, kNormalFontSmall, 15);
    ctx->setFont(kNormalFontSmall);
    ctx->setFontColor(white);
    const int cols = 2;
    const CCoord colW = (r.getWidth() - 28) / cols, rowH = 15;
    const int rows = static_cast<int>((r.bottom - 8 - y) / rowH);
    for (size_t i = 0; i < items.size(); ++i) {
        int c = static_cast<int>(i) / std::max(1, rows), row = static_cast<int>(i) % std::max(1, rows);
        if (c >= cols) {
            ctx->setFontColor(dim);
            ctx->drawString(("... ほか " + std::to_string(items.size() - i) + " 個").c_str(),
                            CRect(x + colW, r.bottom - 8 - rowH, r.right - 14, r.bottom - 8), kRightText);
            break;
        }
        ctx->drawString(("  " + items[i]).c_str(), CRect(x + c * colW, y + row * rowH, x + (c + 1) * colW, y + (row + 1) * rowH),
                        kLeftText);
    }
    setDirty(false);
}

StatusEditor::StatusEditor(MpsController* controller) : VSTGUIEditor(controller), controller_(controller) {
    Steinberg::ViewRect vr(0, 0, kW, kH);
    setRect(vr);
}

bool PLUGIN_API StatusEditor::open(void* parent, const PlatformType& platformType) {
    if (frame) return false;
    CRect size(0, 0, kW, kH);
    frame = new CFrame(size, this);
    view_ = new StatusView(size, controller_);
    frame->addView(view_);
    frame->open(parent, platformType);
    controller_->requestStatus();
    timer_ = makeOwned<CVSTGUITimer>(
        [this](CVSTGUITimer*) {
            controller_->requestStatus();
            if (view_) view_->invalid();
        },
        300, true);
    return true;
}

void PLUGIN_API StatusEditor::close() {
    timer_ = nullptr;
    if (frame) {
        frame->forget();
        frame = nullptr;
        view_ = nullptr;
    }
}

}  // namespace mpsvst
