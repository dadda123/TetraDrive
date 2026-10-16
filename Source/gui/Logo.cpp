#include "Logo.h"

#include <cmath>
#include <iterator>
#include <utility>
#include <vector>

#include "TetraLookAndFeel.h"

namespace tdui::logo {

namespace {

constexpr float kS3 = 1.7320508f;
constexpr float kH = kS3 * 0.5f;
constexpr float kPi = juce::MathConstants<float>::pi;

// ---- the molecule --------------------------------------------------------------------------
// Ergoline skeleton on a hexagon lattice (circumradius 1, y up): benzene A and ring C share a
// vertical edge, the pyrrole B bridges their peri positions, ring D sits on C's upper right edge.
// The substituents make it lysergic acid diethylamide rather than plain ergoline.

struct Atom {
    float x, y;
    const char* label;   // heteroatoms are lettered; bonds stop short of the letter
    float gap;
};

constexpr Atom kAtoms[] = {
    {-kH, 1.0f, nullptr, 0.0f},         // 0   A
    {0.0f, 0.5f, nullptr, 0.0f},        // 1   A/C
    {0.0f, -0.5f, nullptr, 0.0f},       // 2   A/B/C junction
    {-kH, -1.0f, nullptr, 0.0f},        // 3   A/B
    {-kS3, -0.5f, nullptr, 0.0f},       // 4   A
    {-kS3, 0.5f, nullptr, 0.0f},        // 5   A
    {kH, 1.0f, nullptr, 0.0f},          // 6   C/D
    {kS3, 0.5f, nullptr, 0.0f},         // 7   C/D
    {kS3, -0.5f, nullptr, 0.0f},        // 8   C
    {kH, -1.0f, nullptr, 0.0f},         // 9   B/C
    {kS3, 2.5f, nullptr, 0.0f},         // 10  D, carries the amide
    {kS3 + kH, 2.0f, nullptr, 0.0f},    // 11  D
    {kS3 + kH, 1.0f, "N", 0.36f},       // 12  D, ring nitrogen with methyl
    {kH, 2.0f, nullptr, 0.0f},          // 13  D
    {0.5f, -1.93f, nullptr, 0.0f},      // 14  B
    {-0.5f, -1.93f, "NH", 0.46f},       // 15  B, indole nitrogen
    {kS3 + kS3, 0.5f, nullptr, 0.0f},   // 16  N-methyl
    {kH, 3.0f, nullptr, 0.0f},          // 17  amide carbon
    {kH, 4.0f, "O", 0.36f},             // 18  carbonyl oxygen
    {0.0f, 2.5f, "N", 0.36f},           // 19  amide nitrogen
    {-kH, 3.0f, nullptr, 0.0f},         // 20  ethyl
    {-kH, 2.0f, nullptr, 0.0f},         // 21  ethyl
};

struct Bond { int a, b; bool core; };
constexpr Bond kBonds[] = {
    {0, 1, true}, {1, 2, true}, {2, 3, true}, {3, 4, true}, {4, 5, true}, {5, 0, true},      // A
    {6, 7, true}, {7, 8, true}, {8, 9, true}, {9, 2, true}, {1, 6, true},                    // C
    {10, 11, true}, {11, 12, true}, {12, 7, true}, {6, 13, true}, {13, 10, true},            // D
    {9, 14, true}, {14, 15, true}, {15, 3, true},                                            // B
    {12, 16, false}, {10, 17, false}, {17, 18, false}, {17, 19, false}, {19, 20, false}, {19, 21, false},
};

// Second line of a double bond, drawn on the side facing (sx, sy).
struct Double { int a, b; float sx, sy; float shorten; };
constexpr Double kDoubles[] = {
    {0, 1, -kH, 0.0f, 0.18f}, {2, 3, -kH, 0.0f, 0.18f}, {4, 5, -kH, 0.0f, 0.18f},   // aromatic A
    {6, 13, kS3, 1.5f, 0.18f},                                                      // D
    {9, 14, 0.0f, -1.27f, 0.18f},                                                   // pyrrole
    {17, 18, 0.0f, 3.5f, 0.08f},                                                    // C=O
};

constexpr int kNumAtoms = static_cast<int>(std::size(kAtoms));
constexpr int kSeedAtom = 2;   // the reveal grows outwards from the A/B/C junction

juce::Point<float> atomPos(int i) { return {kAtoms[i].x, -kAtoms[i].y}; }   // flip to y down

// Extents of the static drawing (atoms, plus the plates behind lettered ones). Fitting uses
// this rather than the live path so the animated molecule does not re-centre every frame.
juce::Rectangle<float> staticBounds(bool detail) {
    juce::Rectangle<float> r;
    bool first = true;
    for (int i = 0; i < kNumAtoms; ++i) {
        if (!detail && i > 15) continue;
        juce::Rectangle<float> a(atomPos(i), atomPos(i));
        if (detail && kAtoms[i].label) a = a.withSizeKeepingCentre(2 * kAtoms[i].gap * 0.78f, 2 * kAtoms[i].gap * 0.78f);
        r = first ? a : r.getUnion(a);
        first = false;
    }
    return r;
}

// Pseudo-3D: atoms sit on a sheet that carries a travelling radial ripple, the sheet tilts
// slowly about two axes, and the result is projected with mild perspective. Returns the
// projected point and its perspective scale (used to size the letters).
struct Warp {
    float time = 0.0f, depth = 0.0f;
    juce::Point<float> centre;

