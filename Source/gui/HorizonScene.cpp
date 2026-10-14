#include "HorizonScene.h"

namespace horizon::ui
{

namespace
{
    // Spring speeds (see motion::Spring): a hand on the knob, a preset
    // recall, the sunrise on open.
    constexpr float kFollowOmega  = 9.0f;  // ~0.5 s
    constexpr float kRecallOmega  = 4.7f;  // ~1 s
    constexpr float kSunriseOmega = 3.4f;  // ~1.4 s

    // The low glow (a sun just behind the ridge): centre 40 px above the
    // window's bottom edge, 640 px across before WIDTH stretches it.
    constexpr float kGlowDiameter = 640.0f;
    constexpr float kGlowCentreFromBottom = 40.0f;

    // The ridge: the bottom 120 px, plus margins so the widest blur can
    // feather inside the mask instead of being cut off at its edges.
    constexpr float kRidgeHeight = 120.0f;
    constexpr int kRidgeSideMargin = 48;
    constexpr int kRidgeBlurMargin = 48;

    //==========================================================================
    // OKLab mixing: the palette came from oklch() values in the design
    // handoff, and mixing in the same space keeps a dusk-to-day shift free of
    // the muddy midpoints a straight sRGB lerp gives (Bjorn Ottosson's
    // OKLab formulas).
    struct Lab { float L, a, b, alpha; };

    float toLinear (float c) noexcept   { return c <= 0.04045f ? c / 12.92f : std::pow ((c + 0.055f) / 1.055f, 2.4f); }
    float fromLinear (float c) noexcept { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow (c, 1.0f / 2.4f) - 0.055f; }

    Lab toLab (juce::Colour c) noexcept
    {
        const auto r = toLinear (c.getFloatRed()), g = toLinear (c.getFloatGreen()), b = toLinear (c.getFloatBlue());

        const auto l = std::cbrt (0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b);
        const auto m = std::cbrt (0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b);
        const auto s = std::cbrt (0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b);

        return { 0.2104542553f * l + 0.7936177850f * m - 0.0040720468f * s,
                 1.9779984951f * l - 2.4285922050f * m + 0.4505937099f * s,
                 0.0259040371f * l + 0.7827717662f * m - 0.8086757660f * s,
                 c.getFloatAlpha() };
    }

    juce::Colour fromLab (Lab c) noexcept
    {
        const auto l = std::pow (c.L + 0.3963377774f * c.a + 0.2158037573f * c.b, 3.0f);
        const auto m = std::pow (c.L - 0.1055613458f * c.a - 0.0638541728f * c.b, 3.0f);
        const auto s = std::pow (c.L - 0.0894841775f * c.a - 1.2914855480f * c.b, 3.0f);

        const auto channel = [] (float linear)
        {
            return (juce::uint8) juce::roundToInt (juce::jlimit (0.0f, 1.0f, fromLinear (linear)) * 255.0f);
        };

        return juce::Colour (channel ( 4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s),
                             channel (-1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s),
                             channel (-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s),
                             juce::jlimit (0.0f, 1.0f, c.alpha));
    }

    juce::Colour mixOklab (juce::Colour from, juce::Colour to, float t) noexcept
    {
        if (t <= 0.0f) return from;
        if (t >= 1.0f) return to;

        const auto a = toLab (from), b = toLab (to);
        return fromLab ({ a.L + (b.L - a.L) * t, a.a + (b.a - a.a) * t,
                          a.b + (b.b - a.b) * t, a.alpha + (b.alpha - a.alpha) * t });
    }

    //==========================================================================
    // FILTER at 50% (its neutral, the sound design's own brightness) is the
    // panel exactly as designed; 0% is night falling, 100% golden hour.
    // Kept dark enough at every point that the wordmark, the preset pills and
    // the footer - drawn straight on the panel - stay readable.
    struct Stops { juce::Colour top, upper, lower, bottom, glow; };

    const Stops& designedStops()
    {
        static const Stops s { Palette::panelGradientTop, Palette::panelGradientUpper,
                               Palette::panelGradientLower, Palette::panelGradientBottom, Palette::glow };
        return s;
    }

