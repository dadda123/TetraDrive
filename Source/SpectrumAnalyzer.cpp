#include "SpectrumAnalyzer.h"

namespace {
constexpr float kFloorDb = -140.0f;
constexpr float kCoherentGain = 0.35875f;   // Blackman-Harris
}  // namespace

SpectrumAnalyzer::SpectrumAnalyzer() {
    for (int o = kMinOrder; o <= kMaxOrder; ++o) {
        const int n = 1 << o;
        ffts_[static_cast<std::size_t>(o)] = std::make_unique<juce::dsp::FFT>(o);
        auto& w = windows_[static_cast<std::size_t>(o)];
        w.resize(static_cast<std::size_t>(n));
        // 4-term Blackman-Harris
        for (int i = 0; i < n; ++i) {
            const double x = 2.0 * juce::MathConstants<double>::pi * i / (n - 1);
            w[static_cast<std::size_t>(i)] = static_cast<float>(0.35875 - 0.48829 * std::cos(x) + 0.14128 * std::cos(2 * x) - 0.01168 * std::cos(3 * x));
        }
    }
}

void SpectrumAnalyzer::push(const float* const* channels, int numChannels, int numSamples) noexcept {
    if (numChannels <= 0 || numSamples <= 0) return;
    const int n = juce::jmin(numSamples, fifo_.getFreeSpace());
    if (n <= 0) return;
    const float scale = 1.0f / static_cast<float>(numChannels);
    const auto w = fifo_.write(n);
    auto copy = [&](int start, int count, int srcOffset) {
        for (int i = 0; i < count; ++i) {
            float s = 0.0f;
            for (int c = 0; c < numChannels; ++c) s += channels[c][srcOffset + i];
            fifoData_[static_cast<std::size_t>(start + i)] = s * scale;
        }
    };
    copy(w.startIndex1, w.blockSize1, 0);
    copy(w.startIndex2, w.blockSize2, w.blockSize1);
}

void SpectrumAnalyzer::setOrder(int order) {
    order_ = juce::jlimit(kMinOrder, kMaxOrder, order);
}

void SpectrumAnalyzer::rebuildMap(const std::vector<float>& freqs) {
    const int n = size();
    const double binHz = sampleRate() / n;
    const int maxBin = n / 2 - 1;
    map_.assign(freqs.size(), {});
    for (std::size_t i = 0; i < freqs.size(); ++i) {
        const float f = freqs[i];
        const float fLo = i > 0 ? std::sqrt(freqs[i - 1] * f) : f / 1.01f;
        const float fHi = i + 1 < freqs.size() ? std::sqrt(f * freqs[i + 1]) : f * 1.01f;
        const int lo = juce::jlimit(1, maxBin, static_cast<int>(std::ceil(fLo / binHz)));
        const int hi = juce::jlimit(1, maxBin, static_cast<int>(std::floor(fHi / binHz)));
        if (hi > lo) {
            map_[i] = {lo, hi, 0.0f, false};   // dense: peak of the bins in this slot
        } else {
            const double b = juce::jlimit(1.0, static_cast<double>(maxBin - 1), f / binHz);
            const int b0 = static_cast<int>(std::floor(b));
            map_[i] = {b0, b0 + 1, static_cast<float>(b - b0), true};   // sparse: interpolate
        }
    }
    mapOrder_ = order_;
    mapRate_ = sampleRate();
    mapPoints_ = freqs.size();
    mapFirst_ = freqs.empty() ? 0.0f : freqs.front();
    mapLast_ = freqs.empty() ? 0.0f : freqs.back();
}

bool SpectrumAnalyzer::update(const std::vector<float>& freqs, std::vector<float>& db, double dtSeconds) {
    // drain FIFO into the history ring
    const int ready = fifo_.getNumReady();
    if (ready > 0) {
        const auto r = fifo_.read(ready);
        auto take = [&](int start, int count) {
            for (int i = 0; i < count; ++i) {
                history_[static_cast<std::size_t>(historyPos_)] = fifoData_[static_cast<std::size_t>(start + i)];
                historyPos_ = (historyPos_ + 1) & (kMaxSize - 1);
            }
        };
        take(r.startIndex1, r.blockSize1);
        take(r.startIndex2, r.blockSize2);
    }

    if (display_.size() != freqs.size()) display_.assign(freqs.size(), kFloorDb);
    const float decay = static_cast<float>(kReleaseDbPerSecond * juce::jlimit(0.0, 0.25, dtSeconds));

    if (ready <= 0) {
        for (auto& d : display_) d = juce::jmax(kFloorDb, d - decay);
        db = display_;
        return false;
    }

    if (mapOrder_ != order_ || mapRate_ != sampleRate() || mapPoints_ != freqs.size() ||
        (!freqs.empty() && (mapFirst_ != freqs.front() || mapLast_ != freqs.back())))
        rebuildMap(freqs);

    // latest N samples, windowed
    const int n = size();
    const auto& win = windows_[static_cast<std::size_t>(order_)];
    int start = (historyPos_ - n + kMaxSize) & (kMaxSize - 1);
    for (int i = 0; i < n; ++i) {
        work_[static_cast<std::size_t>(i)] = history_[static_cast<std::size_t>(start)] * win[static_cast<std::size_t>(i)];
        start = (start + 1) & (kMaxSize - 1);
    }
    std::fill(work_.begin() + n, work_.begin() + 2 * n, 0.0f);
    ffts_[static_cast<std::size_t>(order_)]->performFrequencyOnlyForwardTransform(work_.data(), true);

    const float norm = 2.0f / (static_cast<float>(n) * kCoherentGain);
    for (std::size_t i = 0; i < freqs.size(); ++i) {
        const auto& m = map_[i];
        float mag;
        if (m.interp) {
            mag = work_[static_cast<std::size_t>(m.lo)] + (work_[static_cast<std::size_t>(m.hi)] - work_[static_cast<std::size_t>(m.lo)]) * m.frac;
        } else {
            mag = 0.0f;
            for (int b = m.lo; b <= m.hi; ++b) mag = juce::jmax(mag, work_[static_cast<std::size_t>(b)]);
        }
        const float level = 20.0f * std::log10(mag * norm + 1e-12f) + kTiltDbPerOctave * std::log2(freqs[i] / 1000.0f);
        display_[i] = juce::jmax(level, display_[i] - decay);
    }
    db = display_;
    return true;
}
