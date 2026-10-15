// Input spectrum: audio thread writes a lock-free FIFO, GUI thread runs the FFT at display rate.
#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

class SpectrumAnalyzer {
public:
    static constexpr int kMinOrder = 12, kMaxOrder = 14;   // 4096 .. 16384
    static constexpr int kMaxSize = 1 << kMaxOrder;
    static constexpr float kTiltDbPerOctave = 3.0f;
    static constexpr float kReleaseDbPerSecond = 20.0f;

    SpectrumAnalyzer();

    // Any thread before playback.
    void prepare(double sampleRate) { sampleRate_.store(sampleRate); }
    double sampleRate() const { return sampleRate_.load(); }

    // Audio thread: mono sum of the incoming signal; drops samples when full.
    void push(const float* const* channels, int numChannels, int numSamples) noexcept;

    // GUI thread: FFT size (order 12..14).
    void setOrder(int order);
    int order() const { return order_; }
    int size() const { return 1 << order_; }

    // GUI thread: drains the FIFO, analyses the latest N samples and updates dB values on freqs.
    // Returns true when new audio arrived.
    bool update(const std::vector<float>& freqs, std::vector<float>& db, double dtSeconds);

    // GUI thread: keeps the history current while this analyzer is not displayed.
    void drain() { drainFifo(); }

    void resetDisplay() { display_.clear(); }

private:
    int drainFifo();
    void rebuildMap(const std::vector<float>& freqs);

    std::atomic<double> sampleRate_{44100.0};

    // FIFO shared with the audio thread.
    juce::AbstractFifo fifo_{1 << 15};
    std::vector<float> fifoData_ = std::vector<float>(1 << 15, 0.0f);

    // GUI-thread state.
    std::vector<float> history_ = std::vector<float>(kMaxSize, 0.0f);
    int historyPos_ = 0;
    int order_ = 13;
    std::array<std::unique_ptr<juce::dsp::FFT>, kMaxOrder + 1> ffts_;
    std::array<std::vector<float>, kMaxOrder + 1> windows_;
    std::vector<float> work_ = std::vector<float>(2 * kMaxSize, 0.0f);
    std::vector<float> display_;

    // Grid point -> bin mapping, cached per order, rate and grid.
    struct MapEntry { int lo, hi; float frac; bool interp; };
    std::vector<MapEntry> map_;
    int mapOrder_ = -1;
    double mapRate_ = 0.0;
    std::size_t mapPoints_ = 0;
    float mapFirst_ = 0.0f, mapLast_ = 0.0f;
};
