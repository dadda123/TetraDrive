#include "GlobalPanel.h"

#include "../PluginProcessor.h"
#include "HelpBar.h"
#include "TetraLookAndFeel.h"

namespace tdui {

namespace {
const char* choicePrefix(int param) { return param == td::kCrossover ? "Crossover: " : "OS: "; }
}  // namespace

GlobalPanel::GlobalPanel(TetraDriveProcessor& p, const MeterDisplay& meters)
    : proc_(p),
      meters_(meters),
      input_(p, td::kInput, "Input", colours::textPrimary),
      shape_(p, td::kShape, "Shape", colours::textPrimary),
      output_(p, td::kOutput, "Output", colours::textPrimary),
      bypassAttachment_(p.apvts, "bypass", bypass_) {
    shape_.slider().getProperties().set("shapePreview", true);
    addAndMakeVisible(input_);
    addAndMakeVisible(shape_);
    addAndMakeVisible(output_);

    for (auto choice : {std::pair{&crossover_, td::kCrossover}, std::pair{&oversample_, td::kOversample}}) {
        auto* button = choice.first;
        const int param = choice.second;
        button->setClickingTogglesState(false);
        setHelpParam(*button, param);
        button->onClick = [this, button, param] { showChoiceMenu(param, *button); };
        addAndMakeVisible(*button);
    }
    refresh();

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

void GlobalPanel::refresh() {
    for (auto [button, param] : {std::pair{&crossover_, td::kCrossover}, std::pair{&oversample_, td::kOversample}}) {
        const auto text = juce::String(choicePrefix(param)) + td::formatValue(param, proc_.realValue(param));
        if (button->getButtonText() != text) button->setButtonText(text);
    }
    repaint(inMeter_.expanded(1));
    repaint(outMeter_.expanded(1));
}

void GlobalPanel::showChoiceMenu(int param, juce::TextButton& anchor) {
    const auto& spec = td::kParams[static_cast<std::size_t>(param)];
    const int current = static_cast<int>(std::lround(proc_.realValue(param)));
    juce::PopupMenu m;
    m.addSectionHeader(spec.name);
    for (int i = static_cast<int>(spec.min); i <= static_cast<int>(spec.max); ++i)
        m.addItem(juce::PopupMenu::Item(td::formatValue(param, static_cast<float>(i))).setTicked(i == current).setAction([this, param, i] {
            proc_.setRealValue(param, static_cast<float>(i));
            refresh();
        }));
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&anchor));
}

// Stereo peak meter: level bar, peak-hold line, clip cap; -60 dBFS at the bottom, 0 at the top.
void GlobalPanel::paintMeter(juce::Graphics& g, juce::Rectangle<float> area, const std::array<MeterDisplay::Channel, 2>& ch) const {
    const float s = scaleOf(const_cast<GlobalPanel&>(*this));
    const float gap = 1.0f * s;
    const float w = (area.getWidth() - gap) * 0.5f;
    for (std::size_t c = 0; c < 2; ++c) {
        auto bar = area.withWidth(w).withX(area.getX() + static_cast<float>(c) * (w + gap));
        g.setColour(colours::outline);
        g.fillRoundedRectangle(bar, 1.0f * s);
        const float level = MeterDisplay::fraction(ch[c].db);
        if (level > 0.0f) {
            auto fill = bar.withTop(bar.getBottom() - bar.getHeight() * level);
            g.setColour(ch[c].db > -6.0f ? colours::bands[2] : colours::meterFill);
            g.fillRoundedRectangle(fill, 1.0f * s);
        }
        const float hold = MeterDisplay::fraction(ch[c].holdDb);
        if (hold > 0.0f) {
            g.setColour(colours::textPrimary);
            g.fillRect(bar.getX(), bar.getBottom() - bar.getHeight() * hold - 1.0f * s, bar.getWidth(), 1.0f * s);
        }
        if (ch[c].clipped()) {
            g.setColour(colours::clip);
            g.fillRoundedRectangle(bar.removeFromTop(3.0f * s), 1.0f * s);
        }
    }
}

void GlobalPanel::paint(juce::Graphics& g) {
    const float s = scaleOf(*this);
    g.setColour(colours::panel);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f * s);
    g.setColour(colours::textPrimary);
    g.setFont(TetraLookAndFeel::font(13.0f * s, Weight::SemiBold));
    g.drawText("Global", getLocalBounds().reduced(static_cast<int>(10.0f * s), 0).withTrimmedTop(static_cast<int>(8.0f * s)).removeFromTop(static_cast<int>(20.0f * s)),
               juce::Justification::centredLeft);
    paintMeter(g, inMeter_.toFloat(), meters_.input);
    paintMeter(g, outMeter_.toFloat(), meters_.output);
}

void GlobalPanel::resized() {
    const float s = scaleOf(*this);
    auto r = getLocalBounds().reduced(static_cast<int>(8.0f * s));
    auto head = r.removeFromTop(static_cast<int>(26.0f * s));
    bypass_.setBounds(head.removeFromRight(static_cast<int>(24.0f * s)).withSizeKeepingCentre(static_cast<int>(20.0f * s), static_cast<int>(20.0f * s)));

    // choice row, aligned with the band strips' Solo buttons: crossover mode takes the wider share
    auto row = r.removeFromBottom(static_cast<int>(24.0f * s)).reduced(static_cast<int>(4.0f * s), 0);
    const int gap = static_cast<int>(6.0f * s);
    crossover_.setBounds(row.removeFromLeft((row.getWidth() - gap) * 11 / 20));
    row.removeFromLeft(gap);
    oversample_.setBounds(row);
    r.removeFromBottom(static_cast<int>(6.0f * s));

    // input meter beside the Input knob, output meter beside the Output knob
    auto meterArea = r.reduced(0, static_cast<int>(18.0f * s));
    inMeter_ = meterArea.removeFromLeft(static_cast<int>(9.0f * s));
    outMeter_ = meterArea.removeFromRight(static_cast<int>(9.0f * s));
    r.removeFromLeft(static_cast<int>(12.0f * s));
    r.removeFromRight(static_cast<int>(12.0f * s));

    const int third = r.getWidth() / 3;
    input_.setBounds(r.removeFromLeft(third));
    shape_.setBounds(r.removeFromLeft(third));
    output_.setBounds(r);
}

}  // namespace tdui
