// Global controls: Input, Shape (with curve preview), Output, Bypass, Crossover mode, Oversampling, I/O meters.
#pragma once

#include <array>

#include "BandStrip.h"
#include "MeterDisplay.h"

namespace tdui {

class GlobalPanel : public juce::Component {
public:
    GlobalPanel(TetraDriveProcessor&, const MeterDisplay&);
    void paint(juce::Graphics&) override;
    void resized() override;

    // Syncs the choice buttons with their parameters and redraws the meters (editor timer).
    void refresh();

private:
    void paintMeter(juce::Graphics&, juce::Rectangle<float>, const std::array<MeterDisplay::Channel, 2>&) const;
    void showChoiceMenu(int param, juce::TextButton& anchor);

    TetraDriveProcessor& proc_;
    const MeterDisplay& meters_;
    ParamKnob input_, shape_, output_;
    juce::TextButton crossover_, oversample_;
    juce::Rectangle<int> inMeter_, outMeter_;
    juce::DrawableButton bypass_{"Bypass", juce::DrawableButton::ImageFitted};
    juce::AudioProcessorValueTreeState::ButtonAttachment bypassAttachment_;
};

}  // namespace tdui
