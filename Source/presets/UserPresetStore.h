#pragma once

#include "Presets.h"

#include <optional>
#include <vector>

namespace horizon
{

/**
    The player's own presets: one .oberlipreset per preset in the User folder
    (PresetFile::userFolder(), D.6.4/D.6.5), shared by every instance in every
    host and never touched by an update. Message thread only.

    Before 0.1.0 they lived in one UserPresets.xml; scan() moves those into
    files once and keeps the old file beside them as UserPresets-before-0.1.0.xml.
*/
class UserPresetStore
{
public:
    /** Every readable file in the User folder, oldest first (a new save
        appends), after migrating a legacy UserPresets.xml if there is one. */
    static std::vector<Preset> scan();

    /** Writes `preset` as a new file (a unique ASCII slug from its name) and
        returns it with `file` and `slug` set; nullopt and `error` on failure. */
    static std::optional<Preset> saveNew (Preset preset, juce::String& error);

    /** Moves the preset's file to the trash: a friend's preset is never
        deleted outright. */
    static bool remove (const Preset& preset);

    /** Writes the bank into the Factory folder on the first run of each
        plugin version (Factory/.version), so a changed factory preset reaches
        every friend; nothing else in the preset folder is touched. Once per
        process. */
    static void syncFactoryFolder();
};

} // namespace horizon
