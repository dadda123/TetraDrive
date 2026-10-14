#include "SpectrumBandView.h"

#include <complex>

#include "../PluginProcessor.h"
#include "TetraLookAndFeel.h"

namespace tdui {

namespace {

constexpr float kGapRatio = 1.0594630943592953f;   // 1/12 octave
constexpr float kDriveDbPerHeight = 60.0f;          // drive change for a full-height drag

const char* edgeHint(int e) {
    return (e == 0 || e == td::kBands) ? "Drag to move. Shift = fine. Double-click = reset."
                                       : "Drag to move the crossover. Shift = fine. Double-click = reset.";
}

std::complex<double> sectionResponse(const td::Section& s, std::complex<double> zInv) {
    const auto z2 = zInv * zInv;
    const auto z3 = z2 * zInv;
    const auto z4 = z2 * z2;
    const auto num = s.b0 + s.b2 * z2 + s.b4 * z4;
    const auto den = 1.0 + s.a1 * zInv + s.a2 * z2 + s.a3 * z3 + s.a4 * z4;
    return num / den;
}

}  // namespace

SpectrumBandView::SpectrumBandView(TetraDriveProcessor& p)
    : proc_(p), vblank_(this, [this](double ts) { tick(ts); }) {
    setOpaque(true);
    setRepaintsOnMouseActivity(false);
}

// ---- geometry ---------------------------------------------------------------------------------

float SpectrumBandView::freqToX(float f) const {
    const float t = std::log(f / kFreqMin) / std::log(kFreqMax / kFreqMin);
    return plot_.getX() + t * plot_.getWidth();
}
float SpectrumBandView::xToFreq(float x) const {
    const float t = (x - plot_.getX()) / plot_.getWidth();
    return kFreqMin * std::pow(kFreqMax / kFreqMin, t);
}
float SpectrumBandView::specDbToY(float db) const {
    return juce::jmap(db, kSpecBottom, kSpecTop, plot_.getBottom(), plot_.getY());
}
float SpectrumBandView::bandDbToY(float db) const {
    return juce::jmap(db, kBandBottom, kBandTop, plot_.getBottom(), plot_.getY());
}
float SpectrumBandView::yToBandDb(float y) const {
    return juce::jmap(y, plot_.getBottom(), plot_.getY(), kBandBottom, kBandTop);
}

float SpectrumBandView::edgeHz(int e) const { return proc_.realValue(td::edgeParam(e)); }

// Edge line position, kept inside the plot so every handle stays visible and grabbable.
float SpectrumBandView::edgeX(int e) const {
    const float inset = 4.0f * scaleOf(const_cast<SpectrumBandView&>(*this));
    return juce::jlimit(plot_.getX() + inset, plot_.getRight() - inset, freqToX(edgeHz(e)));
}

float SpectrumBandView::puckX(int band) const {
    const float lo = juce::jmax(kFreqMin, edgeHz(band)), hi = juce::jmin(kFreqMax, edgeHz(band + 1));
    const float mid = std::sqrt(lo * juce::jmax(lo, hi));
    return juce::jlimit(plot_.getX() + 8.0f, plot_.getRight() - 8.0f, freqToX(mid));
}
float SpectrumBandView::puckY(int band) const { return bandDbToY(proc_.realValue(td::levelParam(band))); }

void SpectrumBandView::resized() {
    const float s = scaleOf(*this);
    plot_ = getLocalBounds().toFloat().withTrimmedLeft(44.0f * s).withTrimmedRight(44.0f * s).withTrimmedTop(10.0f * s).withTrimmedBottom(22.0f * s);
    const int points = juce::jmax(64, static_cast<int>(plot_.getWidth() / 2.0f));
    freqs_.resize(static_cast<std::size_t>(points));
    for (int i = 0; i < points; ++i)
        freqs_[static_cast<std::size_t>(i)] = kFreqMin * std::pow(kFreqMax / kFreqMin, static_cast<float>(i) / static_cast<float>(points - 1));
    proc_.analyzer.resetDisplay();
    curveKey_.clear();
    rebuildCurves();
    rebuildSpectrumPath();
}

// ---- animation --------------------------------------------------------------------------------

void SpectrumBandView::tick(double ts) {
    const double dt = lastTs_ > 0.0 ? ts - lastTs_ : 1.0 / 60.0;
    lastTs_ = ts;
    bool dirty = false;

    const bool on = proc_.ui.analyzerOn.load();
    if (on) {
        proc_.analyzer.setOrder(proc_.ui.fftOrder.load());
        proc_.analyzer.update(freqs_, specDb_, dt);
        rebuildSpectrumPath();
        dirty = true;
    } else if (analyzerWasOn_) {
        specDb_.clear();
        rebuildSpectrumPath();
        dirty = true;
    }
    analyzerWasOn_ = on;

    const auto before = curveKey_;
    rebuildCurves();
    dirty |= before != curveKey_;
    if (dirty) repaint();
}

void SpectrumBandView::rebuildSpectrumPath() {
    specFill_.clear();
    specLine_.clear();
    if (specDb_.size() != freqs_.size() || freqs_.empty()) return;
    for (std::size_t i = 0; i < freqs_.size(); ++i) {
        const float x = freqToX(freqs_[i]);
        const float y = juce::jlimit(plot_.getY(), plot_.getBottom(), specDbToY(specDb_[i]));
        if (i == 0) { specLine_.startNewSubPath(x, y); specFill_.startNewSubPath(x, plot_.getBottom()); }
        else specLine_.lineTo(x, y);
        specFill_.lineTo(x, y);
    }
    specFill_.lineTo(plot_.getRight(), plot_.getBottom());
    specFill_.closeSubPath();
}

void SpectrumBandView::rebuildCurves() {
    const double sr = proc_.currentSampleRate();
    std::vector<float> key;
    std::array<float, td::kBands + 1> e{};
    for (int i = 0; i <= td::kBands; ++i) e[static_cast<std::size_t>(i)] = edgeHz(i);
    std::sort(e.begin(), e.end());
    for (float v : e) key.push_back(v);
    for (int b = 0; b < td::kBands; ++b) key.push_back(proc_.realValue(td::levelParam(b)));
    key.push_back(static_cast<float>(sr));
    key.push_back(plot_.getWidth());
    key.push_back(plot_.getHeight());
    if (key == curveKey_) return;
    curveKey_ = key;

    std::array<std::array<td::Section, td::kSectionsPerBand>, td::kBands> sections{};
    std::array<double, td::kBands> gains{};
    for (int b = 0; b < td::kBands; ++b) {
        td::Engine::designBandForDisplay(sr, e[static_cast<std::size_t>(b)], e[static_cast<std::size_t>(b + 1)], sections[static_cast<std::size_t>(b)]);
        gains[static_cast<std::size_t>(b)] = std::pow(10.0, proc_.realValue(td::levelParam(b)) / 20.0);
    }

    for (auto& p : bandCurves_) p.clear();
    sumCurve_.clear();
    const float step = 2.0f;
    bool started = false;
    for (float x = plot_.getX(); x <= plot_.getRight(); x += step) {
        const double f = xToFreq(x);
        if (f >= sr * 0.4999) break;
        const double w = juce::MathConstants<double>::twoPi * f / sr;
        const std::complex<double> zInv = std::polar(1.0, -w);
        std::complex<double> sum{};
        for (int b = 0; b < td::kBands; ++b) {
            std::complex<double> h = gains[static_cast<std::size_t>(b)];
            for (const auto& sec : sections[static_cast<std::size_t>(b)]) h *= sectionResponse(sec, zInv);
            sum += h;
            const float y = juce::jlimit(plot_.getY() - 2.0f, plot_.getBottom() + 2.0f,
                                         bandDbToY(static_cast<float>(20.0 * std::log10(std::abs(h) + 1e-9))));
            if (!started) bandCurves_[static_cast<std::size_t>(b)].startNewSubPath(x, y);
            else bandCurves_[static_cast<std::size_t>(b)].lineTo(x, y);
        }
        const float ys = juce::jlimit(plot_.getY() - 2.0f, plot_.getBottom() + 2.0f,
                                      bandDbToY(static_cast<float>(20.0 * std::log10(std::abs(sum) + 1e-9))));
        if (!started) sumCurve_.startNewSubPath(x, ys);
        else sumCurve_.lineTo(x, ys);
        started = true;
    }
}

// ---- painting ---------------------------------------------------------------------------------

void SpectrumBandView::paint(juce::Graphics& g) {
    const float s = scaleOf(*this);
    g.fillAll(colours::background);
    g.setColour(colours::panel);
    g.fillRect(plot_);

    // grid
    static constexpr float kGridFreqs[] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900,
                                           1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 20000};
    for (float f : kGridFreqs) {
        const bool major = f == 10 || f == 20 || f == 50 || f == 100 || f == 200 || f == 500 || f == 1000 || f == 2000 || f == 5000 || f == 10000 || f == 20000;
        g.setColour(major ? colours::grid.brighter(0.15f) : colours::grid);
        g.drawVerticalLine(juce::roundToInt(freqToX(f)), plot_.getY(), plot_.getBottom());
    }
    for (float db = kBandTop - 6.0f; db > kBandBottom; db -= 6.0f) {
        g.setColour(db == 0.0f ? colours::outline.brighter(0.2f) : colours::grid);
        g.drawHorizontalLine(juce::roundToInt(bandDbToY(db)), plot_.getX(), plot_.getRight());
    }

