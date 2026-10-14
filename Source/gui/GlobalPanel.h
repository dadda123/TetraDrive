// Global controls: Input, Shape (with curve preview), Output and Bypass.
#pragma once

#include "BandStrip.h"

namespace tdui {

class GlobalPanel : public juce::Component {
public:
    explicit GlobalPanel(TetraDriveProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    ParamKnob input_, shape_, output_;
    juce::DrawableButton bypass_{"Bypass", juce::DrawableButton::ImageFitted};
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment_;
};

}  // namespace tdui