    std::pair<juce::Point<float>, float> operator()(juce::Point<float> p) const {
        if (depth <= 0.0f) return {p, 1.0f};
        const auto q = p - centre;
        const float r = q.getDistanceFromOrigin();
        // wavelength is about the size of the molecule, so it bulges as a whole rather than crumpling
        const float z = depth * 0.24f * std::sin(r * 1.05f - time * 1.7f);
        const float a = depth * 0.34f * std::sin(time * 0.83f);            // about the vertical axis
        const float b = depth * 0.24f * std::sin(time * 0.57f + 1.3f);     // about the horizontal axis
        const float x1 = q.x * std::cos(a) + z * std::sin(a);
        const float z1 = -q.x * std::sin(a) + z * std::cos(a);
        const float y1 = q.y * std::cos(b) - z1 * std::sin(b);
        const float z2 = q.y * std::sin(b) + z1 * std::cos(b);
        constexpr float eye = 10.0f;
        const float s = eye / (eye - z2);
        return {centre + juce::Point<float>(x1 * s, y1 * s), s};
    }
};

struct Label {
    juce::Path glyph, plate;
    float alpha = 1.0f;
};

struct Tip {
    juce::Point<float> at;
    int tint;
};

struct Molecule {
    juce::Path bonds;                 // stroked
    std::vector<Label> labels;        // letters and the paper discs behind them
    std::vector<Tip> tips;            // growing bond ends while the reveal is in progress
    juce::Rectangle<float> bounds;
};

// `motion` may be null for the static print.
Molecule buildMolecule(bool detail, float labelHeight, const Motion* motion) {
    Molecule m;
    m.bounds = staticBounds(detail);
    const float reveal = motion != nullptr ? juce::jlimit(0.0f, 1.0f, motion->reveal) : 1.0f;
    Warp warp;
    if (motion != nullptr) {
        warp.time = motion->time;
        warp.depth = juce::jlimit(0.0f, 1.0f, motion->depth);
        warp.centre = m.bounds.getCentre();
    }

    // reveal timing: everything starts in order of distance from the seed and takes a fixed
    // slice; the farthest label (start 0.58 + 0.12, slice 0.28) completes just as reveal hits 1
    const auto seed = atomPos(kSeedAtom);
    float maxDist = 1.0f;
    for (int i = 0; i < kNumAtoms; ++i) maxDist = juce::jmax(maxDist, atomPos(i).getDistanceFrom(seed));
    auto progressAt = [&](float dist, float delay) {
        const float start = (dist / maxDist) * 0.58f + delay;
        return juce::jlimit(0.0f, 1.0f, (reveal - start) / 0.28f);
    };

    std::array<std::pair<juce::Point<float>, float>, kNumAtoms> pos;
    for (int i = 0; i < kNumAtoms; ++i) pos[static_cast<std::size_t>(i)] = warp(atomPos(i));
    auto at = [&](int i) { return pos[static_cast<std::size_t>(i)].first; };

    int bondIndex = 0;
    for (const auto& bond : kBonds) {
        if (!bond.core && !detail) continue;
        // grow from the end nearer the seed
        const bool flip = atomPos(bond.b).getDistanceFrom(seed) < atomPos(bond.a).getDistanceFrom(seed);
        const int ia = flip ? bond.b : bond.a, ib = flip ? bond.a : bond.b;
        const float f = progressAt(atomPos(ia).getDistanceFrom(seed), 0.0f);
        if (f <= 0.0f) { ++bondIndex; continue; }
        auto p0 = at(ia), p1 = at(ib);
        const auto dir = (p1 - p0) / p0.getDistanceFrom(p1);
        if (detail && kAtoms[ia].label) p0 += dir * kAtoms[ia].gap;
        if (detail && kAtoms[ib].label) p1 -= dir * kAtoms[ib].gap;
        const auto end = p0 + (p1 - p0) * f;
        m.bonds.startNewSubPath(p0);
        m.bonds.lineTo(end);
        if (f < 1.0f) m.tips.push_back({end, bondIndex % 4});
        ++bondIndex;
    }
    if (detail) {
        for (const auto& d : kDoubles) {
            const float near = juce::jmin(atomPos(d.a).getDistanceFrom(seed), atomPos(d.b).getDistanceFrom(seed));
            const float f = progressAt(near, 0.10f);
            if (f <= 0.0f) continue;
            const auto p0 = at(d.a), p1 = at(d.b);
            const auto dir = (p1 - p0) / p0.getDistanceFrom(p1);
            const auto side = warp({d.sx, -d.sy}).first;
            auto n = juce::Point<float>(-dir.y, dir.x);
            if ((side - p0).getDotProduct(n) < 0.0f) n = -n;
            const auto off = n * 0.17f;
            const auto len = p0.getDistanceFrom(p1);
            auto a = p0 + dir * (len * d.shorten) + off;
            auto b = p1 - dir * (len * d.shorten) + off;
            if (kAtoms[d.a].label) a += dir * kAtoms[d.a].gap;
            if (kAtoms[d.b].label) b -= dir * kAtoms[d.b].gap;
            m.bonds.startNewSubPath(a);
            m.bonds.lineTo(a + (b - a) * f);
        }
        // letters are built large and scaled down so glyph outlines stay exact
        const float big = 100.0f;
        const auto font = TetraLookAndFeel::font(big, Weight::SemiBold);
        for (int i = 0; i < kNumAtoms; ++i) {
            if (!kAtoms[i].label) continue;
            const float alpha = progressAt(atomPos(i).getDistanceFrom(seed), 0.12f);
            if (alpha <= 0.0f) continue;
            juce::GlyphArrangement ga;
            ga.addLineOfText(font, kAtoms[i].label, 0.0f, 0.0f);
            const auto bb = ga.getBoundingBox(0, -1, true);
            Label label;
            ga.createPath(label.glyph);
            const auto [centre, scale] = pos[static_cast<std::size_t>(i)];
            const float k = labelHeight / big * scale;
            label.glyph.applyTransform(juce::AffineTransform::translation(-bb.getCentreX(), -bb.getCentreY())
                                           .scaled(k, k)
                                           .translated(centre));
            const float r = kAtoms[i].gap * 0.78f * scale;
            label.plate.addEllipse(juce::Rectangle<float>(2 * r, 2 * r).withCentre(centre));
            label.alpha = alpha;
            m.labels.push_back(std::move(label));
        }
    }
    return m;
}

// ---- the blotter ---------------------------------------------------------------------------

constexpr float kNotchR = 0.018f;
constexpr int kNotchesPerEdge = 18;
constexpr float kHoleR = 0.012f;
constexpr int kHolesPerGutter = 20;

// One edge of a stamp outline: a straight run with semicircular bites taken out of the paper.
void addStampEdge(juce::Path& p, juce::Point<float> from, juce::Point<float> to, bool notched) {
    const auto dir = (to - from) / from.getDistanceFrom(to);
    const juce::Point<float> inward{-dir.y, dir.x};   // clockwise walk: paper is on the right
    if (!notched) { p.lineTo(to); return; }
    const float pitch = 1.0f / kNotchesPerEdge;
    for (int k = 0; k < kNotchesPerEdge; ++k) {
        const auto c = from + dir * ((static_cast<float>(k) + 0.5f) * pitch);
        p.lineTo(c - dir * kNotchR);
        constexpr int steps = 14;
        for (int i = 1; i < steps; ++i) {
            const float t = kPi * static_cast<float>(i) / static_cast<float>(steps);
            p.lineTo(c - dir * (kNotchR * std::cos(t)) + inward * (kNotchR * std::sin(t)));
        }
        p.lineTo(c + dir * kNotchR);
    }
    p.lineTo(to);
}

// Unit square sheet (-0.5..0.5) with notched edges; gutter holes are subpaths removed by even-odd fill.
juce::Path buildSheet(bool notched, bool holes) {
    juce::Path p;
    const juce::Point<float> tl{-0.5f, -0.5f}, tr{0.5f, -0.5f}, br{0.5f, 0.5f}, bl{-0.5f, 0.5f};
    p.startNewSubPath(tl);
    addStampEdge(p, tl, tr, notched);
    addStampEdge(p, tr, br, notched);
    addStampEdge(p, br, bl, notched);
    addStampEdge(p, bl, tl, notched);
    p.closeSubPath();
    if (holes) {
        const float pitch = 1.0f / kHolesPerGutter;
        for (int k = 0; k < kHolesPerGutter; ++k) {
            const float t = -0.5f + (static_cast<float>(k) + 0.5f) * pitch;
            p.addEllipse(juce::Rectangle<float>(2 * kHoleR, 2 * kHoleR).withCentre({0.0f, t}));
            p.addEllipse(juce::Rectangle<float>(2 * kHoleR, 2 * kHoleR).withCentre({t, 0.0f}));
        }
    }
    p.setUsingNonZeroWinding(false);
    return p;
}

void paintTints(juce::Graphics& g, const Style& st) {
    // one pastel bloom per tab, strongest at the tab centre
    const juce::Point<float> centres[] = {{-0.25f, -0.25f}, {0.25f, -0.25f}, {-0.25f, 0.25f}, {0.25f, 0.25f}};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto c = centres[i];
        const auto corner = c * 2.0f;
        juce::ColourGradient grad(st.tints[i].withAlpha(0.62f), c.x, c.y, st.tints[i].withAlpha(0.14f), corner.x, corner.y, true);
        g.setGradientFill(grad);
        g.fillRect(juce::Rectangle<float>(0.5f, 0.5f).withCentre(c));
    }
}

// Concentric print rings, like a target tie-dye, alternating through the four tints. With
// motion they drift outwards and fade at both ends of their run so none pops in or out.
void paintRipples(juce::Graphics& g, const Style& st, const Motion* motion) {
    constexpr float r0 = 0.085f, span = 0.665f, pitch = 0.07f;
    constexpr int rings = static_cast<int>(span / pitch) + 1;
    const float drift = motion != nullptr ? std::fmod(motion->time * 0.028f * juce::jlimit(0.0f, 1.0f, motion->depth), span) : 0.0f;
    for (int i = 0; i < rings; ++i) {
        const float u = std::fmod(static_cast<float>(i) * pitch + drift, span);
        const float r = r0 + u;
        const float alpha = motion != nullptr ? 0.19f * std::sin(kPi * u / span) : 0.16f;
        g.setColour(st.tints[static_cast<std::size_t>(i % 4)].withAlpha(alpha));
        g.drawEllipse(juce::Rectangle<float>(2 * r, 2 * r).withCentre({0.0f, 0.0f}), 0.011f);
    }
}

void paintPrint(juce::Graphics& g, const Style& st, float sidePx, const Motion* motion) {
    const bool detail = sidePx >= 110.0f;
    if (!detail) motion = nullptr;   // too small for the print to move meaningfully
    if (detail) paintRipples(g, st, motion);

    // molecule, fitted to the sheet
    const float labelHeight = 0.62f;
    auto mol = buildMolecule(detail, labelHeight, motion);
    const float fit = detail ? 0.80f : 0.76f;
    const float k = fit / juce::jmax(mol.bounds.getWidth(), mol.bounds.getHeight());
    const auto t = juce::AffineTransform::translation(-mol.bounds.getCentreX(), -mol.bounds.getCentreY()).scaled(k, k);
    mol.bonds.applyTransform(t);

    const float stroke = juce::jmax(detail ? 0.024f : 0.03f, 1.15f / sidePx);
    for (auto& label : mol.labels) {
        label.plate.applyTransform(t);
        g.setColour(st.paper.withAlpha(label.alpha));
        g.fillPath(label.plate);
    }
    g.setColour(st.ink);
    g.strokePath(mol.bonds, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    for (auto& label : mol.labels) {
        label.glyph.applyTransform(t);
        g.setColour(st.ink.withAlpha(label.alpha));
        g.fillPath(label.glyph);
    }
    // growing ends glow in the band colours
    for (const auto& tip : mol.tips) {
        const auto p = tip.at.transformedBy(t);
        const auto c = st.tints[static_cast<std::size_t>(tip.tint)];
        g.setColour(c.withAlpha(0.35f));
        g.fillEllipse(juce::Rectangle<float>(0.11f, 0.11f).withCentre(p));
        g.setColour(c.brighter(0.4f));
        g.fillEllipse(juce::Rectangle<float>(0.045f, 0.045f).withCentre(p));
    }
}

float textWidth(const juce::Font& f, const juce::String& s) {
    juce::GlyphArrangement ga;
    ga.addLineOfText(f, s, 0.0f, 0.0f);
    return ga.getBoundingBox(0, -1, true).getWidth();
}

// Placement of the sheet inside `area`, shared by the static and animated drawing.
struct Layout {
    float side = 0.0f, physical = 0.0f;
    bool notched = false;
    juce::AffineTransform xf;
    juce::Path sheetScreen;
};

bool layoutSheet(Layout& out, juce::Rectangle<float> area, float pixelScale, const Style& st) {
    const float box = juce::jmin(area.getWidth(), area.getHeight());
    if (box < 4.0f) return false;
    const float tilt = juce::degreesToRadians(st.tiltDegrees);
    // shrink so the rotated square still fits the box
    out.side = box / (std::abs(std::cos(tilt)) + std::abs(std::sin(tilt)));
    // level of detail follows the pixels actually rendered, so Retina gets the full print
    out.physical = out.side * pixelScale;
    out.notched = out.physical >= 40.0f;
    const auto centre = area.getCentre();
    out.xf = juce::AffineTransform::scale(out.side).rotated(tilt).translated(centre.x, centre.y);
    out.sheetScreen = buildSheet(out.notched, out.notched);
    out.sheetScreen.applyTransform(out.xf);
    return true;
}

// Shadow, paper and tints: everything that does not move.
void paintBase(juce::Graphics& g, const Layout& lay, const Style& st) {
    if (st.shadow && lay.notched) {
        juce::DropShadow(st.ink.withAlpha(0.45f), juce::jmax(1, juce::roundToInt(lay.side * 0.06f)), {0, juce::roundToInt(lay.side * 0.025f)}).drawForPath(g, lay.sheetScreen);
    }
    g.setColour(st.paper);
    g.fillPath(lay.sheetScreen);

    g.saveState();
    g.reduceClipRegion(lay.sheetScreen);
    g.addTransform(lay.xf);
    paintTints(g, st);
    g.restoreState();

    if (!lay.notched) {
        g.setColour(st.ink.withAlpha(0.35f));
        g.strokePath(lay.sheetScreen, juce::PathStrokeType(1.0f));
        // gutters as fine lines when the holes would be sub-pixel
        g.saveState();
        g.addTransform(lay.xf);
        g.setColour(st.ink.withAlpha(0.45f));
        g.drawLine(0.0f, -0.5f, 0.0f, 0.5f, 1.0f / lay.side);
        g.drawLine(-0.5f, 0.0f, 0.5f, 0.0f, 1.0f / lay.side);
        g.restoreState();
    }
}

void paintPrintClipped(juce::Graphics& g, const Layout& lay, const Style& st, const Motion* motion) {
    g.saveState();
    g.reduceClipRegion(lay.sheetScreen);
    g.addTransform(lay.xf);
    paintPrint(g, st, lay.physical, motion);
    g.restoreState();
}

}  // namespace

void drawGlyph(juce::Graphics& g, juce::Rectangle<float> area, const Style& st) {
    Layout lay;
    if (!layoutSheet(lay, area, g.getInternalContext().getPhysicalPixelScaleFactor(), st)) return;
    paintBase(g, lay, st);
    paintPrintClipped(g, lay, st, nullptr);
}

void Animator::draw(juce::Graphics& g, juce::Rectangle<float> area, const Motion& motion, const Style& st) {
    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    Layout lay;
    if (!layoutSheet(lay, area, scale, st)) return;

    // the base is cached at physical resolution, with a margin for the shadow
    const auto pad = std::ceil(lay.side * 0.12f);
    const auto baseArea = area.expanded(pad);
    if (!base_.isValid() || baseArea != baseArea_ || std::abs(scale - baseScale_) > 1e-4f) {
        const int w = juce::jmax(1, juce::roundToInt(baseArea.getWidth() * scale));
        const int h = juce::jmax(1, juce::roundToInt(baseArea.getHeight() * scale));
        base_ = juce::Image(juce::Image::ARGB, w, h, true);
        juce::Graphics bg(base_);
        bg.addTransform(juce::AffineTransform::translation(-baseArea.getX(), -baseArea.getY()).scaled(scale));
        paintBase(bg, lay, st);
        baseArea_ = baseArea;
        baseScale_ = scale;
    }
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.drawImage(base_, baseArea);
    paintPrintClipped(g, lay, st, &motion);
}

float wordmarkWidth(float areaHeight, float textHeight) {
    return areaHeight + textHeight * 0.42f + textWidth(TetraLookAndFeel::font(textHeight, Weight::SemiBold), "TetraDrive");
}

float drawWordmark(juce::Graphics& g, juce::Rectangle<float> area, float textHeight, const Style& st) {
    const float h = area.getHeight();
    drawGlyph(g, area.removeFromLeft(h), st);
    area.removeFromLeft(textHeight * 0.42f);
    const auto font = TetraLookAndFeel::font(textHeight, Weight::SemiBold);
    g.setFont(font);
    g.setColour(colours::textPrimary);
    g.drawText("TetraDrive", area, juce::Justification::centredLeft);
    return wordmarkWidth(h, textHeight);
}

juce::Image renderGlyph(int sizePx, const Style& st) {
    juce::Image img(juce::Image::ARGB, sizePx, sizePx, true);
    juce::Graphics g(img);
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    const float px = static_cast<float>(sizePx);
    drawGlyph(g, juce::Rectangle<float>(px, px).reduced(px * 0.04f), st);
    return img;
}

}  // namespace tdui::logo
