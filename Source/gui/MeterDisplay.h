// GUI-side meter ballistics: drains LiveMeters once per editor tick into smoothed dB values.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

#include "../LiveMeters.h"

namespace tdui {

class MeterDisplay {
public:
    static constexpr float kFloorDb = -60.0f;
    // Heat is distortion energy relative to the clean band: below -36 dB it is inaudible,
    // around -4 dB the band is as much distortion as signal.
    static constexpr float kHeatFloorDb = -36.0f, kHeatCeilDb = -4.0f;

    struct Channel {
        float db = kFloorDb, holdDb = kFloorDb;
        double holdAge = 0.0, clipAge = 10.0;
        bool clipped() const { return clipAge < kClipShowSeconds; }
    };

    void update(LiveMeters& live, double dt) {
        dt = juce::jlimit(0.0, 0.25, dt);
        const float fall = static_cast<float>(kFallDbPerSecond * dt);
        const float heatFall = static_cast<float>(kHeatFallDbPerSecond * dt);
        for (std::size_t b = 0; b < td::kBands; ++b) {
            bandOutDb[b] = juce::jmax(toDb(live.bandOut[b].take()), bandOutDb[b] - fall, kFloorDb);
            const float heatDb = 10.0f * std::log10(live.bandHeat[b].take() + 1e-9f);   // energy ratio
            bandHeatDb[b] = juce::jmax(heatDb, bandHeatDb[b] - heatFall, kHeatFloorDb);
        }
        for (std::size_t c = 0; c < td::kMaxChannels; ++c) {
            updateChannel(input[c], live.input[c].take(), fall, dt);
            updateChannel(output[c], live.output[c].take(), fall, dt);
        }
    }

    // 0..1 for drawing: how much the shaper is changing the band.
    float bandSaturation(int b) const {
        return juce::jlimit(0.0f, 1.0f, (bandHeatDb[static_cast<std::size_t>(b)] - kHeatFloorDb) / (kHeatCeilDb - kHeatFloorDb));
    }
    // 0..1 for drawing a level between the floor and 0 dBFS.
    static float fraction(float db) { return juce::jlimit(0.0f, 1.0f, (db - kFloorDb) / -kFloorDb); }

    std::array<float, td::kBands> bandOutDb{kFloorDb, kFloorDb, kFloorDb, kFloorDb};
    std::array<float, td::kBands> bandHeatDb{kHeatFloorDb, kHeatFloorDb, kHeatFloorDb, kHeatFloorDb};
    std::array<Channel, td::kMaxChannels> input{}, output{};

private:
    static constexpr double kFallDbPerSecond = 40.0;
    static constexpr double kHeatFallDbPerSecond = 50.0;
    static constexpr double kHoldSeconds = 1.5;
    static constexpr double kHoldFallDbPerSecond = 20.0;
    static constexpr double kClipShowSeconds = 1.0;

    static float toDb(float gain) { return juce::Decibels::gainToDecibels(gain, kFloorDb - 1.0f); }

    static void updateChannel(Channel& ch, float peak, float fall, double dt) {
        const float db = toDb(peak);
        ch.db = juce::jmax(db, ch.db - fall, kFloorDb);
        if (db >= ch.holdDb) { ch.holdDb = db; ch.holdAge = 0.0; }
        else {
            ch.holdAge += dt;
            if (ch.holdAge > kHoldSeconds) ch.holdDb = juce::jmax(ch.db, ch.holdDb - static_cast<float>(kHoldFallDbPerSecond * dt));
        }
        ch.clipAge = peak > 1.0f ? 0.0 : ch.clipAge + dt;
    }
};

}  // namespace tdui
