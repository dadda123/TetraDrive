// About overlay: name, version, description, link, licenses and patch notes from embedded files.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Logo.h"

namespace tdui {

class AboutPanel : public juce::Component, private juce::Timer {
public:
    AboutPanel();
    ~AboutPanel() override;

    std::function<void()> onClose;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void visibilityChanged() override;
    // 0 = About, 1 = What's new.
    void showTab(int tab);
    // Clickable link areas on the current page (for tests).
    int numLinkAreas() const;

private:
    class TextPage;
    juce::Rectangle<int> cardBounds() const;
    juce::Rectangle<float> glyphBounds() const;
    void rebuildPages();
    void timerCallback() override;

    // the logo draws itself in whenever the panel opens, then keeps gently moving
    logo::Animator logoAnimator_;
    double openedAtMs_ = 0.0;

    juce::var about_;
    juce::String changelog_;
    juce::TextButton aboutTab_{"About"}, notesTab_{"What's new"};
    juce::DrawableButton close_{"Close", juce::DrawableButton::ImageFitted};
    juce::HyperlinkButton link_;
    juce::Viewport viewport_;
    std::unique_ptr<TextPage> page_;
    int tab_ = 0;
    float builtScale_ = 0.0f;
};

}  // namespace tdui
