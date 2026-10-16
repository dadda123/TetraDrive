// Peak meters shared between the audio thread (push) and the GUI (take-and-clear); lock free.
#pragma once

#include <array>
#include <atomic>

#include "td_dsp.h"

struct PeakAtomic {
    std::atomic<float> value{0.0f};

    // Audio thread. The loop only repeats when the GUI's take() lands between load and store.
    void push(float v) noexcept {
        float cur = value.load(std::memory_order_relaxed);
        while (v > cur && !value.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
    }
    // GUI thread: peak since the previous take.
    float take() noexcept { return value.exchange(0.0f, std::memory_order_relaxed); }
};

struct LiveMeters {
    std::array<PeakAtomic, td::kBands> bandHeat, bandOut;   // heat: see td::BandMeters
    std::array<PeakAtomic, td::kMaxChannels> input, output;
};
