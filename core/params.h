// パラメータ定義(VST3・操作サーバー・オフライン書き出しで共通)。
// 値は「プレーン値」(Hz、秒、半音など実際の単位)でやり取りし、VST3 の 0〜1 への変換だけをここで行う。
#pragma once
#include <string>
#include <vector>

namespace mps {

enum class Scale { Linear, Cubic, Log, Int, Enum, Bool };

struct ParamDef {
    const char* key;   // 操作サーバー/状態 JSON のキー(変えるときは PluginSpec::find に古いキーを残す)
    const char* name;  // DAW に表示する名前
    const char* unit;
    Scale scale;
    double min, max, def;
    std::vector<const char*> labels;  // Enum のときの選択肢
    const char* help = "";
    bool hidden = false;
};

double toPlain(const ParamDef& d, double norm);
double toNorm(const ParamDef& d, double plain);
double clampPlain(const ParamDef& d, double plain);
int stepCount(const ParamDef& d);
std::string formatValue(const ParamDef& d, double plain);
// 数値文字列か Enum のラベルを受け付ける
bool parseValue(const ParamDef& d, const std::string& text, double& plain);
const char* scaleName(Scale s);

}  // namespace mps
