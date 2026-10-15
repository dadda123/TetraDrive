// Parameter spec shared by DSP, processor and GUI; framework independent.
#pragma once

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "td_dsp.h"

namespace td {

enum class Unit { Hz, Decibel, Choice };

struct ParamSpec {
    const char* id;
    const char* name;
    const char* shortName;
    Unit unit;
    float min, max, def, step;
    bool logScale;
    const char* description;
};

enum ParamIndex : int {
    kLowCut, kXover1, kXover2, kXover3, kHighCut,
    kDrive1, kDrive2, kDrive3, kDrive4,
    kLevel1, kLevel2, kLevel3, kLevel4,
    kInput, kOutput, kShape, kSolo, kOversample, kCrossover,
    kNumParams
};

// Default state: the factory "Default" record.
inline constexpr std::array<ParamSpec, kNumParams> kParams{{
    {"lowCut", "Low Cut", "Low Cut", Unit::Hz, 10.0f, 1000.0f, 25.0f, 0.0f, true,
     "Lower edge of the Low band. Cuts rumble below this frequency."},
    {"xover1", "Crossover 1", "X-Over 1", Unit::Hz, 20.0f, 20000.0f, 136.24f, 0.0f, true,
     "Split point between the Low and Low Mid bands."},
    {"xover2", "Crossover 2", "X-Over 2", Unit::Hz, 20.0f, 20000.0f, 742.46f, 0.0f, true,
     "Split point between the Low Mid and High Mid bands."},
    {"xover3", "Crossover 3", "X-Over 3", Unit::Hz, 20.0f, 20000.0f, 4046.1f, 0.0f, true,
     "Split point between the High Mid and High bands."},
    {"highCut", "High Cut", "High Cut", Unit::Hz, 1000.0f, 22050.0f, 22050.0f, 0.0f, true,
     "Upper edge of the High band. Cuts fizz above this frequency."},
    {"drive1", "Low Drive", "Drive", Unit::Decibel, -24.0f, 24.0f, 0.0f, 0.1f, false,
     "Gain into the Low band's waveshaper. More = more distortion."},
    {"drive2", "Low Mid Drive", "Drive", Unit::Decibel, -24.0f, 24.0f, 0.0f, 0.1f, false,
     "Gain into the Low Mid band's waveshaper. More = more distortion."},
    {"drive3", "High Mid Drive", "Drive", Unit::Decibel, -24.0f, 24.0f, 0.0f, 0.1f, false,
     "Gain into the High Mid band's waveshaper. More = more distortion."},
    {"drive4", "High Drive", "Drive", Unit::Decibel, -24.0f, 24.0f, 0.0f, 0.1f, false,
     "Gain into the High band's waveshaper. More = more distortion."},
    {"level1", "Low Level", "Level", Unit::Decibel, -24.0f, 24.0f, 0.0f, 0.1f, false,
     "Output level of the Low band after distortion."},
    {"level2", "Low Mid Level", "Level", Unit::Decibel, -24.0f, 24.0f, 0.0f, 0.1f, false,
     "Output level of the Low Mid band after distortion."},
    {"level3", "High Mid Level", "Level", Unit::Decibel, -24.0f, 24.0f, 0.0f, 0.1f, false,
     "Output level of the High Mid band after distortion."},
    {"level4", "High Level", "Level", Unit::Decibel, -24.0f, 24.0f, 0.0f, 0.1f, false,
     "Output level of the High band after distortion."},
    {"input", "Input", "Input", Unit::Decibel, -20.0f, 20.0f, 0.0f, 0.1f, false,
     "Gain into all bands before distortion. Also drives harder."},
    {"output", "Output", "Output", Unit::Decibel, -20.0f, 20.0f, 0.0f, 0.1f, false,
     "Final output level after all bands are summed."},
    {"shape", "Shape", "Shape", Unit::Choice, 0.0f, 4.0f, 0.0f, 1.0f, false,
     "Waveshaper curve used by every band."},
    {"solo", "Solo", "Solo", Unit::Choice, 0.0f, 4.0f, 0.0f, 1.0f, false,
     "Listen to one band on its own."},
    {"oversample", "Oversampling", "OS", Unit::Choice, 0.0f, 3.0f, 1.0f, 1.0f, false,
     "Runs the waveshapers at a higher rate to reduce aliasing. Higher settings cost more CPU and add latency, which is reported to the host."},
    {"crossover", "Crossover", "X-Over", Unit::Choice, 0.0f, 1.0f, 0.0f, 1.0f, false,
     "Classic splits the bands like the original hardware, with a little ripple where they meet. Flat uses Linkwitz-Riley filters so the bands add back to a flat response."},
}};

inline constexpr std::array<const char*, kShapeCount> kShapeNames{{"Shape 1", "Shape 2", "Shape 3", "Shape 4", "Shape 5"}};
inline constexpr std::array<const char*, kBands + 1> kSoloNames{{"Off", "Low", "Low Mid", "High Mid", "High"}};
inline constexpr std::array<const char*, kMaxOsStages + 1> kOversampleNames{{"Off", "2x", "4x", "8x"}};
inline constexpr std::array<const char*, 2> kCrossoverNames{{"Classic", "Flat"}};
inline constexpr std::array<const char*, kBands> kBandNames{{"Low", "Low Mid", "High Mid", "High"}};

inline int driveParam(int band) { return kDrive1 + band; }
inline int levelParam(int band) { return kLevel1 + band; }
inline int edgeParam(int edge) { return kLowCut + edge; }

// "136 Hz", "4.05 kHz".
inline std::string formatHz(float hz) {
    char buf[32];
    if (hz < 1000.0f) std::snprintf(buf, sizeof buf, "%.0f Hz", static_cast<double>(hz));
    else if (hz < 10000.0f) std::snprintf(buf, sizeof buf, "%.2f kHz", static_cast<double>(hz) / 1000.0);
    else std::snprintf(buf, sizeof buf, "%.1f kHz", static_cast<double>(hz) / 1000.0);
    return buf;
}

// "+6.2 dB", "-6.2 dB", "0.0 dB".
inline std::string formatDb(float db) {
    char buf[32];
    const double v = std::round(static_cast<double>(db) * 10.0) / 10.0;
    if (v == 0.0) return "0.0 dB";
    std::snprintf(buf, sizeof buf, "%+.1f dB", v);
    return buf;
}

// Formats a real value with its unit.
inline std::string formatValue(int index, float value) {
    const auto& p = kParams[static_cast<std::size_t>(index)];
    switch (p.unit) {
        case Unit::Hz: return formatHz(value);
        case Unit::Decibel: return formatDb(value);
        case Unit::Choice: {
            const int i = static_cast<int>(std::lround(value));
            if (index == kShape && i >= 0 && i < kShapeCount) return kShapeNames[static_cast<std::size_t>(i)];
            if (index == kSolo && i >= 0 && i <= kBands) return kSoloNames[static_cast<std::size_t>(i)];
            if (index == kOversample && i >= 0 && i <= kMaxOsStages) return kOversampleNames[static_cast<std::size_t>(i)];
            if (index == kCrossover && i >= 0 && i <= 1) return kCrossoverNames[static_cast<std::size_t>(i)];
            return std::to_string(i);
        }
    }
    return {};
}

// Parses "742", "742 Hz", "4.05k", "4.05 kHz", "-6.2", "-6.2 dB".
inline float parseValue(int index, const std::string& text) {
    const auto& p = kParams[static_cast<std::size_t>(index)];
    double v = std::atof(text.c_str());
    if (p.unit == Unit::Hz && text.find_first_of("kK") != std::string::npos) v *= 1000.0;
    if (v < p.min) v = p.min;
    if (v > p.max) v = p.max;
    return static_cast<float>(v);
}

}  // namespace td
