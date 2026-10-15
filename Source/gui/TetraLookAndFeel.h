// Dark "Pro Studio Minimal" theme: colour tokens, Inter variable font, flat vector controls.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

namespace tdui {

// Colour tokens.
namespace colours {
inline const juce::Colour background{0xff0e1014};
inline const juce::Colour panel{0xff161a21};
inline const juce::Colour panelRaised{0xff1d222b};
inline const juce::Colour outline{0xff2a303a};
inline const juce::Colour grid{0xff222833};
inline const juce::Colour textPrimary{0xffe8ecf2};
inline const juce::Colour textSecondary{0xff9aa4b2};
inline const juce::Colour focus{0xffe8ecf2};
inline const juce::Colour spectrumFill{0x33e8ecf2};
inline const juce::Colour spectrumLine{0xb3c9d1dc};
// When input and output are shown together the input steps back to a muted outline.
inline const juce::Colour spectrumInputDim{0x998a9ab0};
inline const juce::Colour spectrumOutputLine{0xf2f4f7fa};
inline const juce::Colour spectrumOutputFill{0x2ee8ecf2};
inline const juce::Colour sumCurve{0xffd5dbe3};
inline const juce::Colour meterFill{0xffb8c2cf};
inline const juce::Colour clip{0xffe35d5d};
inline const std::array<juce::Colour, 4> bands{{juce::Colour{0xff3ec6e0}, juce::Colour{0xff5dd39e},
                                                juce::Colour{0xfff2b33d}, juce::Colour{0xffe15fb0}}};
}  // namespace colours

enum class Weight { Regular, Medium, SemiBold };

class TetraLookAndFeel : public juce::LookAndFeel_V4 {
public:
    TetraLookAndFeel();

    // Inter at the given pixel height and weight.
    static juce::Font font(float height, Weight weight = Weight::Regular);

    // Loads an embedded SVG icon recoloured to the given colour.
    static std::unique_ptr<juce::Drawable> icon(const char* name, juce::Colour colour);

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle,
                          float endAngle, juce::Slider&) override;
    juce::Label* createSliderTextBox(juce::Slider&) override;
    juce::Font getLabelFont(juce::Label&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;

    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    juce::Font getPopupMenuFont() override;


    juce::Font getAlertWindowTitleFont() override { return font(17.0f, Weight::SemiBold); }
    juce::Font getAlertWindowMessageFont() override { return font(14.0f); }
    juce::Font getAlertWindowFont() override { return font(14.0f); }

    // UI scale for this editor, set from its height.
    void setScale(float s) { scale_ = s; }
    float getScale() const { return scale_; }

private:
    float scale_ = 1.0f;
};

// Scale of the LookAndFeel used by a component.
inline float scaleOf(juce::Component& c) {
    if (auto* lf = dynamic_cast<TetraLookAndFeel*>(&c.getLookAndFeel())) return lf->getScale();
    return 1.0f;
}

}  // namespace tdui
