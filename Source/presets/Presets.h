#pragma once

#include "../dsp/HorizonTypes.h"
#include "../Params.h"

#include <vector>

namespace horizon
{

/** Canonical parameter IDs for the ten host-automatable parameters, from the
    generated Params.h (parameters.csv is the source; rule 36: never renamed). */
namespace ParamID
{
    // Pad volumes (ROOT/CLEARING/EXPANSE/BLOOM in the GUI), LayerIndex order.
    static constexpr const char* rootVolume     = Params::rootVolume;
    static constexpr const char* clearingVolume = Params::clearingVolume;
    static constexpr const char* expanseVolume  = Params::expanseVolume;
    static constexpr const char* bloomVolume    = Params::bloomVolume;

    // Global macros, MacroIndex order.
    static constexpr const char* attackMacro  = Params::attackMacro;
    static constexpr const char* releaseMacro = Params::releaseMacro;
    static constexpr const char* filterMacro  = Params::filterMacro;
    static constexpr const char* reverbMacro  = Params::reverbMacro;

    // WIDTH acts on every pad through that pad's own fixed width profile
    // (LayerBase::widthProfile()), DETUNE scales every pad's designed
    // unison detune (LayerBase::setDetune()).
    static constexpr const char* widthMacro   = Params::widthMacro;
    static constexpr const char* detuneMacro  = Params::detuneMacro;
}

/** The ten parameters run 0..100 in the host, the state and every preset
    (parameters.csv, since state version 2); the DSP and the editor's scene
    work in 0..1. The range is linear from 0, so a 0..1 value is also the
    parameter's normalised value. */
inline constexpr float kParamScale = 100.0f;
inline float toUnit (float hostValue) noexcept { return hostValue / kParamScale; }

/** The ten parameters in host order - the four volumes (LayerIndex), then the
    six macros (MacroIndex) - with parameters.csv's defaults, which are
    Lagerfeuer's values (the Init preset). One table for the layout, the state
    and the preset files; test parameter_table_matches_plugin diffs it against
    the CSV. */
struct ParamSpec
{
    const char* id;
    float defaultValue;
};

inline constexpr std::array<ParamSpec, (size_t) (kNumLayers + kNumGlobalParams)> kParamSpecs {{
    { ParamID::rootVolume,     47.7f },
    { ParamID::clearingVolume, 19.1f },
    { ParamID::expanseVolume,  9.6f },
    { ParamID::bloomVolume,    15.9f },
    { ParamID::attackMacro,    73.13f },
    { ParamID::releaseMacro,   73.13f },
    { ParamID::filterMacro,    30.0f },
    { ParamID::reverbMacro,    20.0f },
    { ParamID::widthMacro,     40.0f },
    { ParamID::detuneMacro,    0.0f },
}};

/**
    A preset: the four pad volumes and the six macros, in the host's 0..100.
    There is no per-layer tone block - each pad's character is fixed by its
    own validated Faust-derived DSP, and every macro acts on all four pads at
    once - so a preset is a point in this 10-dimensional blend/macro space.

    Factory presets are .oberlipreset files compiled into the plugin
    (presets/, in presets.csv's order); user presets are the same files in
    the User folder (PresetFile.h). Everything shown - name, category,
    author, description - is read from the file (D.6.5).
*/
struct Preset
{
    juce::String slug;          ///< the file name without extension, ASCII (rule 14)
    juce::String name;          ///< display name
    juce::String description;
    juce::String category;      ///< Pad, In-between or Lead for the factory bank
    juce::String author;
    juce::File file;            ///< user presets: where the file lives; empty for factory

    /** Root, Clearing, Expanse, Bloom. 0..100. */
    std::array<float, (size_t) kNumLayers> volumes {};

    /** Attack, Release, Filter, Reverb, Width, Detune (MacroIndex). 0..100. */
    std::array<float, (size_t) kNumGlobalParams> macros {};

    float valueAt (size_t specIndex) const noexcept
    {
        return specIndex < (size_t) kNumLayers ? volumes[specIndex] : macros[specIndex - (size_t) kNumLayers];
    }

    void setValueAt (size_t specIndex, float value) noexcept
    {
        (specIndex < (size_t) kNumLayers ? volumes[specIndex] : macros[specIndex - (size_t) kNumLayers]) = value;
    }
};

/** The factory bank, in presets.csv's order: Init (Lagerfeuer) first. */
const std::vector<Preset>& getFactoryPresets();

int getNumFactoryPresets();

/** Index of the factory preset with this slug, or -1. */
int findFactoryPreset (const juce::String& slug);

/** The raw bytes of every embedded factory file, by file name (for the
    Factory folder the plugin writes on the first run of a version). */
std::vector<std::pair<juce::String, juce::MemoryBlock>> getFactoryPresetFiles();

} // namespace horizon
