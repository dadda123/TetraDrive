#include "PluginEditor.h"

using namespace tdui;

namespace {
constexpr int kMinW = 760, kMinH = 480, kMaxW = 2400, kMaxH = 1500;
constexpr float kBaseW = 1000.0f, kBaseH = 640.0f;

// Window size presets; Medium is the default.
struct SizePreset { const char* name; int w, h; };
constexpr SizePreset kSizes[] = {{"Small", 760, 480}, {"Medium", 1000, 640}, {"Large", 1400, 896}};
}  // namespace

TetraDriveEditor::TetraDriveEditor(TetraDriveProcessor& p)
    : AudioProcessorEditor(p), proc_(p), presetBar_(p), spectrum_(p), global_(p) {
    setLookAndFeel(&lnf_);

    // top bar
    auto infoNormal = TetraLookAndFeel::icon("info_svg", colours::textSecondary);
    auto infoOver = TetraLookAndFeel::icon("info_svg", colours::textPrimary);
    info_.setImages(infoNormal.get(), infoOver.get(), infoOver.get());
    info_.setColour(juce::DrawableButton::backgroundColourId, juce::Colours::transparentBlack);
    describe(info_, "About", "Version, description, licenses and patch notes.");
    info_.onClick = [this] { setAboutVisible(true); };
    addAndMakeVisible(info_);

    // help bar toggle: bright when the bar is shown
    auto helpOff = TetraLookAndFeel::icon("help_svg", colours::textSecondary.withAlpha(0.6f));
    auto helpOffOver = TetraLookAndFeel::icon("help_svg", colours::textSecondary);
    auto helpOn = TetraLookAndFeel::icon("help_svg", colours::textPrimary);
    help_.setImages(helpOff.get(), helpOffOver.get(), helpOn.get(), nullptr, helpOn.get(), helpOn.get(), helpOff.get());
    help_.setColour(juce::DrawableButton::backgroundColourId, juce::Colours::transparentBlack);
    help_.setColour(juce::DrawableButton::backgroundOnColourId, juce::Colours::transparentBlack);
    help_.setClickingTogglesState(true);
    help_.setToggleState(proc_.ui.helpBar.load(), juce::dontSendNotification);
    describe(help_, "Help bar", "Show or hide this help bar.");
    help_.onClick = [this] {
        proc_.ui.helpBar = help_.getToggleState();
        resized();
    };
    addAndMakeVisible(help_);

    describe(size_, "Window size", "Switch between Small, Medium and Large. You can also drag the corner to any size.");
    size_.onClick = [this] { showSizeMenu(); };
    addAndMakeVisible(size_);

    addAndMakeVisible(presetBar_);

    for (std::size_t i = 0; i < fft_.size(); ++i) {
        auto& b = fft_[i];
        b.setRadioGroupId(1001);
        b.setClickingTogglesState(true);
        describe(b, "Analyzer resolution", "Samples analysed per FFT. Higher values resolve frequencies more accurately. The display rate stays the same.");
        b.onClick = [this, i] {
            if (fft_[i].getToggleState()) proc_.ui.fftOrder = 12 + static_cast<int>(i);
        };
        addAndMakeVisible(b);
    }
    analyzer_.setClickingTogglesState(true);
    analyzer_.setToggleState(proc_.ui.analyzerOn.load(), juce::dontSendNotification);
    describe(analyzer_, "Analyzer", "Show or hide the live input spectrum behind the bands.");
    analyzer_.onClick = [this] { proc_.ui.analyzerOn = analyzer_.getToggleState(); };
    addAndMakeVisible(analyzer_);
    updateFftButtons();

    // main area
    spectrum_.onHelp = [this](const HelpInfo& info) {
        spectrumHelpActive_ = !info.isEmpty();
        helpTarget_ = nullptr;
        helpBar_.setInfo(info);
    };
    addAndMakeVisible(spectrum_);
    for (int b = 0; b < td::kBands; ++b) {
        strips_[static_cast<std::size_t>(b)] = std::make_unique<BandStrip>(p, b);
        addAndMakeVisible(*strips_[static_cast<std::size_t>(b)]);
    }
    addAndMakeVisible(global_);
    addAndMakeVisible(helpBar_);

    about_.onClose = [this] { setAboutVisible(false); };
    addChildComponent(about_);

    addMouseListener(this, true);

    // read the saved size first: setResizeLimits() resizes the empty editor and triggers resized()
    const int savedW = juce::jlimit(kMinW, kMaxW, proc_.ui.width.load());
    const int savedH = juce::jlimit(kMinH, kMaxH, proc_.ui.height.load());
    setResizable(true, true);
    setResizeLimits(kMinW, kMinH, kMaxW, kMaxH);
    setSize(savedW, savedH);
    sizeRestored_ = true;
    proc_.ui.width = getWidth();
    proc_.ui.height = getHeight();
    startTimerHz(30);
}

