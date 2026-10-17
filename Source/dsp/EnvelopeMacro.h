#pragma once

#include <JuceHeader.h>

namespace horizon
{

/**
    ATTACK/RELEASE: the macro's mapping and the time it shows.

    One exponential curve from 1% of each layer's designed time at 0% to the
    designed time itself at 100% (brief A-009, 2026-10-14): every 10% of travel
    multiplies the time by the same 1.58, so the short times worship playing
    lives in get most of the knob - 0-50% leads and plucks (Root attack
    22-220 ms), 50-80% in-between (0.2-0.9 s), 80-100% pads (0.9-2.2 s).

    0% keeps the old floor: each layer's attack/release in the ~10-25 ms
    range, short and percussive but clear of the ~1-5 ms where an envelope
    ramp starts audibly clicking; LayerBase::beginNote()'s jmax(0.001f, ...)
    floor remains the hard backstop underneath. Written as kMinScale^(1 - x)
    so that 100% is exactly 1.0 - the designed envelope, bit for bit (the G5
    null test runs there).

    The knob and the host show a time, not a percentage (owner, 2026-10-17):
    Root's, because Root is the foundation every chord stands on and A-009
    quotes its times. The other pads scale with it - Bloom about half as long,
    Clearing and Expanse a little longer on the attack.
*/
inline float attackReleaseTimeScale (float macroValue) noexcept
{
    constexpr float kMinScale = 0.01f;
    return std::pow (kMinScale, 1.0f - juce::jlimit (0.0f, 1.0f, macroValue));
}

/** Root's designed envelope (WarmFoundationLayer returns these): the times
    ATTACK and RELEASE show at 100%. */
inline constexpr float kRootAttackSeconds = 2.2f;
inline constexpr float kRootReleaseSeconds = 2.4f;

/** "22 ms", "0.64 s", "2.20 s" - the precision a player can hear. */
inline juce::String envelopeTimeText (float seconds)
{
    if (seconds < 0.1f)
        return juce::String (juce::roundToInt (seconds * 1000.0f)) + " ms";

    return juce::String (seconds, 2) + " s";
}

/** The text a knob or host shows for an ATTACK/RELEASE value 0..1. */
inline juce::String envelopeMacroText (float macroValue, float referenceSeconds)
{
    return envelopeTimeText (referenceSeconds * attackReleaseTimeScale (macroValue));
}

/** The inverse, for typed values: "0.5", "0.5 s", "500 ms", "500ms", or
    "40 %" for the knob position. Clamped to the knob's range. */
inline float envelopeMacroFromText (const juce::String& text, float referenceSeconds)
{
    const auto t = text.trim().toLowerCase();
    const auto number = t.getFloatValue();

    if (t.endsWith ("%"))
        return juce::jlimit (0.0f, 1.0f, number * 0.01f);

    const auto seconds = t.endsWith ("ms") ? number * 0.001f : number;

    if (seconds <= 0.0f)
        return 0.0f;

    // scale = 0.01^(1 - x)  ->  x = 1 - ln(scale) / ln(0.01)
    const auto scale = seconds / referenceSeconds;
    return juce::jlimit (0.0f, 1.0f, 1.0f - std::log (scale) / std::log (0.01f));
}

} // namespace horizon
