#pragma once
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace mpsvst {

static const Steinberg::FUID kSynthProcessorUID(0xbb398422, 0xbe164b07, 0x89652777, 0x146b9635);
static const Steinberg::FUID kSynthControllerUID(0x3e8703ab, 0x610b474b, 0xb514e61d, 0x5cd5c1ef);
static const Steinberg::FUID kFxProcessorUID(0x18a2e28f, 0x0e6f4f49, 0x9fd33fe9, 0x9b294249);
static const Steinberg::FUID kFxControllerUID(0x228519bb, 0xe0cc4508, 0xb2cdf271, 0x3c9c7e6d);

// プロセッサーとコントローラーの間のメッセージ(属性 "data" にバイナリで UTF-8 を入れる)
constexpr const char* kMsgTrack = "mps.track";          // コントローラー → プロセッサー: トラック名
constexpr const char* kMsgStatusReq = "mps.status?";    // コントローラー → プロセッサー: 状態の問い合わせ
constexpr const char* kMsgStatus = "mps.status";        // プロセッサー → コントローラー: 状態 JSON

}  // namespace mpsvst
