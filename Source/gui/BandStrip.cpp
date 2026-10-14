#include "BandStrip.h"

#include "../PluginProcessor.h"
#include "HelpBar.h"
#include "TetraLookAndFeel.h"

namespace tdui {

ParamKnob::ParamKnob(TetraDriveProcessor& p, int paramIndex, const juce::String& title, juce::Colour accent)
    : title_(title), attachment_(p.apvts, td::kParams[static_cast<std::size_t>(paramIndex)].id, slider_) {
    const auto& spec = td::kParams[static_cast<std::size_t>(paramIndex)];
    slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    slider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    slider_.setTextBoxIsEditable(true);
    slider_.setColour(juce::Slider::rotarySliderFillColourId, accent);
    if (spec.unit == td::Unit::Decibel) slider_.getProperties().set("origin", static_cast<double>(spec.def));
    slider_.setDoubleClickReturnValue(true, static_cast<double>(spec.def));
    slider_.setRotaryParameters(juce::degreesToRadians(225.0f), juce::degreesToRadians(495.0f), true);
    setHelpParam(slider_, paramIndex);
    addAndMakeVisible(slider_);
}

void ParamKnob::resized() {
    const float s = scaleOf(*this);
    auto r = getLocalBounds();
    r.removeFromTop(static_cast<int>(16.0f * s));
    slider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, r.getWidth(), static_cast<int>(18.0f * s));
    slider_.setBounds(r);
}

void ParamKnob::paint(juce::Graphics& g) {
    const float s = scaleOf(*this);
    g.setColour(colours::textSecondary);
    g.setFont(TetraLookAndFeel::font(11.5f * s, Weight::Medium));
    g.drawText(title_, getLocalBounds().removeFromTop(static_cast<int>(16.0f * s)), juce::Justification::centred);
}

BandStrip::BandStrip(TetraDriveProcessor& p, int band)
    : proc_(p),
      band_(band),
      drive_(p, td::driveParam(band), "Drive", colours::bands[static_cast<std::size_t>(band)]),
      level_(p, td::levelParam(band), "Level", colours::bands[static_cast<std::size_t>(band)]) {
    header_ = td::kBandNames[static_cast<std::size_t>(band)];
    addAndMakeVisible(drive_);
    addAndMakeVisible(level_);
    solo_.setClickingTogglesState(false);
    solo_.getProperties().set("accent", static_cast<juce::int64>(colours::bands[static_cast<std::size_t>(band)].getARGB()));
    describe(solo_, "Solo " + juce::String(td::kBandNames[static_cast<std::size_t>(band)]), "Listen to this band on its own. Click again to hear all bands.");
    solo_.onClick = [this] {
        const int current = static_cast<int>(std::lround(proc_.realValue(td::kSolo)));
        proc_.setRealValue(td::kSolo, current == band_ + 1 ? 0.0f : static_cast<float>(band_ + 1));
        refresh();
    };
    addAndMakeVisible(solo_);
    refresh();
}

void BandStrip::refresh() {
    std::array<float, td::kBands + 1> e{};
    for (int i = 0; i <= td::kBands; ++i) e[static_cast<std::size_t>(i)] = proc_.realValue(td::edgeParam(i));
    std::sort(e.begin(), e.end());
    const juce::String range = juce::String(td::formatHz(e[static_cast<std::size_t>(band_)])) + " - " +
                               juce::String(td::formatHz(e[static_cast<std::size_t>(band_ + 1)]));
    if (range != range_) { range_ = range; repaint(); }
    const bool soloed = static_cast<int>(std::lround(proc_.realValue(td::kSolo))) == band_ + 1;
    solo_.setToggleState(soloed, juce::dontSendNotification);
}

void BandStrip::paint(juce::Graphics& g) {
    const float s = scaleOf(*this);
    auto r = getLocalBounds().toFloat();
    g.setColour(colours::panel);
    g.fillRoundedRectangle(r, 6.0f * s);
    const auto colour = colours::bands[static_cast<std::size_t>(band_)];
    g.setColour(colour);
    g.fillRoundedRectangle(r.removeFromTop(3.0f * s).reduced(10.0f * s, 0.0f), 1.5f * s);

    auto head = getLocalBounds().reduced(static_cast<int>(10.0f * s), 0).withTrimmedTop(static_cast<int>(8.0f * s)).removeFromTop(static_cast<int>(20.0f * s));
    juce::AttributedString text;
    text.setJustification(juce::Justification::centredLeft);
    text.append(header_, TetraLookAndFeel::font(13.0f * s, Weight::SemiBold), colours::textPrimary);
    text.append(juce::String(juce::CharPointer_UTF8("  \xc2\xb7  ")), TetraLookAndFeel::font(13.0f * s), colours::textSecondary);
    text.append(range_, TetraLookAndFeel::font(12.0f * s), colours::textSecondary);
    text.setWordWrap(juce::AttributedString::none);
    text.draw(g, head.toFloat());
}

void BandStrip::resized() {
    const float s = scaleOf(*this);
    auto r = getLocalBounds().reduced(static_cast<int>(8.0f * s));
    r.removeFromTop(static_cast<int>(26.0f * s));
    solo_.setBounds(r.removeFromBottom(static_cast<int>(24.0f * s)).reduced(static_cast<int>(12.0f * s), 0));
    r.removeFromBottom(static_cast<int>(6.0f * s));
    drive_.setBounds(r.removeFromLeft(r.getWidth() / 2));
    level_.setBounds(r);
}

}  // namespace tdui
