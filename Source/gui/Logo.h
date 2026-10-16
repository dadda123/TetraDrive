// Procedural TetraDrive artwork: a four-tab blotter printed with the four-ring ergoline core
// of the LSD molecule. Four rings, four tabs, four bands. Pure paths, so it is sharp at any size.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

namespace tdui::logo {

struct Style {
    juce::Colour paper{0xfff4efe4};
    juce::Colour ink{0xff0e1014};
    std::array<juce::Colour, 4> tints{{juce::Colour{0xff3ec6e0}, juce::Colour{0xff5dd39e},
                                      juce::Colour{0xfff2b33d}, juce::Colour{0xffe15fb0}}};
    float tiltDegrees = -8.0f;
    bool shadow = true;
};

// Live state for the animated print. `reveal` draws the molecule in from the ring junction
// outwards (0 = blank paper, 1 = complete); `time` (seconds) drives the ripple rings and the
// pseudo-3D flex of the skeleton; `depth` scales that motion (0 = perfectly still).
struct Motion {
    float time = 0.0f;
    float reveal = 1.0f;
    float depth = 1.0f;
};

// Draws the blotter inside the largest centred square of `area` (the tilt stays within it).
void drawGlyph(juce::Graphics&, juce::Rectangle<float> area, const Style& = {});

// Frame-by-frame drawing of the same glyph. The paper, its shadow and the tints are rendered
// once and cached; only the print (rings and molecule) is redrawn, so it is cheap at 60 fps.
class Animator {
public:
    void draw(juce::Graphics&, juce::Rectangle<float> area, const Motion&, const Style& = {});

private:
    juce::Image base_;
    juce::Rectangle<float> baseArea_;
    float baseScale_ = 0.0f;
};

// Blotter followed by the "TetraDrive" wordmark, vertically centred in `area`; returns the width used.
float drawWordmark(juce::Graphics&, juce::Rectangle<float> area, float textHeight, const Style& = {});
float wordmarkWidth(float areaHeight, float textHeight);

// Transparent ARGB render of the glyph alone, for icons and exports.
juce::Image renderGlyph(int sizePx, const Style& = {});

}  // namespace tdui::logo
