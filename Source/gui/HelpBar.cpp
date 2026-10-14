#include "HelpBar.h"

#include "TetraLookAndFeel.h"

namespace tdui {

void HelpBar::paint(juce::Graphics& g) {
    const float s = scaleOf(*this);
    auto r = getLocalBounds().toFloat();
    g.setColour(colours::panel);
    g.fillRect(r);
    g.setColour(colours::outline);
    g.fillRect(r.removeFromTop(1.0f));

    r = r.reduced(12.0f * s, 0.0f);
    if (info_.isEmpty()) {
        g.setColour(colours::textSecondary);
        g.setFont(TetraLookAndFeel::font(12.5f * s));
        g.drawText("Hover a control to see what it does.", r, juce::Justification::centredLeft, true);
        return;
    }
    juce::AttributedString text;
    text.setJustification(juce::Justification::centredLeft);
    text.setWordWrap(juce::AttributedString::none);
    text.append(info_.title, TetraLookAndFeel::font(12.5f * s, Weight::SemiBold), colours::textPrimary);
    if (info_.value.isNotEmpty())
        text.append("   " + info_.value, TetraLookAndFeel::font(12.5f * s, Weight::Medium), colours::textPrimary);
    if (info_.description.isNotEmpty())
        text.append("   " + info_.description, TetraLookAndFeel::font(12.5f * s), colours::textSecondary);
    text.draw(g, r);
}

}  // namespace tdui