    // band fields
    std::array<float, td::kBands + 1> e{};
    for (int i = 0; i <= td::kBands; ++i) e[static_cast<std::size_t>(i)] = edgeHz(i);
    std::sort(e.begin(), e.end());
    const int solo = static_cast<int>(std::lround(proc_.realValue(td::kSolo))) - 1;
    g.saveState();
    g.reduceClipRegion(plot_.toNearestInt());
    for (int b = 0; b < td::kBands; ++b) {
        const float x0 = freqToX(e[static_cast<std::size_t>(b)]), x1 = freqToX(e[static_cast<std::size_t>(b + 1)]);
        const float alpha = solo < 0 ? 0.12f : (solo == b ? 0.2f : 0.04f);
        g.setColour(colours::bands[static_cast<std::size_t>(b)].withAlpha(alpha));
        g.fillRect(juce::Rectangle<float>(x0, plot_.getY(), juce::jmax(0.0f, x1 - x0), plot_.getHeight()));
    }
    // outside low/high cut
    g.setColour(colours::background.withAlpha(0.55f));
    g.fillRect(plot_.withRight(freqToX(e[0])));
    g.fillRect(plot_.withLeft(freqToX(e[td::kBands])));

    // spectrum
    if (!specLine_.isEmpty()) {
        g.setColour(colours::spectrumFill);
        g.fillPath(specFill_);
        g.setColour(colours::spectrumLine);
        g.strokePath(specLine_, juce::PathStrokeType(1.0f));
    }

