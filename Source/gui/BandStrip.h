// Per-band controls (Drive, Level, Solo) and the shared labelled parameter knob.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class TetraDriveProcessor;

namespace tdui {

// Rotary knob bound to a parameter, with a title above and an editable value below.
class ParamKnob : public juce::Component {
public:
    ParamKnob(TetraDriveProcessor&, int paramIndex, const juce::String& title, juce::Colour accent);
    void resized() override;
    void paint(juce::Graphics&) override;
    juce::Slider& slider() { return slider_; }

private:
    juce::String title_;
    juce::Slider slider_;
    juce::AudioProcessorValueTreeState::SliderAttachment attachment_;
};

class BandStrip : public juce::Component {
public:
    BandStrip(TetraDriveProcessor&, int band);
    void paint(juce::Graphics&) override;
    void resized() override;
    // Updates the header range text and solo state.
    void refresh();

private:
    TetraDriveProcessor& proc_;
    int band_;
    juce::String header_, range_;
    ParamKnob drive_, level_;
    juce::TextButton solo_{"Solo"};
};

}  // namespace tdui