    const Stops& nightStops()
    {
        static const Stops s { juce::Colour (0xff182032), juce::Colour (0xff34354f),
                               juce::Colour (0xff5e3640), juce::Colour (0xff8a3b26), juce::Colour (0x26e2825a) };
        return s;
    }

    // Golden hour keeps the lower sky and the glow at the designed
    // brightness - the footer text sits there, and the design already gives
    // it the least contrast on the panel - and puts the warmth into the
    // upper sky and the glow's hue.
    const Stops& goldenStops()
    {
        static const Stops s { juce::Colour (0xff514e37), juce::Colour (0xff9c752e),
                               juce::Colour (0xffb45a1c), juce::Colour (0xffcc4c16), juce::Colour (0x34ffc36a) };
        return s;
    }

    Stops mixStops (const Stops& a, const Stops& b, float t)
    {
        return { mixOklab (a.top, b.top, t), mixOklab (a.upper, b.upper, t), mixOklab (a.lower, b.lower, t),
                 mixOklab (a.bottom, b.bottom, t), mixOklab (a.glow, b.glow, t) };
    }

    /** The palette for a FILTER value. Night closes in a little faster than
        the light warms: the default patch (FILTER 30%) only leans toward
        dusk, Mitternachtsblau (12%) is well into it. */
    Stops paletteAt (float filter)
    {
        const auto f = juce::jlimit (0.0f, 1.0f, filter);

        return f < 0.5f ? mixStops (designedStops(), nightStops(), std::pow ((0.5f - f) / 0.5f, 1.25f))
                        : mixStops (designedStops(), goldenStops(), (f - 0.5f) / 0.5f);
    }

    /** REVERB -> the ridge's blur. 20% (the default) is the blur as designed
        (JUCE's Gaussian, radius 14, on a 33-tap kernel); dry air is a little
        crisper, a full wet tail much hazier. */
    float ridgeSigma (float reverb) noexcept
    {
        return reverb < 0.2f ? juce::jmap (reverb, 0.0f, 0.2f, 9.0f, 14.0f)
                             : juce::jmap (juce::jmin (reverb, 1.0f), 0.2f, 1.0f, 14.0f, 30.0f);
    }

