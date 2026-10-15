#include "SpectrumBandView.h"

#include <complex>

#include "../PluginProcessor.h"
#include "MeterDisplay.h"
#include "TetraLookAndFeel.h"

namespace tdui {

namespace {

constexpr float kGapRatio = 1.0594630943592953f;   // 1/12 octave
constexpr float kDriveDbPerHeight = 60.0f;          // drive change for a full-height drag

const char* edgeHint(int e) {
    return (e == 0 || e == td::kBands) ? "Drag to move. Shift = fine. Double-click = reset."
                                       : "Drag to move the crossover. Shift = fine. Double-click = reset.";
}

}  // namespace

SpectrumBandView::SpectrumBandView(TetraDriveProcessor& p, const MeterDisplay& meters)
    : proc_(p), meters_(meters), vblank_(this, [this](double ts) { tick(ts); }) {
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
    proc_.analyzerOut.resetDisplay();
    curveKey_.clear();
    rebuildCurves();
    rebuildSpectrumPaths();
}

// ---- animation --------------------------------------------------------------------------------

void SpectrumBandView::updateAnalyzer(SpectrumAnalyzer& analyzer, bool show, std::vector<float>& db, double dt) {
    if (show) {
        analyzer.setOrder(proc_.ui.fftOrder.load());
        analyzer.update(freqs_, db, dt);
        return;
    }
    analyzer.drain();
    if (!db.empty()) { db.clear(); analyzer.resetDisplay(); }
}

void SpectrumBandView::tick(double ts) {
    const double dt = lastTs_ > 0.0 ? ts - lastTs_ : 1.0 / 60.0;
    lastTs_ = ts;

    const bool on = proc_.ui.analyzerOn.load();
    const int source = proc_.ui.analyzerSource.load();
    const bool wasShowing = showIn_ || showOut_;
    showIn_ = on && source != TetraDriveProcessor::kAnalyzeOutput;
    showOut_ = on && source != TetraDriveProcessor::kAnalyzeInput;
    updateAnalyzer(proc_.analyzer, showIn_, inDb_, dt);
    updateAnalyzer(proc_.analyzerOut, showOut_, outDb_, dt);
    bool dirty = showIn_ || showOut_ || wasShowing;
    if (dirty) rebuildSpectrumPaths();

    const auto before = curveKey_;
    rebuildCurves();
    dirty |= before != curveKey_;
    for (int b = 0; b < td::kBands; ++b) {
        const int q = juce::roundToInt(meters_.bandSaturation(b) * 48.0f);
        if (q != shownSaturation_[static_cast<std::size_t>(b)]) { shownSaturation_[static_cast<std::size_t>(b)] = q; dirty = true; }
    }
    if (dirty) repaint();
}

void SpectrumBandView::buildSpectrumPath(const std::vector<float>& db, juce::Path& fill, juce::Path& line) const {
    fill.clear();
    line.clear();
    if (db.size() != freqs_.size() || freqs_.empty()) return;
    line.preallocateSpace(static_cast<int>(freqs_.size()) * 3 + 8);
    fill.preallocateSpace(static_cast<int>(freqs_.size()) * 3 + 16);
    for (std::size_t i = 0; i < freqs_.size(); ++i) {
        const float x = freqToX(freqs_[i]);
        const float y = juce::jlimit(plot_.getY(), plot_.getBottom(), specDbToY(db[i]));
        if (i == 0) { line.startNewSubPath(x, y); fill.startNewSubPath(x, plot_.getBottom()); }
        else line.lineTo(x, y);
        fill.lineTo(x, y);
    }
    fill.lineTo(plot_.getRight(), plot_.getBottom());
    fill.closeSubPath();
}

void SpectrumBandView::rebuildSpectrumPaths() {
    buildSpectrumPath(inDb_, inFill_, inLine_);
    buildSpectrumPath(outDb_, outFill_, outLine_);
}

void SpectrumBandView::rebuildCurves() {
    const double sr = proc_.currentSampleRate();
    std::vector<float> key;
    std::array<float, td::kBands + 1> e{};
    for (int i = 0; i <= td::kBands; ++i) e[static_cast<std::size_t>(i)] = edgeHz(i);
    std::sort(e.begin(), e.end());
    for (float v : e) key.push_back(v);
    for (int b = 0; b < td::kBands; ++b) key.push_back(proc_.realValue(td::levelParam(b)));
    const auto mode = std::lround(proc_.realValue(td::kCrossover)) == 1 ? td::Crossover::Flat : td::Crossover::Classic;
    key.push_back(static_cast<float>(mode));
    key.push_back(static_cast<float>(sr));
    key.push_back(plot_.getWidth());
    key.push_back(plot_.getHeight());
    if (key == curveKey_) return;
    curveKey_ = key;

    std::array<double, td::kBands + 1> edges{};
    for (int i = 0; i <= td::kBands; ++i) edges[static_cast<std::size_t>(i)] = e[static_cast<std::size_t>(i)];
    std::array<td::BandFilter, td::kBands> filters{};
    std::array<double, td::kBands> gains{};
    for (int b = 0; b < td::kBands; ++b) {
        td::Engine::designBandForDisplay(mode, sr, edges.data(), b, filters[static_cast<std::size_t>(b)]);
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
            const std::complex<double> h = gains[static_cast<std::size_t>(b)] * filters[static_cast<std::size_t>(b)].response(zInv);
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

    // spectrum: on its own either trace is filled; together the output is filled and the input
    // becomes a muted outline so the added harmonics read as the difference between the two
    const bool both = !inLine_.isEmpty() && !outLine_.isEmpty();
    if (!outLine_.isEmpty()) {
        g.setColour(both ? colours::spectrumOutputFill : colours::spectrumFill);
        g.fillPath(outFill_);
        g.setColour(both ? colours::spectrumOutputLine : colours::spectrumLine);
        g.strokePath(outLine_, juce::PathStrokeType(both ? 1.25f : 1.0f));
    }
    if (!inLine_.isEmpty()) {
        if (!both) {
            g.setColour(colours::spectrumFill);
            g.fillPath(inFill_);
        }
        // drawn last so the input stays legible inside the output's fill
        g.setColour(both ? colours::spectrumInputDim : colours::spectrumLine);
        g.strokePath(inLine_, juce::PathStrokeType(1.0f));
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
        // the puck glows as the band is driven into its curve
        const float sat = meters_.bandSaturation(b);
        if (sat > 0.0f) {
            const float glowR = ringR + (3.0f + 9.0f * sat) * s;
            juce::ColourGradient glow(colour.withAlpha(0.38f * sat), c.x, c.y, colour.withAlpha(0.0f), c.x + glowR, c.y, true);
            g.setGradientFill(glow);
            g.fillEllipse(juce::Rectangle<float>(2 * glowR, 2 * glowR).withCentre(c));
        }
        g.setColour(colours::outline);
        g.strokePath(ringTrack, juce::PathStrokeType(2.0f * s));
        g.setColour(colour);
        g.strokePath(ring, juce::PathStrokeType(2.0f * s));
        const bool lit = active || sat > 0.55f;
        g.setColour(active ? colour : colours::panelRaised.interpolatedWith(colour, sat));
        g.fillEllipse(juce::Rectangle<float>(2 * r, 2 * r).withCentre(c));
        g.setColour(active ? colours::focus : colour);
        g.drawEllipse(juce::Rectangle<float>(2 * r, 2 * r).withCentre(c), 2.0f);
        g.setColour(lit ? colours::background : colours::textPrimary);
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

    // legend
    if (showIn_ || showOut_) {
        const auto font = TetraLookAndFeel::font(11.0f * s);
        g.setFont(font);
        float x = plot_.getX() + 8.0f * s;
        const float y = plot_.getY() + 6.0f * s, h = 16.0f * s;
        auto item = [&](juce::Colour swatch, const juce::String& label) {
            g.setColour(swatch);
            g.fillRect(x, y + h * 0.5f - 1.0f * s, 12.0f * s, 2.0f * s);
            x += 16.0f * s;
            juce::GlyphArrangement ga;
            ga.addLineOfText(font, label, 0.0f, 0.0f);
            const float w = ga.getBoundingBox(0, -1, true).getWidth() + 2.0f * s;
            g.setColour(colours::textSecondary);
            g.drawText(label, juce::Rectangle<float>(x, y, w, h), juce::Justification::centredLeft);
            x += w + 10.0f * s;
        };
        if (showIn_) item(showOut_ ? colours::spectrumInputDim : colours::spectrumLine, "Input");
        if (showOut_) item(showIn_ ? colours::spectrumOutputLine : colours::spectrumLine, "Output");
        g.setColour(colours::textSecondary.withAlpha(0.7f));
        g.drawText("+3 dB/oct tilt", juce::Rectangle<float>(x, y, 100.0f * s, h), juce::Justification::centredLeft);
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
        h.description = "Live input and output spectrum with the four bands on top. Drag the lines to move crossovers, drag the numbered pucks to set band level.";
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
