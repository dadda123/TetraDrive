// TetraDrive editor: top bar, spectrum/band view, band strips, global panel, help bar and About overlay.
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <memory>

#include "PluginProcessor.h"
#include "gui/AboutPanel.h"
#include "gui/BandStrip.h"
#include "gui/GlobalPanel.h"
#include "gui/HelpBar.h"
#include "gui/MeterDisplay.h"
#include "gui/PresetBar.h"
#include "gui/SpectrumBandView.h"
#include "gui/TetraLookAndFeel.h"

class TetraDriveEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit TetraDriveEditor(TetraDriveProcessor&);
    ~TetraDriveEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;

    // Test hooks for offscreen snapshots.
    tdui::SpectrumBandView& spectrumView() { return spectrum_; }
    void showAbout(bool visible, int tab = 0);
    int aboutLinkAreas() const { return about_.numLinkAreas(); }

private:
    void timerCallback() override;
    void updateFftButtons();
    void updateSizeButton();
    void showSizeMenu();
    void updateAnalyzerButton();
    void showAnalyzerMenu();
    void setAboutVisible(bool);
    tdui::HelpInfo helpForComponent(juce::Component*) const;

    TetraDriveProcessor& proc_;
    tdui::TetraLookAndFeel lnf_;
    tdui::MeterDisplay meters_;   // before the views that read it
    double lastMeterTick_ = 0.0;

    juce::Rectangle<int> wordmark_;
    juce::DrawableButton info_{"About", juce::DrawableButton::ImageFitted};
    juce::DrawableButton help_{"Help", juce::DrawableButton::ImageFitted};
    tdui::PresetBar presetBar_;
    std::array<juce::TextButton, 3> fft_{juce::TextButton{"4096"}, juce::TextButton{"8192"}, juce::TextButton{"16384"}};
    juce::TextButton analyzer_{"Analyzer"};
    juce::TextButton size_{"Small"};

    tdui::SpectrumBandView spectrum_;
    std::array<std::unique_ptr<tdui::BandStrip>, td::kBands> strips_;
    tdui::GlobalPanel global_;
    tdui::HelpBar helpBar_;
    tdui::AboutPanel about_;

    juce::Component::SafePointer<juce::Component> helpTarget_;
    bool spectrumHelpActive_ = false;
    bool sizeRestored_ = false;   // blocks saving the size until the saved one is applied

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TetraDriveEditor)
};