    /** Separable Gaussian, truncated at the same 1.14 sigma the designed
        33-tap kernel was (16 taps either side at sigma 14) so every haze
        level keeps the designed character; zero outside the image, as
        ImageConvolutionKernel::applyToImage() treated it. */
    void gaussianBlur (std::vector<float>& pixels, int width, int height, float sigma)
    {
        const auto radius = juce::jmax (1, (int) std::ceil (sigma * (16.0f / 14.0f)));
        std::vector<float> kernel ((size_t) (2 * radius + 1));
        auto sum = 0.0f;

        for (int k = -radius; k <= radius; ++k)
            sum += kernel[(size_t) (k + radius)] = std::exp (-(float) (k * k) / (2.0f * sigma * sigma));

        for (auto& k : kernel)
            k /= sum;

        std::vector<float> temp (pixels.size(), 0.0f);

        for (int y = 0; y < height; ++y)
        {
            const auto* row = pixels.data() + (size_t) y * (size_t) width;
            auto* out = temp.data() + (size_t) y * (size_t) width;

            for (int x = 0; x < width; ++x)
            {
                auto acc = 0.0f;

                for (int k = juce::jmax (-radius, -x); k <= juce::jmin (radius, width - 1 - x); ++k)
                    acc += row[x + k] * kernel[(size_t) (k + radius)];

                out[x] = acc;
            }
        }

        for (int y = 0; y < height; ++y)
        {
            auto* out = pixels.data() + (size_t) y * (size_t) width;

            for (int x = 0; x < width; ++x)
            {
                auto acc = 0.0f;

                for (int k = juce::jmax (-radius, -y); k <= juce::jmin (radius, height - 1 - y); ++k)
                    acc += temp[(size_t) (y + k) * (size_t) width + (size_t) x] * kernel[(size_t) (k + radius)];

                out[x] = acc;
            }
        }
    }
}

//==============================================================================
void HorizonScene::setTargets (float filter, float reverb, float width) noexcept
{
    timeOfDay.target = juce::jlimit (0.0f, 1.0f, filter);
    haze.target = juce::jlimit (0.0f, 1.0f, reverb);
    spread.target = juce::jlimit (0.0f, 1.0f, width);
    glowLevel.target = 1.0f;
}

void HorizonScene::snapToTargets() noexcept
{
    for (auto* s : { &timeOfDay, &haze, &spread, &glowLevel })
        s->snapTo (s->target);

    rising = recallGlide = false;
    dirty = Change::all;
}

void HorizonScene::prepareSunrise() noexcept
{
    timeOfDay.value = 0.0f;
    timeOfDay.velocity = 0.0f;
    glowLevel.value = 0.0f;
    glowLevel.velocity = 0.0f;
    rising = true;
    dirty = Change::all;
}

HorizonScene::Change HorizonScene::advance (float dt) noexcept
{
    const auto omega = rising ? kSunriseOmega : (recallGlide ? kRecallOmega : kFollowOmega);

    const auto airOmega = recallGlide ? kRecallOmega : kFollowOmega;

    const bool skyMoved = timeOfDay.advance (dt, omega, 2.0e-3f);
    const bool glowMoved = glowLevel.advance (dt, omega, 2.0e-3f);
    const bool hazeMoved = haze.advance (dt, airOmega, 2.0e-3f);
    const bool spreadMoved = spread.advance (dt, airOmega, 2.0e-3f);
    const bool airMoved = hazeMoved || spreadMoved;

    if (timeOfDay.isSettled() && glowLevel.isSettled())
        rising = false;

    if (timeOfDay.isSettled() && haze.isSettled() && spread.isSettled())
        recallGlide = false;

    const auto change = skyMoved ? Change::all : ((glowMoved || airMoved) ? Change::lower : Change::none);

    if (change == Change::all || (change == Change::lower && dirty == Change::none))
        dirty = change;

    return change;
}

juce::Rectangle<int> HorizonScene::lowerRegion (juce::Rectangle<int> bounds) noexcept
{
    const auto top = bounds.getBottom() - (int) (kGlowCentreFromBottom + kGlowDiameter * 0.5f) - 2;
    return bounds.withTop (juce::jmax (bounds.getY(), top));
}

juce::ColourGradient HorizonScene::skyGradient (float top, float bottom) const
{
    const auto p = paletteAt (timeOfDay.value);
    juce::ColourGradient grad (p.top, 0.0f, top, p.bottom, 0.0f, bottom, false);
    grad.addColour (0.55, p.upper);
    grad.addColour (0.82, p.lower);
    return grad;
}

juce::Colour HorizonScene::skyColourAt (float proportionDown) const
{
    return skyGradient (0.0f, 1.0f).getColourAtPosition (juce::jlimit (0.0, 1.0, (double) proportionDown));
}

void HorizonScene::paint (juce::Graphics& g, juce::Rectangle<float> bounds)
{
    const auto area = bounds.getSmallestIntegerContainer();

    // The cache matches the screen's pixel density, so drawing it is a 1:1
    // copy - no resampling, and no clip (the corners are built in).
    const auto scale = juce::jlimit (1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const auto pixelWidth = juce::jmax (1, juce::roundToInt ((float) area.getWidth() * scale));
    const auto pixelHeight = juce::jmax (1, juce::roundToInt ((float) area.getHeight() * scale));

    if (cache.isNull() || cache.getWidth() != pixelWidth || cache.getHeight() != pixelHeight)
    {
        if (ridgeBounds.getWidth() != area.getWidth() + 2 * kRidgeSideMargin)
            ridgeMasks.clear();

        cache = juce::Image (juce::Image::ARGB, pixelWidth, pixelHeight, true);
        cacheScale = scale;
        dirty = Change::all;
    }

    if (dirty != Change::none)
    {
        const auto local = area.withZeroOrigin();
        renderCache (local, dirty == Change::all ? local : lowerRegion (local));
        dirty = Change::none;
    }

    g.drawImageTransformed (cache, juce::AffineTransform::scale (1.0f / cacheScale)
                                       .translated ((float) area.getX(), (float) area.getY()));
}

void HorizonScene::renderCache (juce::Rectangle<int> bounds, juce::Rectangle<int> region)
{
    const auto pixelRegion = (region.toFloat() * cacheScale).getSmallestIntegerContainer()
                                                            .getIntersection (cache.getBounds());
    const auto r = bounds.toFloat();
    const auto p = paletteAt (timeOfDay.value);

    // --- Sky: vertical only, the same across any row (PresetBar relies on
    // that for its fade colour, see skyColourAt()) - so it is filled a row
    // at a time, one colour per row, the same colours a gradient fill gives.
    // CoreGraphics' own gradient fill took 8.8 ms for the window at 2x.
    {
        const auto grad = skyGradient (0.0f, 1.0f);
        const juce::Image::BitmapData data (cache, pixelRegion.getX(), pixelRegion.getY(),
                                            pixelRegion.getWidth(), pixelRegion.getHeight(),
                                            juce::Image::BitmapData::writeOnly);

        for (int y = 0; y < data.height; ++y)
        {
            const auto proportion = ((double) (pixelRegion.getY() + y) + 0.5) / (double) cache.getHeight();
            const auto pixel = grad.getColourAtPosition (proportion).getPixelARGB();
            auto* line = reinterpret_cast<juce::PixelARGB*> (data.getLinePointer (y));
            std::fill (line, line + data.width, pixel);
        }
    }

    juce::Graphics g (cache);
    g.addTransform (juce::AffineTransform::scale (cacheScale));
    g.reduceClipRegion (region);

    // --- The low glow, drawn before the ridge so the mountains read as a
    // dark silhouette cut into it. WIDTH stretches it sideways; 40% (the
    // default) is the designed 640 px.
    {
        const auto glow = p.glow.withMultipliedAlpha (glowLevel.value);
        const juce::Point<float> centre (r.getCentreX(), r.getBottom() - kGlowCentreFromBottom);
        const auto stretch = 0.45f + 1.4f * spread.value;   // a narrow spot (mono) to a band across the horizon

        juce::ColourGradient radial (glow, centre.x, centre.y,
                                     glow.withAlpha (0.0f), centre.x, centre.y - kGlowDiameter * 0.5f, true);

        juce::Graphics::ScopedSaveState save (g);
        g.addTransform (juce::AffineTransform::scale (stretch, 1.0f, centre.x, centre.y));
        g.setGradientFill (radial);
        g.fillEllipse (juce::Rectangle<float> (kGlowDiameter, kGlowDiameter).withCentre (centre));
    }

    // --- The ridge: soft dark waves, blurred more as REVERB rises and
    // lifted toward the air's colour above the default (aerial perspective).
    if (ridgeMasks.empty())
        buildRidgeMasks (bounds);

    blendRidgeMask (ridgeSigma (haze.value));

    const auto lift = 0.32f * juce::jmax (0.0f, (haze.value - 0.2f) / 0.8f);
    g.setColour (mixOklab (Palette::skyline.withAlpha (1.0f), p.lower, lift));
    g.drawImage (ridgeMask, ridgeBounds.toFloat(), juce::RectanglePlacement::stretchToFit, true);

    // --- The border, along the window's edge. The panel fills the window
    // edge to edge: a host's plugin window is rectangular, and the rounded
    // corners the design had (on a dark web page) left four unpainted
    // corners for the host's own background to show through.
    // Half a pixel wide: the design's 1 px stroke sat centred on the
    // panel's edge, so only its inner half ever showed.
    g.setColour (Palette::panelBorder);
    g.drawRect (r, 0.5f);
}

void HorizonScene::buildRidgeMasks (juce::Rectangle<int> bounds)
{
    // Masks are built at half resolution: after even the lightest blur
    // there is no detail a full-resolution mask would add, and it makes the
    // four blurs (once per editor) four times cheaper.
    const auto width = bounds.getWidth() + 2 * kRidgeSideMargin;
    const auto height = (int) kRidgeHeight + 2 * kRidgeBlurMargin;

    ridgeBounds = { bounds.getX() - kRidgeSideMargin,
                    bounds.getBottom() - (int) kRidgeHeight - kRidgeBlurMargin, width, height };
    maskWidth = (width + 1) / 2;
    maskHeight = (height + 1) / 2;

    // The ridge outline - the same points the design always used, as
    // percentages of the visible width and of the 120 px ridge height - is
    // solid down past the bottom margin, so the blur feathers inside the
    // mask and the true bottom edge stays opaque. It runs flat out into the
    // side margins for the same reason.
    juce::Image outline (juce::Image::SingleChannel, maskWidth, maskHeight, true, juce::SoftwareImageType());
    {
        juce::Graphics g (outline);
        g.addTransform (juce::AffineTransform::scale (0.5f));

        const float pointsPct[][2] {
            { 0.0f, 78.0f }, { 9.0f, 60.0f }, { 18.0f, 82.0f },
            { 29.0f, 55.0f }, { 40.0f, 84.0f }, { 52.0f, 58.0f }, { 64.0f, 86.0f },
            { 76.0f, 56.0f }, { 88.0f, 80.0f }, { 100.0f, 62.0f },
        };

        const auto visibleWidth = (float) bounds.getWidth();
        const auto top = (float) kRidgeBlurMargin;
        const auto pointAt = [&] (const float* pct)
        {
            return juce::Point<float> ((float) kRidgeSideMargin + pct[0] * 0.01f * visibleWidth,
                                       top + pct[1] * 0.01f * kRidgeHeight);
        };

        juce::Path ridge;
        ridge.startNewSubPath (0.0f, (float) height);
        ridge.lineTo (0.0f, pointAt (pointsPct[0]).y);

        for (auto& pct : pointsPct)
            ridge.lineTo (pointAt (pct));

        ridge.lineTo ((float) width, pointAt (pointsPct[juce::numElementsInArray (pointsPct) - 1]).y);
        ridge.lineTo ((float) width, (float) height);
        ridge.closeSubPath();

        g.setColour (juce::Colours::white);
        g.fillPath (ridge);
    }

    std::vector<float> coverage ((size_t) (maskWidth * maskHeight));
    {
        const juce::Image::BitmapData data (outline, juce::Image::BitmapData::readOnly);

        for (int y = 0; y < maskHeight; ++y)
            for (int x = 0; x < maskWidth; ++x)
                coverage[(size_t) (y * maskWidth + x)] = (float) *data.getPixelPointer (x, y) / 255.0f;
    }

    ridgeMasks.clear();

    for (auto sigma : { 9.0f, 14.0f, 21.0f, 30.0f })
    {
        RidgeMask mask { sigma, coverage };
        gaussianBlur (mask.coverage, maskWidth, maskHeight, sigma * 0.5f); // half resolution
        ridgeMasks.push_back (std::move (mask));
    }

    ridgeMask = juce::Image (juce::Image::SingleChannel, maskWidth, maskHeight, true, juce::SoftwareImageType());
    ridgeMaskSigma = -1.0f;
}

void HorizonScene::blendRidgeMask (float sigma)
{
    if (std::abs (sigma - ridgeMaskSigma) < 0.05f || ridgeMasks.empty())
        return;

    ridgeMaskSigma = sigma;

    size_t lower = 0;

    while (lower + 2 < ridgeMasks.size() && sigma > ridgeMasks[lower + 1].sigma)
        ++lower;

    const auto& a = ridgeMasks[lower];
    const auto& b = ridgeMasks[juce::jmin (lower + 1, ridgeMasks.size() - 1)];
    const auto t = b.sigma > a.sigma ? juce::jlimit (0.0f, 1.0f, (sigma - a.sigma) / (b.sigma - a.sigma)) : 0.0f;

    const juce::Image::BitmapData data (ridgeMask, juce::Image::BitmapData::writeOnly);

    for (int y = 0; y < maskHeight; ++y)
    {
        for (int x = 0; x < maskWidth; ++x)
        {
            const auto i = (size_t) (y * maskWidth + x);
            const auto v = a.coverage[i] + (b.coverage[i] - a.coverage[i]) * t;
            *data.getPixelPointer (x, y) = (juce::uint8) juce::roundToInt (juce::jlimit (0.0f, 1.0f, v) * 255.0f);
        }
    }
}

} // namespace horizon::ui
