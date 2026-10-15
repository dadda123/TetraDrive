// Live input spectrum with band overlay, filter curves and draggable crossovers and band pucks.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <vector>

#include "HelpBar.h"

class TetraDriveProcessor;
class SpectrumAnalyzer;

namespace tdui {

class MeterDisplay;

class SpectrumBandView : public juce::Component {
public:
    SpectrumBandView(TetraDriveProcessor&, const MeterDisplay&);

    std::function<void(const HelpInfo&)> onHelp;

    void paint(juce::Graphics&) override;
    void resized() override;

    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    // Advances analyzer and curves by one display frame (called on vblank; public for snapshots).
    void tick(double timestampSec);

    static constexpr float kFreqMin = 10.0f, kFreqMax = 22050.0f;   // covers the full Low Cut and High Cut ranges
    static constexpr float kSpecTop = 6.0f, kSpecBottom = -90.0f;    // dBFS
    static constexpr float kBandTop = 30.0f, kBandBottom = -42.0f;   // dB, room for +/-24 dB levels

private:
    enum class Kind { None, Edge, Puck };
    struct Target {
        Kind kind = Kind::None;
        int index = -1;
        bool operator==(const Target& o) const { return kind == o.kind && index == o.index; }
        bool operator!=(const Target& o) const { return !(*this == o); }
    };

    void rebuildCurves();
    void rebuildSpectrumPaths();
    void buildSpectrumPath(const std::vector<float>& db, juce::Path& fill, juce::Path& line) const;
    // Advances one analyzer or, when hidden, keeps its history current and clears its trace.
    void updateAnalyzer(SpectrumAnalyzer& analyzer, bool show, std::vector<float>& db, double dt);

    Target hitTestTarget(juce::Point<float>) const;
    HelpInfo helpFor(const Target&) const;
    void publishHelp();

    float freqToX(float f) const;
    float xToFreq(float x) const;
    float specDbToY(float db) const;
    float bandDbToY(float db) const;
    float yToBandDb(float y) const;

    float edgeHz(int e) const;
    float edgeX(int e) const;
    float puckX(int band) const;
    float puckY(int band) const;

    void beginDrag(int param);
    void setDragValue(int param, float real);
    void endDrag();

    TetraDriveProcessor& proc_;
    const MeterDisplay& meters_;
    juce::VBlankAttachment vblank_;
    juce::Rectangle<float> plot_;
    std::array<int, 4> shownSaturation_{};   // quantised puck glow last painted

    std::vector<float> freqs_, inDb_, outDb_;
    juce::Path inFill_, inLine_, outFill_, outLine_;
    std::array<juce::Path, 4> bandCurves_;
    juce::Path sumCurve_;
    std::vector<float> curveKey_;
    double lastTs_ = 0.0;
    bool showIn_ = false, showOut_ = false;

    Target hover_, drag_;
    juce::Point<float> mousePos_;
    int dragParam_ = -1;
    float dragStartValue_ = 0.0f;
    float dragStartY_ = 0.0f;
    float dragStartX_ = 0.0f;
    bool dragDrive_ = false;
};

}  // namespace tdui
