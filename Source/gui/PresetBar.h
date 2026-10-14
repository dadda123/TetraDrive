// Preset menu with previous/next, Save, Save As and Delete.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class TetraDriveProcessor;

namespace tdui {

class PresetBar : public juce::Component {
public:
    explicit PresetBar(TetraDriveProcessor&);
    void resized() override;
    // Updates the shown name and modified marker.
    void refresh();

private:
    void showMenu();
    void saveCurrent();
    void saveAs();
    void deleteCurrent();
    void writePreset(const juce::String& name);

    TetraDriveProcessor& proc_;
    juce::DrawableButton prev_{"Previous preset", juce::DrawableButton::ImageFitted};
    juce::DrawableButton next_{"Next preset", juce::DrawableButton::ImageFitted};
    juce::TextButton name_;
    juce::DrawableButton save_{"Save preset", juce::DrawableButton::ImageFitted};
    juce::DrawableButton delete_{"Delete preset", juce::DrawableButton::ImageFitted};
    juce::String shown_;
    std::unique_ptr<juce::AlertWindow> nameDialog_;
};

}  // namespace tdui
