#include "UserPresetStore.h"
#include "PresetFile.h"

#include <algorithm>
#include <mutex>

namespace horizon
{

namespace
{
    juce::String thisUser()
    {
        const auto full = juce::SystemStats::getFullUserName().trim();
        return full.isNotEmpty() ? full : juce::SystemStats::getLogonName();
    }

    /** UserPresets.xml (before 0.1.0): <HorizonPadUserPresets [version="2"]>
        with <Preset name v0..v3 m0..m5/>, in 0..1 without a version. */
    void migrateLegacyLibrary()
    {
        const auto legacy = PresetFile::legacyFolder().getChildFile ("UserPresets.xml");

        if (! legacy.existsAsFile())
            return;

        const auto xml = juce::XmlDocument::parse (legacy);

        if (xml != nullptr && xml->hasTagName ("HorizonPadUserPresets"))
        {
            const auto scale = xml->getIntAttribute ("version", 1) < 2 ? kParamScale : 1.0f;

            for (auto* child : xml->getChildWithTagNameIterator ("Preset"))
            {
                Preset preset;
                preset.name = child->getStringAttribute ("name", "Untitled");
                preset.author = thisUser();

                for (int i = 0; i < kNumLayers; ++i)
                    preset.volumes[(size_t) i] = juce::jlimit (0.0f, kParamScale, (float) child->getDoubleAttribute ("v" + juce::String (i)) * scale);

                for (int i = 0; i < kNumGlobalParams; ++i)
                    preset.macros[(size_t) i] = juce::jlimit (0.0f, kParamScale, (float) child->getDoubleAttribute ("m" + juce::String (i)) * scale);

                juce::String error;
                UserPresetStore::saveNew (preset, error);
            }
        }

        legacy.moveFileTo (legacy.getSiblingFile ("UserPresets-before-0.1.0.xml"));
    }

    /** Earlier builds kept the User folder under the company's earlier name ("Quelle
        Music" for 0.1.0): the newest one found is copied once, into a User folder that
        does not exist yet, as .oberlipreset (the reader takes the old root element
        inside). The old folder stays as it was for an older build. */
    void copyPreviousUserFolder()
    {
        const auto to = PresetFile::userFolder();

        if (to.exists())
            return;

        for (const auto& root : PresetFile::previousPresetRoots())
        {
            const auto from = root.getChildFile ("User");

            if (! from.isDirectory())
                continue;

            if (! to.createDirectory())
                return;

            for (const auto& file : from.findChildFiles (juce::File::findFiles, false, "*"))
                if (PresetFile::hasPresetExtension (file.getFileName()))
                    file.copyFileTo (to.getChildFile (file.getFileNameWithoutExtension() + PresetFile::kExtension));

            return;
        }
    }
}

std::vector<Preset> UserPresetStore::scan()
{
    copyPreviousUserFolder();
    migrateLegacyLibrary();

    std::vector<Preset> presets;
    const auto folder = PresetFile::userFolder();

    for (const auto& file : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + PresetFile::kExtension))
    {
        juce::String error;

        if (auto preset = PresetFile::read (file, error))
            presets.push_back (std::move (*preset));
    }

    std::stable_sort (presets.begin(), presets.end(), [] (const Preset& a, const Preset& b)
    {
        const auto ta = a.file.getCreationTime(), tb = b.file.getCreationTime();
        return ta != tb ? ta < tb : a.slug.compareNatural (b.slug) < 0;
    });

    return presets;
}

std::optional<Preset> UserPresetStore::saveNew (Preset preset, juce::String& error)
{
    const auto folder = PresetFile::userFolder();

    if (! folder.createDirectory())
    {
        error = "Cannot create " + folder.getFullPathName();
        return std::nullopt;
    }

    if (preset.name.trim().isEmpty())
        preset.name = "Untitled";

    const auto base = PresetFile::slugFor (preset.name);
    auto slug = base;

    for (int n = 2; folder.getChildFile (slug + PresetFile::kExtension).exists(); ++n)
        slug = base + "-" + juce::String (n);

    const auto file = folder.getChildFile (slug + PresetFile::kExtension);

    if (! PresetFile::toXml (preset, false)->writeTo (file))
    {
        error = "Cannot write " + file.getFullPathName();
        return std::nullopt;
    }

    preset.slug = slug;
    preset.file = file;
    return preset;
}

bool UserPresetStore::remove (const Preset& preset)
{
    return preset.file.existsAsFile() && (preset.file.moveToTrash() || preset.file.deleteFile());
}

void UserPresetStore::syncFactoryFolder()
{
    static std::once_flag once;

    std::call_once (once, []
    {
        const auto folder = PresetFile::factoryFolder();
        const auto stamp = folder.getChildFile (".version");

        if (stamp.loadFileAsString().trim() == JucePlugin_VersionString)
            return;

        if (! folder.createDirectory())
            return;

        for (const auto& old : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + PresetFile::kExtension))
            old.deleteFile();

        for (const auto& [fileName, data] : getFactoryPresetFiles())
            folder.getChildFile (fileName).replaceWithData (data.getData(), data.getSize());

        stamp.replaceWithText (JucePlugin_VersionString);
    });
}

} // namespace horizon