TetraDriveEditor::~TetraDriveEditor() {
    removeMouseListener(this);
    setLookAndFeel(nullptr);
}

void TetraDriveEditor::paint(juce::Graphics& g) {
    const float s = lnf_.getScale();
    g.fillAll(colours::background);
    g.setColour(colours::textPrimary);
    g.setFont(TetraLookAndFeel::font(19.0f * s, Weight::SemiBold));
    g.drawText("TetraDrive", wordmark_, juce::Justification::centredLeft);
}

void TetraDriveEditor::resized() {
    if (sizeRestored_) {
        proc_.ui.width = getWidth();
        proc_.ui.height = getHeight();
    }
    const float s = juce::jlimit(0.75f, 2.4f, juce::jmin(static_cast<float>(getWidth()) / kBaseW, static_cast<float>(getHeight()) / kBaseH));
    lnf_.setScale(s);
    auto px = [s](float v) { return juce::roundToInt(v * s); };

    auto r = getLocalBounds();
    about_.setBounds(r);

    // top bar
    auto top = r.removeFromTop(px(46.0f)).reduced(px(12.0f), px(8.0f));
    wordmark_ = top.removeFromLeft(px(112.0f));
    info_.setBounds(top.removeFromLeft(px(22.0f)).withSizeKeepingCentre(px(18.0f), px(18.0f)));
    top.removeFromLeft(px(16.0f));
    help_.setBounds(top.removeFromRight(px(22.0f)).withSizeKeepingCentre(px(18.0f), px(18.0f)));
    top.removeFromRight(px(8.0f));
    size_.setBounds(top.removeFromRight(px(70.0f)));
    top.removeFromRight(px(10.0f));
    analyzer_.setBounds(top.removeFromRight(px(78.0f)));
    top.removeFromRight(px(10.0f));
    for (int i = static_cast<int>(fft_.size()) - 1; i >= 0; --i) {
        fft_[static_cast<std::size_t>(i)].setBounds(top.removeFromRight(px(i == 2 ? 54.0f : 46.0f)));
        top.removeFromRight(px(4.0f));
    }
    top.removeFromRight(px(8.0f));
    presetBar_.setBounds(top.withSizeKeepingCentre(juce::jmin(top.getWidth(), px(340.0f)), top.getHeight()).withX(top.getX()));

    // help bar
    helpBar_.setVisible(proc_.ui.helpBar.load());
    if (helpBar_.isVisible()) helpBar_.setBounds(r.removeFromBottom(px(26.0f)));

    // spectrum and controls
    r = r.reduced(px(12.0f), px(4.0f)).withTrimmedBottom(px(8.0f));
    spectrum_.setBounds(r.removeFromTop(juce::roundToInt(static_cast<float>(r.getHeight()) * 0.58f)));
    r.removeFromTop(px(10.0f));
    const int gap = px(8.0f);
    const int unit = (r.getWidth() - gap * td::kBands) * 10 / (td::kBands * 10 + 13);
    for (auto& strip : strips_) {
        strip->setBounds(r.removeFromLeft(unit));
        r.removeFromLeft(gap);
    }
    global_.setBounds(r);

    updateSizeButton();
    for (auto* c : getChildren()) c->repaint();
}