    // band and sum curves
    for (int b = 0; b < td::kBands; ++b) {
        g.setColour(colours::bands[static_cast<std::size_t>(b)].withAlpha(solo < 0 || solo == b ? 1.0f : 0.35f));
        g.strokePath(bandCurves_[static_cast<std::size_t>(b)], juce::PathStrokeType(1.5f * s));
    }
    g.setColour(colours::sumCurve);
    g.strokePath(sumCurve_, juce::PathStrokeType(2.0f * s));

    // crossover lines and cut handles
    for (int i = 0; i <= td::kBands; ++i) {
        const float x = edgeX(i);
        const bool active = (hover_ == Target{Kind::Edge, i}) || (drag_ == Target{Kind::Edge, i});
        g.setColour(active ? colours::focus : colours::textSecondary.withAlpha(0.7f));
        g.drawLine(x, plot_.getY(), x, plot_.getBottom(), active ? 2.0f : 1.0f);
        const auto tab = juce::Rectangle<float>(8.0f * s, 14.0f * s).withCentre({x, plot_.getBottom() - 10.0f * s});
        g.setColour(active ? colours::focus : colours::panelRaised);
        g.fillRoundedRectangle(tab, 2.0f * s);
        g.setColour(active ? colours::background : colours::textSecondary);
        g.drawRoundedRectangle(tab, 2.0f * s, 1.0f);
    }

