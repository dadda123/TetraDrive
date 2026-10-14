// User presets as XML files; "Default" is built in. Message thread only.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

#include "td_params.h"

class PresetManager {
public:
    static constexpr const char* kDefaultName = "Default";
    static constexpr const char* kExtension = ".tdpreset";

    explicit PresetManager(juce::AudioProcessorValueTreeState& apvts);

    // Folder for user presets; created on demand.
    static juce::File presetDirectory();

    juce::StringArray names() const;
    bool isUserPreset(const juce::String& name) const;
    bool exists(const juce::String& name) const;

    bool load(const juce::String& name);
    bool save(const juce::String& name);
    bool remove(const juce::String& name);
    void loadNext(int direction);

    const juce::String& currentName() const { return current_; }
    // Restores the name after a session load and treats current values as unmodified.
    void restoreCurrent(const juce::String& name);
    bool isModified() const;

    // Version that wrote the preset; for future migrations.
    static constexpr int kFormatVersion = 1;

private:
    static bool includedInPresets(int index) { return index != td::kSolo; }
    juce::File fileFor(const juce::String& name) const;
    void applyReal(const std::array<float, td::kNumParams>& values);
    std::array<float, td::kNumParams> currentReal() const;

    juce::AudioProcessorValueTreeState& apvts_;
    juce::String current_{kDefaultName};
    std::array<float, td::kNumParams> snapshot_{};
};
