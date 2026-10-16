// Per-band controls (Drive, Level, Solo) and the shared labelled parameter knob.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class TetraDriveProcessor;

namespace tdui {

class MeterDisplay;

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

// Lamp beside the Drive knob: dark when the band passes through clean, glowing in the band colour
// as the shaper bends it, white-hot when the band is mostly distortion.
class SaturationLed : public juce::Component {
public:
    explicit SaturationLed(juce::Colour accent);
    void setHeat(float heat01);
    void paint(juce::Graphics&) override;
    // The bounds include room for the corona; only the lens responds to the mouse.
    bool hitTest(int x, int y) override;

private:
    float lensRadius() const;
    juce::Colour accent_;
    float heat_ = 0.0f;
    int shown_ = -1;
};

class BandStrip : public juce::Component {
public:
    BandStrip(TetraDriveProcessor&, int band, const MeterDisplay&);
    void paint(juce::Graphics&) override;
    void resized() override;
    // Updates the header range text, solo state, level bar and saturation lamp.
    void refresh();

private:
    juce::Rectangle<float> meterBar() const;

    TetraDriveProcessor& proc_;
    int band_;
    const MeterDisplay& meters_;
    juce::String header_, range_;
    ParamKnob drive_, level_;
    SaturationLed led_;
    juce::TextButton solo_{"Solo"};
    int shownLevel_ = -1;   // quantised bar length last painted
};

}  // namespace tdui
