// One-line help strip: name, current value and a short description of the hovered control.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace tdui {

struct HelpInfo {
    juce::String title, value, description;
    bool isEmpty() const { return title.isEmpty() && description.isEmpty(); }
};

// Marks a component as describing a parameter (help bar reads name, value and description).
inline void setHelpParam(juce::Component& c, int paramIndex) { c.getProperties().set("helpParam", paramIndex); }
// Marks a component with fixed help text.
inline void describe(juce::Component& c, const juce::String& title, const juce::String& description) {
    c.getProperties().set("helpTitle", title);
    c.getProperties().set("helpText", description);
}

class HelpBar : public juce::Component {
public:
    void setInfo(const HelpInfo& info) {
        if (info.title == info_.title && info.value == info_.value && info.description == info_.description) return;
        info_ = info;
        repaint();
    }
    void paint(juce::Graphics&) override;

private:
    HelpInfo info_;
};

}  // namespace tdui
