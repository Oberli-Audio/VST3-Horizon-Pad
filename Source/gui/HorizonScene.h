#pragma once

#include "HorizonLookAndFeel.h"
#include "Motion.h"

namespace horizon::ui
{

/**
    The one shared panel that is the whole plugin window - sunset sky
    gradient, a low glow, a blurred mountain ridge along the bottom - drawn
    as a slow picture of the patch:

      FILTER -> time of day. 50% (the macro's neutral, the designed
                brightness) is the palette exactly as designed; darker
                settings move toward a dusk-blue sky, brighter ones toward
                golden hour.
      REVERB -> haze. More reverb softens the ridge and lifts it toward the
                colour of the air behind it (aerial perspective). The
                default (20%) is the blur as designed.
      WIDTH  -> the low glow spreads wider along the horizon. The default
                (40%) is the glow as designed.

    Each follows its parameter on a spring: about 1 s after a preset recall
    (beginRecallGlide), quicker under a hand on the knob, so a preset change
    is a change of light rather than a cut.

    The text drawn straight on the panel has to stay readable across the
    whole range. Measured with tools/gui_snapshot (2026-10-07), as designed
    (FILTER 50%) -> golden hour (100%): preset pills 5.3 -> 4.7:1, wordmark
    3.6 -> 3.35:1 (36 px: large text), footer 2.19 -> 2.12:1 (the design's
    own weakest spot, which is why golden hour keeps the lower sky and glow
    at the designed brightness). Every setting below 50% is darker, so
    higher: at 0% the footer reaches 4.2:1. Re-measure after a palette change.

    It fills the window edge to edge, opaque, with a 1 px border along the
    edge. (The design's panel had 24 px rounded corners - right on the dark
    web page of the handoff, wrong in a host's rectangular plugin window,
    where the unpainted corners showed the host's own background. Square
    since 2026-10-14.)

    Rendered into an image at the screen's own pixel density and
    re-rendered only while something moves - only the lower band when just
    REVERB or WIDTH move - so every repaint, even of a few pixels behind a
    glowing pad, is a plain 1:1 copy. (Until 2026-10-14 the image was 1x,
    stretched to Retina inside a rounded-corner clip: 16 ms per full redraw
    on an M3 Pro, and 96% of the editor's painting time; drawn 1:1 it is
    0.3 ms.) Message thread only.
*/
class HorizonScene
{
public:
    HorizonScene() = default;

    /** FILTER, REVERB and WIDTH, 0..1. */
    void setTargets (float filter, float reverb, float width) noexcept;
    void snapToTargets() noexcept;

    /** Glide to the next targets slowly (a preset recall or A/B switch). */
    void beginRecallGlide() noexcept { recallGlide = true; }

    /** E1, sunrise on open: start from night with the glow out; advance()
        then rises to the targets. */
    void prepareSunrise() noexcept;

    enum class Change { none, lower, all };

    /** Advances the springs. Returns what needs repainting. */
    Change advance (float dt) noexcept;

    /** The band Change::lower affects: the glow and the ridge. */
    static juce::Rectangle<int> lowerRegion (juce::Rectangle<int> bounds) noexcept;

    /** Paints the panel, border included - opaque over all of `bounds`. */
    void paint (juce::Graphics&, juce::Rectangle<float> bounds);

    /** The sky gradient's colour `proportionDown` of the way down the window
        (0 = top), without the glow - for components that fade into the sky
        (the preset row's scroll cue). */
    juce::Colour skyColourAt (float proportionDown) const;

private:
    juce::ColourGradient skyGradient (float top, float bottom) const;
    void renderCache (juce::Rectangle<int> bounds, juce::Rectangle<int> region);
    void buildRidgeMasks (juce::Rectangle<int> bounds);
    void blendRidgeMask (float sigma);

    motion::Spring timeOfDay, haze, spread, glowLevel;
    bool recallGlide = false;
    bool rising = false;

    // The panel at the screen's pixel density (cacheScale), and what of it
    // needs rendering again; see paint() and renderCache().
    juce::Image cache;
    float cacheScale = 1.0f;
    Change dirty = Change::all;

    struct RidgeMask
    {
        float sigma = 0.0f;
        std::vector<float> coverage;   // half resolution, 0..1
    };

    std::vector<RidgeMask> ridgeMasks;
    juce::Image ridgeMask;              // the blend for the current haze, half resolution
    float ridgeMaskSigma = -1.0f;
    juce::Rectangle<int> ridgeBounds;   // where the mask lands, in panel coordinates (full resolution)
    int maskWidth = 0, maskHeight = 0;
};

} // namespace horizon::ui
