#include "params.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace mps {

static double clamp01(double v) { return std::min(1.0, std::max(0.0, v)); }

double clampPlain(const ParamDef& d, double plain) {
    double v = std::min(d.max, std::max(d.min, plain));
    if (d.scale == Scale::Int || d.scale == Scale::Enum || d.scale == Scale::Bool) v = std::round(v);
    return v;
}

double toPlain(const ParamDef& d, double norm) {
    norm = clamp01(norm);
    switch (d.scale) {
        case Scale::Linear: return d.min + (d.max - d.min) * norm;
        case Scale::Cubic: return d.min + (d.max - d.min) * norm * norm * norm;
        case Scale::Log: return d.min * std::pow(d.max / d.min, norm);
        case Scale::Int:
        case Scale::Enum:
        case Scale::Bool: return clampPlain(d, d.min + (d.max - d.min) * norm);
    }
    return d.min;
}

double toNorm(const ParamDef& d, double plain) {
    plain = clampPlain(d, plain);
    if (d.max == d.min) return 0.0;
    switch (d.scale) {
        case Scale::Linear:
        case Scale::Int:
        case Scale::Enum:
        case Scale::Bool: return clamp01((plain - d.min) / (d.max - d.min));
        case Scale::Cubic: return clamp01(std::cbrt((plain - d.min) / (d.max - d.min)));
        case Scale::Log: return clamp01(std::log(plain / d.min) / std::log(d.max / d.min));
    }
    return 0.0;
}

int stepCount(const ParamDef& d) {
    if (d.scale == Scale::Int || d.scale == Scale::Enum || d.scale == Scale::Bool)
        return static_cast<int>(std::round(d.max - d.min));
    return 0;
}

std::string formatValue(const ParamDef& d, double plain) {
    char buf[64];
    if (d.scale == Scale::Enum) {
        int i = static_cast<int>(std::round(plain - d.min));
        if (i >= 0 && i < static_cast<int>(d.labels.size())) return d.labels[i];
    }
    if (d.scale == Scale::Bool) return plain >= 0.5 ? "on" : "off";
    if (d.scale == Scale::Int) {
        std::snprintf(buf, sizeof buf, "%d", static_cast<int>(std::round(plain)));
        return buf;
    }
    double a = std::fabs(plain);
    if (a >= 1000) std::snprintf(buf, sizeof buf, "%.0f", plain);
    else if (a >= 100) std::snprintf(buf, sizeof buf, "%.1f", plain);
    else if (a >= 1) std::snprintf(buf, sizeof buf, "%.3f", plain);
    else std::snprintf(buf, sizeof buf, "%.4f", plain);
    return buf;
}

bool parseValue(const ParamDef& d, const std::string& text, double& plain) {
    if (d.scale == Scale::Enum) {
        for (size_t i = 0; i < d.labels.size(); ++i)
            if (text == d.labels[i]) {
                plain = d.min + static_cast<double>(i);
                return true;
            }
    }
    if (d.scale == Scale::Bool) {
        if (text == "on" || text == "true") { plain = 1; return true; }
        if (text == "off" || text == "false") { plain = 0; return true; }
    }
    char* end = nullptr;
    double v = std::strtod(text.c_str(), &end);
    if (end == text.c_str()) return false;
    plain = clampPlain(d, v);
    return true;
}

const char* scaleName(Scale s) {
    switch (s) {
        case Scale::Linear: return "linear";
        case Scale::Cubic: return "cubic";
        case Scale::Log: return "log";
        case Scale::Int: return "int";
        case Scale::Enum: return "enum";
        case Scale::Bool: return "bool";
    }
    return "?";
}

}  // namespace mps
