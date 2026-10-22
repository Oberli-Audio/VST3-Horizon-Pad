#include "Presets.h"
#include "PresetFile.h"

#include <PresetData.h>

namespace horizon
{

/*
    The factory bank lives in presets/ at the repository root: one .quellepreset
    per preset (D.6.5), compiled in as PresetData, and presets.csv - the bank's
    order, categories and notes, checked by presetlint.py. Until 2026-10-21 it
    was a table here in 0..1; scripts that cite "Presets.cpp" for the bank's
    history mean that table (git history, before Round 14).

    Three families since 2026-10-07, in repeating cycles: PADS, IN-BETWEEN
    (onset ~0.2-0.45 s) and LEADS (onset 15-165 ms), each cycle from the
    longest attack to the shortest, so browsing never jumps from a slow pad
    straight to a lead. Init is Lagerfeuer, first (owner, 2026-10-21): the
    parameter defaults are its values, and the plugin opens on it. Every preset
    is loudness-matched to the pad bank's median, -16.9 LUFS
    (tools/measure/descriptors.py --presets); docs/preset-curation-2026-09-23.md
    records the method.

    ATTACK/RELEASE since 2026-10-14 (brief A-009): one exponential curve from 1%
    of each layer's designed time at 0% to the designed time at 100%
    (dsp/EnvelopeMacro.h). PADS 73-87%, IN-BETWEEN 48-72% / 78-90%, LEADS
    0-42% / 30-60%. WIDTH is each preset's former Expanse width; DETUNE 50% is
    the designed detune.
*/

namespace
{
    const char* findResource (const juce::String& fileName, int& size)
    {
        for (int i = 0; i < PresetData::namedResourceListSize; ++i)
        {
            const auto* resource = PresetData::namedResourceList[i];

            if (fileName == PresetData::getNamedResourceOriginalFilename (resource))
                return PresetData::getNamedResource (resource, size);
        }

        size = 0;
        return nullptr;
    }

    /** presets.csv's slugs, in bank order (first column, header skipped). */
    juce::StringArray bankOrder()
    {
        int size = 0;
        const auto* csv = findResource ("presets.csv", size);
        juce::StringArray slugs;

        if (csv == nullptr)
            return slugs;

        auto lines = juce::StringArray::fromLines (juce::String::fromUTF8 (csv, size));
        lines.remove (0);

        for (const auto& line : lines)
            if (const auto slug = line.upToFirstOccurrenceOf (",", false, false).trim(); slug.isNotEmpty())
                slugs.add (slug);

        return slugs;
    }

    std::vector<Preset> loadBank()
    {
        std::vector<Preset> bank;

        for (const auto& slug : bankOrder())
        {
            int size = 0;
            const auto* data = findResource (slug + PresetFile::kExtension, size);
            jassert (data != nullptr);   // presets.csv names a file that is not compiled in

            if (data == nullptr)
                continue;

            const auto xml = juce::XmlDocument::parse (juce::String::fromUTF8 (data, size));
            juce::String error;

            if (xml != nullptr)
            {
                if (auto preset = PresetFile::read (*xml, error))
                {
                    preset->slug = slug;
                    bank.push_back (std::move (*preset));
                    continue;
                }
            }

            jassertfalse;   // a factory file that does not read is a build defect
        }

        return bank;
    }
}

const std::vector<Preset>& getFactoryPresets()
{
    static const std::vector<Preset> bank = loadBank();
    return bank;
}

int getNumFactoryPresets()
{
    return (int) getFactoryPresets().size();
}

int findFactoryPreset (const juce::String& slug)
{
    const auto& bank = getFactoryPresets();

    for (size_t i = 0; i < bank.size(); ++i)
        if (bank[i].slug == slug)
            return (int) i;

    return -1;
}

std::vector<std::pair<juce::String, juce::MemoryBlock>> getFactoryPresetFiles()
{
    std::vector<std::pair<juce::String, juce::MemoryBlock>> files;

    for (const auto& preset : getFactoryPresets())
    {
        int size = 0;
        const auto fileName = preset.slug + PresetFile::kExtension;

        if (const auto* data = findResource (fileName, size))
            files.emplace_back (fileName, juce::MemoryBlock (data, (size_t) size));
    }

    return files;
}

} // namespace horizon