    // band pucks with drive ring
    for (int b = 0; b < td::kBands; ++b) {
        const juce::Point<float> c{puckX(b), puckY(b)};
        const float r = 7.0f * s;
        const auto colour = colours::bands[static_cast<std::size_t>(b)];
        const bool active = (hover_ == Target{Kind::Puck, b}) || (drag_ == Target{Kind::Puck, b});
        const auto& dSpec = td::kParams[static_cast<std::size_t>(td::driveParam(b))];
        const float driveNorm = (proc_.realValue(td::driveParam(b)) - dSpec.min) / (dSpec.max - dSpec.min);
        const float ringR = r + 4.0f * s;
        juce::Path ringTrack, ring;
        ringTrack.addCentredArc(c.x, c.y, ringR, ringR, 0.0f, -2.4f, 2.4f, true);
        ring.addCentredArc(c.x, c.y, ringR, ringR, 0.0f, -2.4f, -2.4f + 4.8f * driveNorm, true);
        g.setColour(colours::outline);
        g.strokePath(ringTrack, juce::PathStrokeType(2.0f * s));
        g.setColour(colour);
        g.strokePath(ring, juce::PathStrokeType(2.0f * s));
        g.setColour(active ? colour : colours::panelRaised);
        g.fillEllipse(juce::Rectangle<float>(2 * r, 2 * r).withCentre(c));
        g.setColour(active ? colours::focus : colour);
        g.drawEllipse(juce::Rectangle<float>(2 * r, 2 * r).withCentre(c), 2.0f);
        g.setColour(active ? colours::background : colours::textPrimary);
        g.setFont(TetraLookAndFeel::font(10.5f * s, Weight::SemiBold));
        g.drawText(juce::String(b + 1), juce::Rectangle<float>(2 * r, 2 * r).withCentre(c), juce::Justification::centred);
    }
    g.restoreState();

