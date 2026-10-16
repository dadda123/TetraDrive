#include "BandStrip.h"

#include "../PluginProcessor.h"
#include "HelpBar.h"
#include "MeterDisplay.h"
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

SaturationLed::SaturationLed(juce::Colour accent) : accent_(accent) {
    setInterceptsMouseClicks(true, false);
    describe(*this, "Saturation",
             "How much this band's waveshaper is changing the signal: the difference between the shaped band and the clean band, "
             "relative to the clean band. Dark = transparent, lit = audible saturation, white = mostly distortion.");
}

void SaturationLed::setHeat(float heat01) {
    heat_ = juce::jlimit(0.0f, 1.0f, heat01);
    const int q = juce::roundToInt(heat_ * 64.0f);
    if (q != shown_) { shown_ = q; repaint(); }
}

float SaturationLed::lensRadius() const { return static_cast<float>(juce::jmin(getWidth(), getHeight())) * 0.13f; }

bool SaturationLed::hitTest(int x, int y) {
    return getLocalBounds().toFloat().getCentre().getDistanceFrom({static_cast<float>(x), static_cast<float>(y)}) <= lensRadius() * 1.8f;
}

void SaturationLed::paint(juce::Graphics& g) {
    const auto area = getLocalBounds().toFloat();
    const auto c = area.getCentre();
    const float r = juce::jmin(area.getWidth(), area.getHeight()) * 0.5f;
    const float lensR = lensRadius();
    const float heat = heat_;

    // corona: reaches further and burns brighter with heat; eased so a hint of saturation shows
    const float glow = std::sqrt(heat);
    if (glow > 0.0f) {
        const float glowR = lensR + (r - lensR) * (0.3f + 0.7f * glow);
        const auto hot = accent_.interpolatedWith(juce::Colours::white, 0.35f * heat);
        juce::ColourGradient grad(hot.withAlpha(0.85f * glow), c.x, c.y, accent_.withAlpha(0.0f), c.x + glowR, c.y, true);
        grad.addColour(0.25, accent_.withAlpha(0.45f * glow));
        grad.addColour(0.55, accent_.withAlpha(0.14f * glow));
        g.setGradientFill(grad);
        g.fillEllipse(juce::Rectangle<float>(2 * glowR, 2 * glowR).withCentre(c));
    }

    // bezel
    g.setColour(colours::background);
    g.fillEllipse(juce::Rectangle<float>(2 * (lensR + 2.0f), 2 * (lensR + 2.0f)).withCentre(c));

    // lens: dark glass -> band colour -> white-hot, with a brighter filament core once lit
    const auto dim = colours::panelRaised.interpolatedWith(accent_, 0.16f);
    const auto edge = heat < 0.7f ? dim.interpolatedWith(accent_, heat / 0.7f)
                                  : accent_.interpolatedWith(juce::Colours::white, (heat - 0.7f) / 0.3f * 0.75f);
    const auto core = edge.interpolatedWith(juce::Colours::white, 0.55f * heat);
    juce::ColourGradient lens(core, c.x, c.y, edge, c.x + lensR, c.y, true);
    g.setGradientFill(lens);
    g.fillEllipse(juce::Rectangle<float>(2 * lensR, 2 * lensR).withCentre(c));
    g.setColour(heat > 0.05f ? accent_.withAlpha(0.6f) : colours::outline);
    g.drawEllipse(juce::Rectangle<float>(2 * lensR, 2 * lensR).withCentre(c), 1.0f);
    // specular highlight keeps it reading as a lamp even when dark
    g.setColour(juce::Colours::white.withAlpha(0.2f + 0.25f * heat));
    g.fillEllipse(juce::Rectangle<float>(lensR * 0.75f, lensR * 0.42f).withCentre({c.x - lensR * 0.2f, c.y - lensR * 0.42f}));
}

BandStrip::BandStrip(TetraDriveProcessor& p, int band, const MeterDisplay& meters)
    : proc_(p),
      band_(band),
      meters_(meters),
      drive_(p, td::driveParam(band), "Drive", colours::bands[static_cast<std::size_t>(band)]),
      level_(p, td::levelParam(band), "Level", colours::bands[static_cast<std::size_t>(band)]),
      led_(colours::bands[static_cast<std::size_t>(band)]) {
    header_ = td::kBandNames[static_cast<std::size_t>(band)];
    addAndMakeVisible(drive_);
    addAndMakeVisible(level_);
    addAndMakeVisible(led_);
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

    const int level = juce::roundToInt(MeterDisplay::fraction(meters_.bandOutDb[static_cast<std::size_t>(band_)]) * meterBar().getWidth());
    if (level != shownLevel_) { shownLevel_ = level; repaint(meterBar().expanded(1.0f).toNearestInt()); }
    led_.setHeat(meters_.bandSaturation(band_));
}

juce::Rectangle<float> BandStrip::meterBar() const {
    const float s = scaleOf(const_cast<BandStrip&>(*this));
    return getLocalBounds().toFloat().removeFromTop(3.0f * s).reduced(10.0f * s, 0.0f);
}

void BandStrip::paint(juce::Graphics& g) {
    const float s = scaleOf(*this);
    auto r = getLocalBounds().toFloat();
    g.setColour(colours::panel);
    g.fillRoundedRectangle(r, 6.0f * s);
    const auto colour = colours::bands[static_cast<std::size_t>(band_)];
    // the accent bar doubles as the band's output level meter
    const auto bar = meterBar();
    g.setColour(colour.withAlpha(0.28f));
    g.fillRoundedRectangle(bar, 1.5f * s);
    const float level = MeterDisplay::fraction(meters_.bandOutDb[static_cast<std::size_t>(band_)]);
    if (level > 0.0f) {
        g.setColour(colour);
        g.fillRoundedRectangle(bar.withWidth(juce::jmax(3.0f * s, bar.getWidth() * level)), 1.5f * s);
    }

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

    // lamp in the free corner above the drive knob's arc (the rotary is inset 2 px and sits
    // above its text box), on the 45 degree diagonal just outside the ring
    auto knob = drive_.getBounds().withTrimmedTop(static_cast<int>(16.0f * s)).withTrimmedBottom(static_cast<int>(18.0f * s)).reduced(2);
    const float kr = static_cast<float>(juce::jmin(knob.getWidth(), knob.getHeight())) * 0.5f;
    const auto centre = knob.getCentre().toFloat();
    const float ledSize = juce::jlimit(30.0f, 56.0f, kr * 1.2f);   // mostly corona; lens is ~a quarter of this
    const juce::Point<float> ledCentre{centre.x + kr * 0.82f, centre.y - kr * 0.82f};
    led_.setBounds(juce::Rectangle<float>(ledSize, ledSize).withCentre(ledCentre).toNearestInt());
}

}  // namespace tdui