void TetraDriveEditor::updateSizeButton() {
    juce::String label = "Custom";
    for (const auto& sz : kSizes)
        if (getWidth() == sz.w && getHeight() == sz.h) label = sz.name;
    size_.setButtonText(label);
}

void TetraDriveEditor::showSizeMenu() {
    juce::PopupMenu m;
    m.addSectionHeader("Window size");
    for (const auto& sz : kSizes) {
        juce::String text = juce::String(sz.name) + " (" + (&sz == &kSizes[1] ? "default " : "") + juce::String(sz.w) + "x" + juce::String(sz.h) + ")";
        const int w = sz.w, h = sz.h;
        m.addItem(juce::PopupMenu::Item(text).setTicked(getWidth() == w && getHeight() == h).setAction([this, w, h] { setSize(w, h); }));
    }
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&size_));
}

void TetraDriveEditor::updateFftButtons() {
    const int order = proc_.ui.fftOrder.load();
    for (std::size_t i = 0; i < fft_.size(); ++i)
        fft_[i].setToggleState(12 + static_cast<int>(i) == order, juce::dontSendNotification);
}

void TetraDriveEditor::setAboutVisible(bool visible) {
    about_.setVisible(visible);
    if (visible) about_.toFront(true);
}

void TetraDriveEditor::showAbout(bool visible, int tab) {
    about_.showTab(tab);
    setAboutVisible(visible);
}

// ---- help bar ---------------------------------------------------------------------------------

HelpInfo TetraDriveEditor::helpForComponent(juce::Component* c) const {
    for (; c != nullptr && c != this; c = c->getParentComponent()) {
        const auto& props = c->getProperties();
        if (props.contains("helpParam")) {
            const int i = static_cast<int>(props["helpParam"]);
            const auto& spec = td::kParams[static_cast<std::size_t>(i)];
            return {spec.name, td::formatValue(i, proc_.realValue(i)), spec.description};
        }
        if (props.contains("helpTitle")) return {props["helpTitle"].toString(), {}, props["helpText"].toString()};
    }
    return {};
}

void TetraDriveEditor::mouseEnter(const juce::MouseEvent& e) {
    if (e.eventComponent == &spectrum_) return;
    spectrumHelpActive_ = false;
    helpTarget_ = e.eventComponent;
    helpBar_.setInfo(helpForComponent(e.eventComponent));
}

void TetraDriveEditor::mouseExit(const juce::MouseEvent& e) {
    if (e.eventComponent == helpTarget_.getComponent() && !e.mods.isAnyMouseButtonDown()) {
        helpTarget_ = nullptr;
        helpBar_.setInfo({});
    }
}

void TetraDriveEditor::mouseUp(const juce::MouseEvent& e) {
    // keep the dragged control's help until the pointer actually leaves it
    if (helpTarget_ != nullptr && !helpTarget_->isMouseOver(true)) {
        helpTarget_ = nullptr;
        helpBar_.setInfo({});
    }
    if (e.eventComponent == this && wordmark_.contains(e.getPosition())) setAboutVisible(true);
}

void TetraDriveEditor::timerCallback() {
    for (auto& strip : strips_) strip->refresh();
    presetBar_.refresh();
    updateFftButtons();
    if (help_.getToggleState() != proc_.ui.helpBar.load())
        help_.setToggleState(proc_.ui.helpBar.load(), juce::dontSendNotification);
    if (analyzer_.getToggleState() != proc_.ui.analyzerOn.load())
        analyzer_.setToggleState(proc_.ui.analyzerOn.load(), juce::dontSendNotification);
    if (helpTarget_ != nullptr && !spectrumHelpActive_) helpBar_.setInfo(helpForComponent(helpTarget_.getComponent()));
}
