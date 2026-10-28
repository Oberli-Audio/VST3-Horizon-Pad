#pragma once

#include "HorizonLookAndFeel.h"
#include "Motion.h"

namespace horizon::ui
{

/**
    The Scene (design system round 11): Horizon Pad's sky, drawn in a framed
    display above the control row - a sunset gradient, a low glow and a
    blurred mountain ridge - as a slow picture of the patch:

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
    is a change of light rather than a cut. It is the one place in the
    editor with gradients and blur; it never takes the accent.

    Scaled to its display (editor F, 2026-10-28): the ridge is
    min(120, h * 0.3) high (88 px in the 294 px display), its blur and the
    glow's centre scale with it, and the glow's diameter with w / 1080 - the
    numbers of the 1080 x 748 window it once filled. The display's 14 px
    corners (`radius-lg`) and its 1 px `line` border are part of the
    picture: the corners outside the curve are the editor's ground.

    Text on the sky (SkyDisplay's recalled preset title) is measured over
    the whole FILTER range with ux-ui-design-bob snapshot_contrast.py; the
    numbers are in SkyDisplay.h. Re-measure after a palette change.

    Rendered into an image at the screen's own pixel density and
    re-rendered only while something moves, so every repaint is a plain 1:1
    copy with no clip (the corners are built in). (Until 2026-10-14 the
    image was 1x, stretched to Retina inside a rounded-corner clip: 16 ms
    per full redraw on an M3 Pro; drawn 1:1 it is 0.3 ms.) Message thread
    only.
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

    /** The band Change::lower affects in a display of `bounds`: the glow
        and the ridge. */
    static juce::Rectangle<int> lowerRegion (juce::Rectangle<int> bounds) noexcept;

    /** Paints the display, rounded corners and border included - opaque
        over all of `bounds`. */
    void paint (juce::Graphics&, juce::Rectangle<float> bounds);

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
    float ridgeHeight = 120.0f;         // min(120, h * 0.3) of the display
    float ridgeScale = 1.0f;            // ridgeHeight / 120: blur and glow offset
    juce::Image ridgeMask;              // the blend for the current haze, half resolution
    float ridgeMaskSigma = -1.0f;
    juce::Rectangle<int> ridgeBounds;   // where the mask lands, in panel coordinates (full resolution)
    int maskWidth = 0, maskHeight = 0;
};

} // namespace horizon::ui
