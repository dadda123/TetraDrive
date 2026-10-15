// TetraDrive audio processor: parameters, sample-accurate event splitting, state and analyzer feed.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

#include "LiveMeters.h"
#include "PresetManager.h"
#include "SampleAccurateEvents.h"
#include "SpectrumAnalyzer.h"
#include "td_dsp.h"
#include "td_params.h"

class TetraDriveProcessor : public juce::AudioProcessor,
                            public juce::SampleAccurateParameterSink,
                            private juce::AsyncUpdater {
public:
    TetraDriveProcessor();
    ~TetraDriveProcessor() override { cancelPendingUpdate(); }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    void addParameterPoint(juce::AudioProcessorParameter& parameter, int sampleOffset, float value) noexcept override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override { return bypass_; }

    juce::RangedAudioParameter& param(int index) const { return *params_[static_cast<std::size_t>(index)]; }
    float realValue(int index) const { return params_[static_cast<std::size_t>(index)]->convertFrom0to1(params_[static_cast<std::size_t>(index)]->getValue()); }
    void setRealValue(int index, float real);
    double currentSampleRate() const { return sampleRate_.load(); }

    juce::AudioProcessorValueTreeState apvts;
    SpectrumAnalyzer analyzer;      // input, before processing
    SpectrumAnalyzer analyzerOut;   // output, after processing
    LiveMeters meters;
    PresetManager presets;

    enum AnalyzerSource : int { kAnalyzeInput = 0, kAnalyzeOutput = 1, kAnalyzeBoth = 2 };

    // Editor settings stored with the session.
    struct UiState {
        std::atomic<int> width{1000}, height{640};
        std::atomic<int> fftOrder{13};
        std::atomic<bool> analyzerOn{true};
        std::atomic<int> analyzerSource{kAnalyzeBoth};
        std::atomic<bool> helpBar{true};
    } ui;

    static juce::String versionString();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    void applyParam(int index, float normalised, int rampSamples);
    void pushEdges(int rampSamples);
    void processInternal(juce::AudioBuffer<float>&, bool forceBypass);
    static void pushChannelPeaks(std::array<PeakAtomic, td::kMaxChannels>&, const juce::AudioBuffer<float>&, int channels, int n);
    void syncAllParams(bool snap);
    // Latency changes with the oversampling factor; the host is told from the message thread.
    void handleAsyncUpdate() override { setLatencySamples(engine_.latencySamples()); }

    td::Engine engine_;
    std::array<juce::RangedAudioParameter*, td::kNumParams> params_{};
    juce::AudioParameterBool* bypass_ = nullptr;
    static constexpr int kBypassIndex = td::kNumParams;

    SampleAccurateEvents events_;
    std::array<float, td::kNumParams + 1> applied_{};      // last normalised value given to the engine
    std::array<float, td::kBands + 1> edgeTargets_{};      // Hz
    std::array<bool, td::kNumParams + 1> hasEvents_{};
    int soloBand_ = -1;
    bool bypassed_ = false;
    bool forcedBypass_ = false;
    std::atomic<double> sampleRate_{44100.0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TetraDriveProcessor)
};
