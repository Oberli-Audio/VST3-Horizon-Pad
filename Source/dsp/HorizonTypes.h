#pragma once

#include <JuceHeader.h>

namespace horizon
{

/** Number of sound layers. Fixed by design: four independently-blendable pads. */
static constexpr int kNumLayers = 4;

/** Simple polyphony cap. Ambient pads do not need a huge voice count. */
static constexpr int kMaxVoices = 8;

/**
    The register: the keys the instrument plays, C0 to C7 (C4 = MIDI 60, the
    convention everywhere in this code). Set by the owner's listening, not by
    the engine's limits (brief A-010, 2026-10-17): below C0 and above C7 the
    sound is no longer musical, so a note-on there starts no voice and makes no
    sound. Guarded by the `register_is_c0_to_c7` test.
*/
static constexpr int kRegisterLowestNote  = 12;   ///< C0
static constexpr int kRegisterHighestNote = 96;   ///< C7

static constexpr bool isInRegister (int midiNote) noexcept
{
    return midiNote >= kRegisterLowestNote && midiNote <= kRegisterHighestNote;
}

/** Number of host-automatable global macros: Attack, Release, Filter, Reverb, Width, Detune. */
static constexpr int kNumGlobalParams = 6;

/** Index of each macro in every macros array (presets, A/B buffers, parameter order). */
enum MacroIndex
{
    attackMacroIndex = 0,
    releaseMacroIndex,
    filterMacroIndex,
    reverbMacroIndex,
    widthMacroIndex,
    detuneMacroIndex
};

/**
    Index of each layer, used everywhere (DSP, GUI, presets). Order matches the
    GUI's knob row: ROOT, CLEARING, EXPANSE, BLOOM.
*/
enum LayerIndex
{
    warmFoundation = 0,   ///< ROOT - dull, slow attack, low-mid body
    analogEnsemble,       ///< CLEARING - bright, medium attack, chorused mid
    airyChoir,            ///< EXPANSE - very bright, very slow, high/air, long tail
    motionPad             ///< BLOOM - medium brightness, built-in movement
};

} // namespace horizon
