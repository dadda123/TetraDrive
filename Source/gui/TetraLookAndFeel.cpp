#include "TetraLookAndFeel.h"

#include <BinaryData.h>

#include "td_dsp.h"

namespace tdui {

namespace {

// Shared Inter typefaces at three weights.
struct FontCache {
    FontCache() {
        int size = 0;
        if (const auto* data = TetraDriveBinary::getNamedResource("InterVariable_ttf", size)) {
            base = juce::Typeface::createSystemTypefaceFor(data, static_cast<size_t>(size));
            if (base != nullptr) {
                regular = withWeight(400.0f);
                medium = withWeight(500.0f);
                semiBold = withWeight(600.0f);
            }
        }
    }

    juce::Typeface::Ptr withWeight(float w) const {
        const juce::FontVariableSetting setting{"wght", w};
        auto t = base->cloneWithVariableSettings(juce::Span<const juce::FontVariableSetting>(&setting, 1));
        return t != nullptr ? t : base;
    }

    juce::Typeface::Ptr get(Weight w) const {
        switch (w) {
            case Weight::Medium: return medium;
            case Weight::SemiBold: return semiBold;
            case Weight::Regular: break;
        }
        return regular;
    }

    juce::Typeface::Ptr base, regular, medium, semiBold;
};

}  // namespace

TetraLookAndFeel::TetraLookAndFeel() {
    using namespace colours;
    setColour(juce::ResizableWindow::backgroundColourId, background);
    setColour(juce::Label::textColourId, textPrimary);
    setColour(juce::Slider::textBoxTextColourId, textPrimary);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, outline);
    setColour(juce::Slider::rotarySliderOutlineColourId, outline);
    setColour(juce::Slider::rotarySliderFillColourId, textPrimary);
    setColour(juce::TextEditor::backgroundColourId, panelRaised);
    setColour(juce::TextEditor::textColourId, textPrimary);
    setColour(juce::TextEditor::outlineColourId, outline);
    setColour(juce::TextEditor::focusedOutlineColourId, focus);
    setColour(juce::TextEditor::highlightColourId, outline);
    setColour(juce::CaretComponent::caretColourId, textPrimary);
    setColour(juce::TextButton::buttonColourId, panelRaised);
    setColour(juce::TextButton::buttonOnColourId, outline);
    setColour(juce::TextButton::textColourOffId, textSecondary);
    setColour(juce::TextButton::textColourOnId, textPrimary);
    setColour(juce::PopupMenu::backgroundColourId, panelRaised);
    setColour(juce::PopupMenu::textColourId, textPrimary);
    setColour(juce::PopupMenu::headerTextColourId, textSecondary);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, outline);
    setColour(juce::PopupMenu::highlightedTextColourId, textPrimary);
    setColour(juce::AlertWindow::backgroundColourId, panel);
    setColour(juce::AlertWindow::textColourId, textPrimary);
    setColour(juce::AlertWindow::outlineColourId, outline);
    setColour(juce::ScrollBar::thumbColourId, outline);
    setColour(juce::HyperlinkButton::textColourId, bands[0]);
}

juce::Font TetraLookAndFeel::font(float height, Weight weight) {
    juce::SharedResourcePointer<FontCache> cache;
    if (auto t = cache->get(weight)) return juce::Font(juce::FontOptions(t).withHeight(height));
    return juce::Font(juce::FontOptions(height));
}

std::unique_ptr<juce::Drawable> TetraLookAndFeel::icon(const char* name, juce::Colour colour) {
    int size = 0;
    const auto* data = TetraDriveBinary::getNamedResource(name, size);
    if (data == nullptr) return {};
    auto d = juce::Drawable::createFromImageData(data, static_cast<size_t>(size));
    if (d != nullptr) d->replaceColour(juce::Colours::black, colour);
    return d;
}

juce::Typeface::Ptr TetraLookAndFeel::getTypefaceForFont(const juce::Font& f) {
    juce::SharedResourcePointer<FontCache> cache;
    if (auto t = cache->get(f.isBold() ? Weight::SemiBold : Weight::Regular)) return t;
    return LookAndFeel_V4::getTypefaceForFont(f);
}

void TetraLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float startAngle,
                                        float endAngle, juce::Slider& s) {
    const auto bounds = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(2.0f);
    const float radius = std::min(bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float stroke = juce::jmax(2.0f, radius * 0.09f);
    const float arcRadius = radius - stroke;
    const auto accent = s.findColour(juce::Slider::rotarySliderFillColourId);
    const auto& props = s.getProperties();
    const float angle = startAngle + pos * (endAngle - startAngle);

    // track
    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour(colours::outline);
    g.strokePath(track, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // value arc
    // arc starts at the default value (0 dB) when an origin is set
    const float from = props.contains("origin")
                           ? startAngle + static_cast<float>(s.valueToProportionOfLength(static_cast<double>(props["origin"]))) * (endAngle - startAngle)
                           : startAngle;
    if (std::abs(angle - from) > 0.001f) {
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, std::min(from, angle), std::max(from, angle), true);
        g.setColour(s.isEnabled() ? accent : accent.withAlpha(0.4f));
        g.strokePath(arc, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // body and pointer
    const float bodyR = arcRadius - stroke * 1.6f;
    g.setColour(colours::panelRaised);
    g.fillEllipse(juce::Rectangle<float>(bodyR * 2.0f, bodyR * 2.0f).withCentre(centre));
    const bool active = s.isMouseOverOrDragging() || s.hasKeyboardFocus(true);
    g.setColour(active ? colours::focus : colours::outline);
    g.drawEllipse(juce::Rectangle<float>(bodyR * 2.0f, bodyR * 2.0f).withCentre(centre), active ? 1.5f : 1.0f);
    // waveshaper curve inside the Shape knob
    if (static_cast<bool>(props.getWithDefault("shapePreview", false))) {
        const int shape = juce::jlimit(0, td::kShapeCount - 1, static_cast<int>(std::lround(s.getValue())));
        const float half = bodyR * 0.55f;
        juce::Path curve;
        for (int i = 0; i <= 40; ++i) {
            const float xn = -1.0f + 2.0f * static_cast<float>(i) / 40.0f;
            const float yn = td::shapeLookup(td::kShapeTables[shape], xn * 1.5f);
            const juce::Point<float> p{centre.x + xn * half, centre.y - yn * half};
            if (i == 0) curve.startNewSubPath(p); else curve.lineTo(p);
        }
        g.setColour(accent.withAlpha(0.9f));
        g.strokePath(curve, juce::PathStrokeType(1.5f));
        return;
    }
    const auto tip = centre.getPointOnCircumference(bodyR * 0.8f, angle);
    const auto inner = centre.getPointOnCircumference(bodyR * 0.25f, angle);
    g.setColour(colours::textPrimary);
    g.drawLine({inner, tip}, juce::jmax(1.5f, stroke * 0.7f));
}

juce::Label* TetraLookAndFeel::createSliderTextBox(juce::Slider& s) {
    auto* l = LookAndFeel_V4::createSliderTextBox(s);
    l->setFont(font(13.0f * getScale(), Weight::Medium));
    l->setJustificationType(juce::Justification::centred);
    l->setColour(juce::Label::textColourId, colours::textPrimary);
    l->setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour(juce::Label::outlineWhenEditingColourId, colours::focus);
    return l;
}

juce::Font TetraLookAndFeel::getLabelFont(juce::Label& l) { return l.getFont(); }

void TetraLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool highlighted, bool down) {
    const auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    const float corner = juce::jmin(6.0f, r.getHeight() * 0.25f);
    const bool on = b.getToggleState();
    const auto onColour = b.getProperties().contains("accent")
                              ? juce::Colour(static_cast<juce::uint32>(static_cast<juce::int64>(b.getProperties()["accent"])))
                              : colours::textPrimary;
    g.setColour(on ? onColour.withAlpha(0.18f) : (down ? colours::outline : colours::panelRaised));
    g.fillRoundedRectangle(r, corner);
    g.setColour(on ? onColour : (highlighted || b.hasKeyboardFocus(false) ? colours::textSecondary : colours::outline));
    g.drawRoundedRectangle(r, corner, 1.0f);
}

void TetraLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool highlighted, bool) {
    const bool on = b.getToggleState();
    g.setFont(getTextButtonFont(b, b.getHeight()));
    g.setColour(on || highlighted ? colours::textPrimary : colours::textSecondary);
    g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(4, 0), juce::Justification::centred, 1);
}

juce::Font TetraLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight) {
    return font(juce::jmin(14.0f * getScale(), static_cast<float>(buttonHeight) * 0.55f), Weight::Medium);
}

void TetraLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height) {
    g.fillAll(colours::panelRaised);
    g.setColour(colours::outline);
    g.drawRect(0, 0, width, height);
}

juce::Font TetraLookAndFeel::getPopupMenuFont() { return font(14.0f * getScale()); }


}  // namespace tdui
