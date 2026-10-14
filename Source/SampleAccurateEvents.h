// RT-safe per-block list of parameter automation points with sample offsets.
#pragma once

#include <array>

struct ParamEvent {
    int offset = 0;
    int param = 0;
    float normalised = 0.0f;
};

class SampleAccurateEvents {
public:
    static constexpr int kCapacity = 4096;

    void clear() noexcept { count_ = 0; }

    // Drops points beyond capacity; the block-start value still covers them.
    void add(int offset, int param, float normalised) noexcept {
        if (count_ < kCapacity) events_[static_cast<std::size_t>(count_++)] = {offset, param, normalised};
    }

    // Stable insertion sort by offset (no allocation; lists are short and mostly sorted).
    void sort() noexcept {
        for (int i = 1; i < count_; ++i) {
            const ParamEvent e = events_[static_cast<std::size_t>(i)];
            int j = i - 1;
            while (j >= 0 && events_[static_cast<std::size_t>(j)].offset > e.offset) {
                events_[static_cast<std::size_t>(j + 1)] = events_[static_cast<std::size_t>(j)];
                --j;
            }
            events_[static_cast<std::size_t>(j + 1)] = e;
        }
    }

    int size() const noexcept { return count_; }
    const ParamEvent& operator[](int i) const noexcept { return events_[static_cast<std::size_t>(i)]; }

private:
    std::array<ParamEvent, kCapacity> events_{};
    int count_ = 0;
};
