#include "PresetBar.h"

#include "../PluginProcessor.h"
#include "HelpBar.h"
#include "TetraLookAndFeel.h"

namespace tdui {

namespace {

void setIcon(juce::DrawableButton& b, const char* name) {
    auto normal = TetraLookAndFeel::icon(name, colours::textSecondary);
    auto over = TetraLookAndFeel::icon(name, colours::textPrimary);
    auto disabled = TetraLookAndFeel::icon(name, colours::outline);
    b.setImages(normal.get(), over.get(), over.get(), disabled.get());
    b.setColour(juce::DrawableButton::backgroundColourId, juce::Colours::transparentBlack);
}

void confirm(juce::Component* owner, const juce::String& title, const juce::String& message, const juce::String& action,
             std::function<void()> onYes) {
    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::QuestionIcon)
                       .withTitle(title)
                       .withMessage(message)
                       .withButton(action)
                       .withButton("Cancel")
                       .withAssociatedComponent(owner);
    juce::AlertWindow::showAsync(options, [onYes = std::move(onYes)](int result) {
        if (result == 1) onYes();
    });
}

}  // namespace

PresetBar::PresetBar(TetraDriveProcessor& p) : proc_(p) {
    setIcon(prev_, "prev_svg");
    setIcon(next_, "next_svg");
    setIcon(save_, "save_svg");
    setIcon(delete_, "trash_svg");
    describe(prev_, "Previous preset", "Load the previous preset in the list.");
    describe(next_, "Next preset", "Load the next preset in the list.");
    describe(name_, "Presets", "Click to load a preset, save, save as or delete. A dot means unsaved changes.");
    describe(save_, "Save preset", "Save changes to the current preset, or save as a new preset.");
    describe(delete_, "Delete preset", "Delete the current user preset. Default cannot be deleted.");

    prev_.onClick = [this] { proc_.presets.loadNext(-1); refresh(); };
    next_.onClick = [this] { proc_.presets.loadNext(1); refresh(); };
    name_.onClick = [this] { showMenu(); };
    save_.onClick = [this] { saveCurrent(); };
    delete_.onClick = [this] { deleteCurrent(); };
    for (auto* c : std::initializer_list<juce::Component*>{&prev_, &name_, &next_, &save_, &delete_}) addAndMakeVisible(c);
    refresh();
}

void PresetBar::resized() {
    auto r = getLocalBounds();
    const int icon = r.getHeight();
    prev_.setBounds(r.removeFromLeft(icon).reduced(icon / 5));
    delete_.setBounds(r.removeFromRight(icon).reduced(icon / 5));
    save_.setBounds(r.removeFromRight(icon).reduced(icon / 5));
    next_.setBounds(r.removeFromRight(icon).reduced(icon / 5));
    name_.setBounds(r.reduced(2, 0));
}

void PresetBar::refresh() {
    const auto& pm = proc_.presets;
    juce::String text = pm.currentName();
    if (pm.isModified()) text << juce::String(juce::CharPointer_UTF8("  \xe2\x80\xa2"));
    if (text != shown_) {
        shown_ = text;
        name_.setButtonText(text);
    }
    delete_.setEnabled(pm.isUserPreset(pm.currentName()));
}

void PresetBar::showMenu() {
    juce::PopupMenu menu;
    const auto names = proc_.presets.names();
    for (int i = 0; i < names.size(); ++i) {
        const auto n = names[i];
        menu.addItem(juce::PopupMenu::Item(n).setTicked(n == proc_.presets.currentName()).setAction([this, n] {
            proc_.presets.load(n);
            refresh();
        }));
        if (i == 0 && names.size() > 1) menu.addSeparator();
    }
    menu.addSeparator();
    menu.addItem("Save", [this] { saveCurrent(); });
    menu.addItem("Save As...", [this] { saveAs(); });
    menu.addItem(juce::PopupMenu::Item("Delete").setEnabled(proc_.presets.isUserPreset(proc_.presets.currentName())).setAction([this] { deleteCurrent(); }));
    menu.addSeparator();
    menu.addItem("Open Presets Folder", [] { PresetManager::presetDirectory().startAsProcess(); });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&name_).withMinimumWidth(name_.getWidth()));
}

void PresetBar::saveCurrent() {
    const auto name = proc_.presets.currentName();
    if (!proc_.presets.isUserPreset(name)) { saveAs(); return; }
    juce::Component::SafePointer<PresetBar> safe(this);
    confirm(this, "Save preset", "Overwrite \"" + name + "\" with the current settings?", "Save", [safe, name] {
        if (safe != nullptr) safe->writePreset(name);
    });
}

void PresetBar::saveAs() {
    nameDialog_ = std::make_unique<juce::AlertWindow>("Save preset as", "Enter a name for the new preset.", juce::MessageBoxIconType::NoIcon, this);
    const auto current = proc_.presets.currentName();
    nameDialog_->addTextEditor("name", current == PresetManager::kDefaultName ? juce::String("My Preset") : current, "Name");
    nameDialog_->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    nameDialog_->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<PresetBar> safe(this);
    nameDialog_->enterModalState(true, juce::ModalCallbackFunction::create([safe](int result) {
        if (safe == nullptr || safe->nameDialog_ == nullptr) return;
        const auto name = safe->nameDialog_->getTextEditorContents("name").trim();
        safe->nameDialog_.reset();
        if (result != 1 || name.isEmpty()) return;
        if (name.equalsIgnoreCase(PresetManager::kDefaultName)) {
            juce::AlertWindow::showAsync(juce::MessageBoxOptions().withTitle("Save preset").withMessage("\"Default\" is built in. Please choose another name.").withButton("OK").withAssociatedComponent(safe.getComponent()), nullptr);
            return;
        }
        if (safe->proc_.presets.exists(name)) {
            confirm(safe.getComponent(), "Save preset", "A preset named \"" + name + "\" already exists. Overwrite it?", "Overwrite", [safe, name] {
                if (safe != nullptr) safe->writePreset(name);
            });
            return;
        }
        safe->writePreset(name);
    }), false);
}

void PresetBar::writePreset(const juce::String& name) {
    if (!proc_.presets.save(name))
        juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::WarningIcon).withTitle("Save preset")
                                         .withMessage("Could not write the preset file.").withButton("OK").withAssociatedComponent(this), nullptr);
    refresh();
}

void PresetBar::deleteCurrent() {
    const auto name = proc_.presets.currentName();
    if (!proc_.presets.isUserPreset(name)) return;
    juce::Component::SafePointer<PresetBar> safe(this);
    confirm(this, "Delete preset", "Delete \"" + name + "\"? This cannot be undone.", "Delete", [safe, name] {
        if (safe == nullptr) return;
        safe->proc_.presets.remove(name);
        safe->refresh();
    });
}

}  // namespace tdui
