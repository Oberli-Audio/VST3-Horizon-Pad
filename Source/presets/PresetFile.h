#pragma once

#include "Presets.h"

#include <optional>

namespace horizon::PresetFile
{

/** Format of <HorizonPadState>, in the session state and inside every preset
    file (playbook D.5.3). A newer file is refused, an older one migrated:
      1  the ten parameters in 0..1, A/B buffers by index, the editor size in
         the state (until 2026-10-21; files of this version are sessions only)
      2  the parameters in 0..100 (parameters.csv), A/B buffers by parameter
         ID, non-parameter state in <Extra>, the editor size machine-local */
inline constexpr int kStateVersion = 2;

/** The sound's version: bumped together with a migration and a changelog
    line when a factory sound changes on purpose (Tier P keeps no old
    behaviour alive behind it). */
inline constexpr int kDspVersion = 1;

inline constexpr const char* kExtension = ".quellepreset";

/** The VST3 spec location, one folder for the product (D.6.4):
    ~/Library/Audio/Presets/Quelle Music/Horizon Pad, or Documents\VST3
    Presets\Quelle Music\Horizon Pad. HORIZON_PRESET_ROOT overrides it, so
    the harness and the tests never touch the real folders. */
juce::File presetRoot();
juce::File factoryFolder();
juce::File userFolder();

/** Machine-local settings (the editor size): ~/Library/Application Support/
    Quelle Music/Horizon Pad or %APPDATA%\Quelle Music\Horizon Pad (D.6.4);
    under HORIZON_PRESET_ROOT/Settings when that is set. */
juce::File settingsFolder();

/** Where versions before 0.1.0 kept UserPresets.xml and EditorSettings.xml. */
juce::File legacyFolder();

/** "Alpenglühen" -> "alpengluehen": ASCII, lowercase, '-' between words. */
juce::String slugFor (const juce::String& name);

/** The ten values of a <PARAMETERS> element written at `version`, migrated to
    today's 0..100; a missing, non-numeric or non-finite value takes the
    parameter's default, everything is clamped to the range. */
void readParameters (const juce::XmlElement* parameters, int version, Preset& into);

/** Reads a <QuellePreset> document. Refuses (nullopt, `error` set) anything
    that is not one, or one from a newer format - never half-loaded. */
std::optional<Preset> read (const juce::XmlElement& xml, juce::String& error);
std::optional<Preset> read (const juce::File& file, juce::String& error);

/** The document for a preset; `factory` marks the bank's own files. */
std::unique_ptr<juce::XmlElement> toXml (const Preset& preset, bool factory);

/** A value as the shortest text that reads back to the same float, in the
    C locale whatever the host set. */
juce::String formatValue (float value);

} // namespace horizon::PresetFile
