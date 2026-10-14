#include "GlobalPanel.h"

#include "../PluginProcessor.h"
#include "HelpBar.h"
#include "TetraLookAndFeel.h"

namespace tdui {

GlobalPanel::GlobalPanel(TetraDriveProcessor& p)
    : input_(p, td::kInput, "Input", colours::textPrimary),
      shape_(p, td::kShape, "Shape", colours::textPrimary),
      output_(p, td::kOutput, "Output", colours::textPrimary),
      bypassAttachment_(p.apvts, "bypass", bypass_) {
    shape_.slider().getProperties().set("shapePreview", true);
    addAndMakeVisible(input_);
    addAndMakeVisible(shape_);
    addAndMakeVisible(output_);

    // power icon: lit when the plug-in is active
    auto off = TetraLookAndFeel::icon("power_svg", colours::textPrimary);
    auto on = TetraLookAndFeel::icon("power_svg", colours::textSecondary.withAlpha(0.6f));
    bypass_.setImages(off.get(), nullptr, nullptr, nullptr, on.get());
    bypass_.setClickingTogglesState(true);
    bypass_.setColour(juce::DrawableButton::backgroundColourId, juce::Colours::transparentBlack);
    bypass_.setColour(juce::DrawableButton::backgroundOnColourId, juce::Colours::transparentBlack);
    describe(bypass_, "Bypass", "Pass the dry signal through. Switching is a click-free crossfade.");
    addAndMakeVisible(bypass_);
}

void GlobalPanel::paint(juce::Graphics& g) {
    const float s = scaleOf(*this);
    g.setColour(colours::panel);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f * s);
    g.setColour(colours::textPrimary);
    g.setFont(TetraLookAndFeel::font(13.0f * s, Weight::SemiBold));
    g.drawText("Global", getLocalBounds().reduced(static_cast<int>(10.0f * s), 0).withTrimmedTop(static_cast<int>(8.0f * s)).removeFromTop(static_cast<int>(20.0f * s)),
               juce::Justification::centredLeft);
}

void GlobalPanel::resized() {
    const float s = scaleOf(*this);
    auto r = getLocalBounds().reduced(static_cast<int>(8.0f * s));
    auto head = r.removeFromTop(static_cast<int>(26.0f * s));
    bypass_.setBounds(head.removeFromRight(static_cast<int>(24.0f * s)).withSizeKeepingCentre(static_cast<int>(20.0f * s), static_cast<int>(20.0f * s)));
    r.removeFromBottom(static_cast<int>(30.0f * s));
    const int w = r.getWidth() / 3;
    input_.setBounds(r.removeFromLeft(w));
    shape_.setBounds(r.removeFromLeft(w));
    output_.setBounds(r);
}

}  // namespace tdui