    // axes
    g.setFont(TetraLookAndFeel::font(11.0f * s));
    g.setColour(colours::textSecondary);
    static constexpr float kLabelFreqs[] = {10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
    for (float f : kLabelFreqs) {
        const juce::String t = f >= 1000 ? juce::String(static_cast<int>(f / 1000)) + "k" : juce::String(static_cast<int>(f));
        const float x = freqToX(f);
        const float w = 36.0f * s;
        const float x0 = juce::jlimit(plot_.getX(), plot_.getRight() - w, x - w * 0.5f);
        auto just = x0 <= plot_.getX() ? juce::Justification::centredLeft
                  : x0 >= plot_.getRight() - w ? juce::Justification::centredRight : juce::Justification::centred;
        g.drawText(t, juce::Rectangle<float>(x0, plot_.getBottom() + 3.0f * s, w, 16.0f * s), just);
    }
    for (float db = 0.0f; db >= kSpecBottom + 1.0f; db -= 24.0f)
        g.drawText(juce::String(static_cast<int>(db)), juce::Rectangle<float>(0.0f, specDbToY(db) - 8.0f * s, plot_.getX() - 6.0f * s, 16.0f * s),
                   juce::Justification::centredRight);
    g.drawText("dBFS", juce::Rectangle<float>(0.0f, plot_.getBottom() - 16.0f * s, plot_.getX() - 6.0f * s, 16.0f * s), juce::Justification::centredRight);
    for (float db = 24.0f; db >= kBandBottom + 1.0f; db -= 12.0f) {
        const juce::String t = db > 0 ? "+" + juce::String(static_cast<int>(db)) : juce::String(static_cast<int>(db));
        g.drawText(t, juce::Rectangle<float>(plot_.getRight() + 6.0f * s, bandDbToY(db) - 8.0f * s, 40.0f * s, 16.0f * s), juce::Justification::centredLeft);
    }
    g.drawText("dB", juce::Rectangle<float>(plot_.getRight() + 6.0f * s, plot_.getBottom() - 16.0f * s, 40.0f * s, 16.0f * s), juce::Justification::centredLeft);

    // caption
    if (proc_.ui.analyzerOn.load()) {
        g.setFont(TetraLookAndFeel::font(11.0f * s));
        g.setColour(colours::textSecondary);
        g.drawText("Input spectrum +3 dB/oct tilt", plot_.reduced(8.0f * s, 6.0f * s).removeFromTop(16.0f * s), juce::Justification::topLeft);
    }

    // value label for the hovered or dragged target
    const Target t = drag_.kind != Kind::None ? drag_ : hover_;
    if (t.kind != Kind::None) {
        const auto info = helpFor(t);
        const juce::String text = t.kind == Kind::Edge ? info.value : info.title + "  " + info.value;
        const auto font = TetraLookAndFeel::font(12.0f * s, Weight::Medium);
        juce::GlyphArrangement ga;
        ga.addLineOfText(font, text, 0.0f, 0.0f);
        const float w = ga.getBoundingBox(0, -1, true).getWidth() + 16.0f * s, h = 22.0f * s;
        juce::Point<float> anchor = t.kind == Kind::Edge ? juce::Point<float>{edgeX(t.index), plot_.getY() + 14.0f * s}
                                                         : juce::Point<float>{puckX(t.index), puckY(t.index) - 24.0f * s};
        auto box = juce::Rectangle<float>(w, h).withCentre(anchor).constrainedWithin(plot_.reduced(2.0f));
        g.setColour(colours::panelRaised);
        g.fillRoundedRectangle(box, 4.0f * s);
        g.setColour(colours::focus);
        g.drawRoundedRectangle(box, 4.0f * s, 1.0f);
        g.setColour(colours::textPrimary);
        g.setFont(font);
        g.drawText(text, box, juce::Justification::centred);
    }

    g.setColour(colours::outline);
    g.drawRect(plot_, 1.0f);
}

// ---- interaction ------------------------------------------------------------------------------

SpectrumBandView::Target SpectrumBandView::hitTestTarget(juce::Point<float> p) const {
    const float s = scaleOf(const_cast<SpectrumBandView&>(*this));
    float best = 13.0f * s;
    Target t;
    for (int b = 0; b < td::kBands; ++b) {
        const float d = p.getDistanceFrom({puckX(b), puckY(b)});
        if (d < best) { best = d; t = {Kind::Puck, b}; }
    }
    if (t.kind != Kind::None) return t;
    best = 6.0f * s;
    for (int i = 0; i <= td::kBands; ++i) {
        const float d = std::abs(p.x - edgeX(i));
        if (d < best && p.y >= plot_.getY() && p.y <= plot_.getBottom()) { best = d; t = {Kind::Edge, i}; }
    }
    return t;
}

HelpInfo SpectrumBandView::helpFor(const Target& t) const {
    HelpInfo h;
    if (t.kind == Kind::Edge) {
        const int p = td::edgeParam(t.index);
        const auto& spec = td::kParams[static_cast<std::size_t>(p)];
        h.title = spec.name;
        h.value = td::formatValue(p, proc_.realValue(p));
        h.description = juce::String(spec.description) + " " + edgeHint(t.index);
    } else if (t.kind == Kind::Puck) {
        const int b = t.index;
        h.title = juce::String(td::kBandNames[static_cast<std::size_t>(b)]) + " band";
        h.value = "Level " + juce::String(td::formatValue(td::levelParam(b), proc_.realValue(td::levelParam(b)))) +
                  ", Drive " + juce::String(td::formatValue(td::driveParam(b), proc_.realValue(td::driveParam(b))));
        h.description = "Drag up/down = Level. Alt/Ctrl-drag or wheel = Drive. Shift = fine. Double-click = reset. Right-click = Solo.";
    } else {
        h.title = "Spectrum";
        h.description = "Live input spectrum with the four bands on top. Drag the lines to move crossovers, drag the numbered pucks to set band level.";
    }
    return h;
}

void SpectrumBandView::publishHelp() {
    if (onHelp) onHelp(helpFor(drag_.kind != Kind::None ? drag_ : hover_));
}

void SpectrumBandView::mouseMove(const juce::MouseEvent& e) {
    mousePos_ = e.position;
    const auto t = hitTestTarget(e.position);
    if (t != hover_) {
        hover_ = t;
        setMouseCursor(t.kind == Kind::Edge ? juce::MouseCursor::LeftRightResizeCursor
                       : t.kind == Kind::Puck ? juce::MouseCursor::UpDownResizeCursor
                                              : juce::MouseCursor::NormalCursor);
        repaint();
    }
    publishHelp();
}

void SpectrumBandView::mouseExit(const juce::MouseEvent&) {
    if (drag_.kind != Kind::None) return;
    hover_ = {};
    repaint();
    if (onHelp) onHelp({});
}

void SpectrumBandView::beginDrag(int param) {
    dragParam_ = param;
    dragStartValue_ = proc_.realValue(param);
    proc_.param(param).beginChangeGesture();
}

void SpectrumBandView::setDragValue(int param, float real) {
    auto& p = proc_.param(param);
    p.setValueNotifyingHost(p.convertTo0to1(real));
}

void SpectrumBandView::endDrag() {
    if (dragParam_ >= 0) proc_.param(dragParam_).endChangeGesture();
    dragParam_ = -1;
}

void SpectrumBandView::mouseDown(const juce::MouseEvent& e) {
    const auto t = hitTestTarget(e.position);
    if (t.kind == Kind::Puck && e.mods.isPopupMenu()) {
        const int soloValue = static_cast<int>(std::lround(proc_.realValue(td::kSolo)));
        proc_.setRealValue(td::kSolo, soloValue == t.index + 1 ? 0.0f : static_cast<float>(t.index + 1));
        repaint();
        return;
    }
    if (t.kind == Kind::None || e.mods.isPopupMenu()) return;
    drag_ = t;
    dragStartX_ = e.position.x;
    dragStartY_ = e.position.y;
    dragDrive_ = t.kind == Kind::Puck && (e.mods.isAltDown() || e.mods.isCtrlDown() || e.mods.isCommandDown());
    beginDrag(t.kind == Kind::Edge ? td::edgeParam(t.index) : dragDrive_ ? td::driveParam(t.index) : td::levelParam(t.index));
    publishHelp();
    repaint();
}

void SpectrumBandView::mouseDrag(const juce::MouseEvent& e) {
    if (drag_.kind == Kind::None || dragParam_ < 0) return;
    const float fine = e.mods.isShiftDown() ? 0.2f : 1.0f;
    const auto& spec = td::kParams[static_cast<std::size_t>(dragParam_)];
    float v;
    if (drag_.kind == Kind::Edge) {
        const float ratio = xToFreq(e.position.x) / xToFreq(dragStartX_);
        v = dragStartValue_ * std::pow(ratio, fine);
        const int i = drag_.index;
        const float lo = i > 0 ? edgeHz(i - 1) * kGapRatio : spec.min;
        const float hi = i < td::kBands ? edgeHz(i + 1) / kGapRatio : spec.max;
        v = juce::jlimit(juce::jmax(spec.min, lo), juce::jmin(spec.max, juce::jmax(lo, hi)), v);
    } else {
        const float dy = (dragStartY_ - e.position.y) * fine;
        v = dragDrive_ ? dragStartValue_ + dy * kDriveDbPerHeight / plot_.getHeight()
                       : dragStartValue_ + dy * (kBandTop - kBandBottom) / plot_.getHeight();
        v = juce::jlimit(spec.min, spec.max, v);
    }
    setDragValue(dragParam_, v);
    publishHelp();
    repaint();
}

void SpectrumBandView::mouseUp(const juce::MouseEvent& e) {
    endDrag();
    drag_ = {};
    hover_ = hitTestTarget(e.position);
    publishHelp();
    repaint();
}

void SpectrumBandView::mouseDoubleClick(const juce::MouseEvent& e) {
    const auto t = hitTestTarget(e.position);
    int p = -1;
    if (t.kind == Kind::Edge) p = td::edgeParam(t.index);
    else if (t.kind == Kind::Puck) p = (e.mods.isAltDown() || e.mods.isCtrlDown() || e.mods.isCommandDown()) ? td::driveParam(t.index) : td::levelParam(t.index);
    if (p < 0) return;
    proc_.setRealValue(p, td::kParams[static_cast<std::size_t>(p)].def);
    publishHelp();
    repaint();
}

void SpectrumBandView::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    const auto t = hitTestTarget(e.position);
    if (t.kind != Kind::Puck) return;
    const int p = td::driveParam(t.index);
    const auto& spec = td::kParams[static_cast<std::size_t>(p)];
    const float step = (e.mods.isShiftDown() ? 3.0f : 12.0f) * w.deltaY;
    proc_.setRealValue(p, juce::jlimit(spec.min, spec.max, proc_.realValue(p) + step));
    publishHelp();
    repaint();
}

}  // namespace tdui
