#include "Presets.h"

namespace horizon
{

/*
    Factory programs.

    Reading the numbers:
      volumes = { Root (Warm Foundation), Clearing (Analog Ensemble),
                  Expanse (Airy Choir), Bloom (Motion Pad) }
      macros  = { Attack, Release, Filter, Reverb, Width, Detune }

    Values are the starting points worked out for the VST3 handoff, matching
    each preset's name against the four pads' existing character - a first
    pass for ear-tuning, not a final mix. Release mirrors attack - a starting
    point for a real sound-design pass, not deliberately tuned yet.

    WIDTH (2026-10-02) is each preset's former Expanse width. Expanse is the
    pad whose width profile is the identity, so the widest pad sits exactly
    where it did and the profiles narrow the other three (see
    LayerBase::widthProfile()). Until then every preset gave all four pads the
    same width, so the shape across the pads never changed between presets;
    now it changes with the knob. DETUNE is 0.5, the designed detune, on
    the pads; leads and in-betweens set their own (Funkenflug 65% for its
    supersaw edge, Glasperle 60% so Expanse's triangles beat).

    Three families since 2026-10-07, in repeating cycles (see the bank-order block):
    PADS, IN-BETWEEN (onset ~0.2-0.45 s) and LEADS (onset 15-165 ms).
    Every preset is loudness-matched to the pad bank's median, -16.9 LUFS
    (tools/measure/descriptors.py --presets).

    ATTACK/RELEASE since 2026-10-14 (brief A-009): one exponential curve from
    1% of each layer's designed time at 0% to the designed time at 100%
    (attackReleaseTimeScale() in PluginProcessor.cpp; before, 50% was the
    designed time and 100% double it). The owner's listening test found the
    pads cinematic, not worship: every PAD now has a third of its former
    attack and release - ATTACK/RELEASE 73-87%, Root 0.64-1.19 s attack and
    0.70-1.30 s release before the room - while IN-BETWEEN (48-72% / 78-90%)
    and LEADS (0-42% / 30-60%) keep exactly the times they had, re-expressed
    on the new curve. Their releases are now longer than the pads'.
*/

static const std::vector<Preset>& buildPresets()
{
    static const std::vector<Preset> presets
    {
        // ==================================================================
        // Bank order (2026-10-07, owner): repeating cycles, each from the
        // longest attack to the shortest - pads, then in-between, then a
        // lead - so browsing never jumps from a slow pad straight to a lead.
        // (By attack: since A-009 the in-betweens release longer than the
        // pads.) The first cycle is the five most different sounds in the bank
        // (chosen by measured distance, one or more per family); every later
        // cycle revisits the families with their variants.
        //   1-5    Alpengluehen, Lagerfeuer, Talwind, Frostklang, Funkenflug
        //   6-9    Steinerne Ruhe, Bergecho, Kupferglanz, Silberpfad
        //   10-13  Mitternachtsblau, Ruhepuls, Sturmfront, Glasperle
        //   14-17  Sternenzelt, Daemmerlicht, Goldstaub, Klarheit
        // The plugin opens on Lagerfeuer (#2): the parameter defaults are its
        // values, and the processor selects the preset they equal.
        // docs/preset-curation-2026-09-23.md records method and numbers.
        // ==================================================================

        // ------------------------------------------------------------------
        // [PAD]
        // 1. ALPENGLUHEN (alpenglow) - warm light spreading across the peaks.
        // Clearing steps forward, wide image, medium reverb.
        //
        // juce::String's const-char* constructor assumes plain ASCII (see
        // Presets.h) and asserts - crashing outright in this project's build
        // - on the high bytes in the raw \xc3\xbc (u-umlaut) UTF-8 escape, so
        // this one name has to be built explicitly as UTF-8 instead of via
        // the struct's normal aggregate-init string literal.
        // ------------------------------------------------------------------
        {
            juce::String (juce::CharPointer_UTF8 ("Alpengl\xc3\xbchen")),
            "Warm light spreading wide across the peaks.",
            { 0.307f, 0.443f, 0.375f, 0.239f },
            { 0.7915f, 0.7915f, 0.65f, 0.45f, 0.80f, 0.50f }
        },

        // ------------------------------------------------------------------
        // [PAD]
        // 2. LAGERFEUER (campfire) - warm, close, grounded. Root carries it,
        // narrow width for an intimate, near-the-fire feel, small reverb.
        // 2026-10-07: DETUNE 0% - the owner preferred the default patch at 0%
        // over the designed detune in the blind A/B (the parameter default follows).
        // ------------------------------------------------------------------
        {
            "Lagerfeuer",
            "Warm, close and grounded - the campfire pad.",
            { 0.477f, 0.191f, 0.096f, 0.159f },
            { 0.7313f, 0.7313f, 0.30f, 0.20f, 0.40f, 0.00f }
        },

        // ------------------------------------------------------------------
        // [IN-BETWEEN]
        // 3. TALWIND (valley wind) - movement and breeze. Bloom carries it,
        // wide image, snappier attack.
        // 2026-10-07: converted from pad to IN-BETWEEN - same layer balance,
        // ATTACK/RELEASE moved into the fast zone (onset ~0.2-0.45 s,
        // release ~0.8-1.4 s with the room), so it bridges the leads and pads.
        // ------------------------------------------------------------------
        {
            "Talwind",
            "Movement and breeze - a moving synth that answers in a breath.",
            { 0.269f, 0.269f, 0.269f, 0.503f },
            { 0.6561f, 0.8946f, 0.50f, 0.35f, 0.70f, 0.50f }
        },

        // ------------------------------------------------------------------
        // [IN-BETWEEN]
        // 4. FROSTKLANG (frost sound) - cold, sharp, bright. Bright.
        // 2026-10-07: converted from pad to IN-BETWEEN - same layer balance,
        // ATTACK/RELEASE moved into the fast zone (onset ~0.2-0.45 s,
        // release ~0.8-1.4 s with the room), so it bridges the leads and pads.
        // ------------------------------------------------------------------
        {
            "Frostklang",
            "Cold, bright and sharp - a frozen ring that speaks at once.",
            { 0.298f, 0.298f, 0.695f, 0.346f },
            { 0.5368f, 0.7754f, 0.75f, 0.35f, 0.55f, 0.55f }
        },

        // ------------------------------------------------------------------
        // [LEAD]
        // 5. FUNKENFLUG (flying sparks) - bright saw lead. Clearing's unison
        // saws lead, DETUNE above the design for a supersaw edge, Root for
        // body. Medium-fast: 74 ms onset at C5 (Clearing's stack starts
        // slower than the other pads), 22 ms at C6; release ~0.2 s.
        // ------------------------------------------------------------------
        {
            "Funkenflug",
            "Bright saw lead - sparks flying, cuts through the band.",
            { 0.281f, 0.514f, 0.187f, 0.047f },
            { 0.0000f, 0.2982f, 0.65f, 0.20f, 0.25f, 0.65f }
        },

        // ------------------------------------------------------------------
        // [PAD]
        // 6. STEINERNE RUHE (stillness in stone) - minimal, sparse, slow.
        // 2026-10-07: volumes -0.5 dB (it measured +1.0 LU over the bank median).
        // ------------------------------------------------------------------
        {
            "Steinerne Ruhe",
            "Stillness carved in stone - minimal, slow and sparse.",
            { 0.476f, 0.273f, 0.273f, 0.203f },
            { 0.8668f, 0.8668f, 0.30f, 0.35f, 0.40f, 0.50f }
        },

        // ------------------------------------------------------------------
        // [PAD]
        // 7. BERGECHO (mountain echo) - vast cinematic space. Big/cinematic.
        // ------------------------------------------------------------------
        {
            "Bergecho",
            "A vast mountain echo - huge, cinematic space.",
            { 0.349f, 0.317f, 0.476f, 0.285f },
            { 0.8216f, 0.8517f, 0.55f, 0.80f, 1.00f, 0.50f }
        },

        // ------------------------------------------------------------------
        // [IN-BETWEEN]
        // 8. KUPFERGLANZ (copper shine) - warm, mid-bright, present. Bright/warm.
        // 2026-10-07: converted from pad to IN-BETWEEN - same layer balance,
        // ATTACK/RELEASE moved into the fast zone (onset ~0.2-0.45 s,
        // release ~0.8-1.4 s with the room), so it bridges the leads and pads.
        // ------------------------------------------------------------------
        {
            "Kupferglanz",
            "Warm copper shine - a present poly synth that speaks quickly.",
            { 0.298f, 0.462f, 0.265f, 0.198f },
            { 0.5964f, 0.8350f, 0.55f, 0.30f, 0.50f, 0.50f }
        },

        // ------------------------------------------------------------------
        // [LEAD]
        // 9. SILBERPFAD (silver path) - soft melodic lead to play over the pads.
        // All four layers, Bloom adding gentle motion. Medium-fast: 72-164 ms
        // onset, release ~0.4 s.
        // ------------------------------------------------------------------
        {
            "Silberpfad",
            "Soft, singing lead to play over the pads.",
            { 0.377f, 0.330f, 0.235f, 0.235f },
            { 0.4175f, 0.5964f, 0.55f, 0.30f, 0.30f, 0.40f }
        },

        // ------------------------------------------------------------------
        // [PAD]
        // 10. MITTERNACHTSBLAU (midnight blue) - deep, near-black drone. Dark.
        // ------------------------------------------------------------------
        {
            "Mitternachtsblau",
            "A deep midnight drone, barely lit.",
            { 0.538f, 0.236f, 0.203f, 0.101f },
            { 0.8517f, 0.8668f, 0.12f, 0.40f, 0.50f, 0.50f }
        },

        // ------------------------------------------------------------------
        // [PAD]
        // 11. RUHEPULS (resting pulse) - slow, subtle motion underneath. Movement.
        // ------------------------------------------------------------------
        {
            "Ruhepuls",
            "A slow resting pulse, with subtle motion underneath.",
            { 0.387f, 0.231f, 0.194f, 0.426f },
            { 0.7915f, 0.8066f, 0.30f, 0.35f, 0.50f, 0.50f }
        },

        // ------------------------------------------------------------------
        // [IN-BETWEEN]
        // 12. STURMFRONT (storm front) - dramatic, wide, rolling. Big/cinematic.
        // 2026-10-07: converted from pad to IN-BETWEEN - same layer balance,
        // ATTACK/RELEASE moved into the fast zone (onset ~0.2-0.45 s,
        // release ~0.8-1.4 s with the room), so it bridges the leads and pads.
        // ------------------------------------------------------------------
        {
            "Sturmfront",
            "A dramatic storm front - big, wide chords that hit sooner.",
            { 0.318f, 0.318f, 0.291f, 0.344f },
            { 0.7157f, 0.8976f, 0.60f, 0.50f, 0.85f, 0.50f }
        },

        // ------------------------------------------------------------------
        // [LEAD]
        // 13. GLASPERLE (glass bead) - glassy bell lead. Expanse's octave-up
        // triangles lead and decay to their 40% sustain, so every note
        // plucks bright and then sings softer. Fast: 18 ms onset at C5;
        // release ~0.6 s with the shimmer's tail. Root and Clearing under
        // it are what lets it reach the bank's loudness (Expanse alone tops
        // out 2 dB short at full volume - Sternenzelt's EX-002 cause).
        // 2026-10-07: less piercing. Expanse's octave-up tone dominated held
        // high notes (-5 dB of the whole sound at G5); Expanse down, Bloom and
        // Clearing up around it, DETUNE 60% so its triangles beat instead of
        // fusing - strongest 1.5-6 kHz partial -12.4 -> -16.6 dB, FILTER 60%.
        // 2026-10-07 (second pass): owner - "no longer piercing, but not quite
        // glass anymore". Halfway back: Expanse 0.50 -> 0.72, FILTER 60 -> 72%;
        // strongest partial -15.3 dB (piercing was -12.4, first fix -16.6).
        // ------------------------------------------------------------------
        {
            "Glasperle",
            "Glassy bell lead - plucks bright, then sings softer.",
            { 0.309f, 0.265f, 0.718f, 0.420f },
            { 0.0000f, 0.5964f, 0.72f, 0.30f, 0.35f, 0.60f }
        },

        // ------------------------------------------------------------------
        // [PAD]
        // 14. STERNENZELT (starry sky) - vast, celestial, night. Expanse
        // dominates, full width, long airy reverb.
        // ------------------------------------------------------------------
        {
            "Sternenzelt",
            "Vast and celestial - Expanse fills the whole sky.",
            { 0.167f, 0.167f, 1.000f, 0.278f },
            { 0.8367f, 0.8367f, 0.80f, 0.75f, 1.00f, 0.50f }
        },

        // ------------------------------------------------------------------
        // [PAD]
        // 15. DAMMERLICHT (dusk light) - warm, settling. Lush/ambient.
        //
        // Non-ASCII name (a-umlaut) - same UTF-8 workaround as above.
        // ------------------------------------------------------------------
        {
            juce::String (juce::CharPointer_UTF8 ("D\xc3\xa4mmerlicht")),
            "Warm dusk light, gently settling.",
            { 0.394f, 0.361f, 0.229f, 0.197f },
            { 0.7765f, 0.7915f, 0.40f, 0.40f, 0.55f, 0.50f }
        },

        // ------------------------------------------------------------------
        // [PAD]
        // 16. GOLDSTAUB (gold dust) - bright, airy, shimmering. Bright.
        // ------------------------------------------------------------------
        {
            "Goldstaub",
            "Golden dust catching the light - bright and airy.",
            { 0.225f, 0.270f, 0.765f, 0.404f },
            { 0.7464f, 0.7614f, 0.80f, 0.65f, 0.95f, 0.50f }
        },

        // ------------------------------------------------------------------
        // [IN-BETWEEN]
        // 17. KLARHEIT (clarity) - clear, present, minimal reverb. Minimal/mix-friendly.
        // 2026-10-07: converted from pad to IN-BETWEEN - same layer balance,
        // ATTACK/RELEASE moved into the fast zone (onset ~0.2-0.45 s,
        // release ~0.8-1.4 s with the room), so it bridges the leads and pads.
        // ------------------------------------------------------------------
        {
            "Klarheit",
            "Clear, present and simple - quick enough for rhythm parts.",
            { 0.383f, 0.313f, 0.244f, 0.209f },
            { 0.4771f, 0.7754f, 0.55f, 0.25f, 0.45f, 0.50f }
        }
    };

    return presets;
}

const std::vector<Preset>& getFactoryPresets()
{
    return buildPresets();
}

int getNumFactoryPresets()
{
    return (int) getFactoryPresets().size();
}

} // namespace horizon
