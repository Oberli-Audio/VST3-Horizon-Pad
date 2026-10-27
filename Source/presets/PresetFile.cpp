#include "PresetFile.h"

#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace horizon::PresetFile
{

namespace
{
    juce::File overrideRoot()
    {
        const auto path = juce::SystemStats::getEnvironmentVariable ("HORIZON_PRESET_ROOT", {});
        return path.isNotEmpty() && juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
    }

    juce::File presetRootFor (const juce::String& company)
    {
       #if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
            .getChildFile ("Library/Audio/Presets").getChildFile (company).getChildFile ("Horizon Pad");
       #elif JUCE_WINDOWS
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("VST3 Presets").getChildFile (company).getChildFile ("Horizon Pad");
       #else
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
            .getChildFile (".vst3/presets").getChildFile (company).getChildFile ("Horizon Pad");
       #endif
    }

    juce::File settingsFolderFor (const juce::String& company)
    {
       #if JUCE_MAC
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("Application Support").getChildFile (company).getChildFile ("Horizon Pad");
       #else
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile (company).getChildFile ("Horizon Pad");
       #endif
    }

    constexpr const char* kCompany = "Oberli Audio";
    constexpr std::array<const char*, 2> kPreviousCompanies { "bob Audio", "Quelle Music" };   // newest first
}

bool hasPresetExtension (const juce::String& path)
{
    if (path.endsWithIgnoreCase (kExtension))
        return true;

    for (const auto* previous : kPreviousExtensions)
        if (path.endsWithIgnoreCase (previous))
            return true;

    return false;
}

juce::File presetRoot()
{
    if (const auto root = overrideRoot(); root != juce::File())
        return root;

    return presetRootFor (kCompany);
}

juce::File factoryFolder() { return presetRoot().getChildFile ("Factory"); }
juce::File userFolder()    { return presetRoot().getChildFile ("User"); }

juce::File settingsFolder()
{
    if (const auto root = overrideRoot(); root != juce::File())
        return root.getChildFile ("Settings");

    return settingsFolderFor (kCompany);
}

std::vector<juce::File> previousPresetRoots()
{
    if (const auto root = overrideRoot(); root != juce::File())
        return { root.getChildFile ("Previous") };

    std::vector<juce::File> roots;

    for (const auto* company : kPreviousCompanies)
        roots.push_back (presetRootFor (company));

    return roots;
}

std::vector<juce::File> previousSettingsFolders()
{
    if (const auto root = overrideRoot(); root != juce::File())
        return { root.getChildFile ("Previous").getChildFile ("Settings") };

    std::vector<juce::File> folders;

    for (const auto* company : kPreviousCompanies)
        folders.push_back (settingsFolderFor (company));

    return folders;
}

juce::File legacyFolder()
{
    if (const auto root = overrideRoot(); root != juce::File())
        return root.getChildFile ("Legacy");

    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Quelle Music").getChildFile ("Horizon Pad");
}

juce::String slugFor (const juce::String& name)
{
    // German letters spelled out, as a German reader would type them.
    const auto spelled = name.trim().toLowerCase()
                             .replace (juce::String::fromUTF8 ("\xc3\xa4"), "ae")
                             .replace (juce::String::fromUTF8 ("\xc3\xb6"), "oe")
                             .replace (juce::String::fromUTF8 ("\xc3\xbc"), "ue")
                             .replace (juce::String::fromUTF8 ("\xc3\x9f"), "ss");
    juce::String slug;
    bool gap = false;

    for (auto c : spelled)
    {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        {
            if (gap && slug.isNotEmpty())
                slug << '-';

            slug << juce::String::charToString (c);
            gap = false;
        }
        else
        {
            gap = true;
        }
    }

    return slug.isNotEmpty() ? slug.substring (0, 48) : juce::String ("preset");
}

void readParameters (const juce::XmlElement* parameters, int version, Preset& into)
{
    // Version 1 stored the parameters in 0..1.
    const auto scale = version < 2 ? kParamScale : 1.0f;

    for (size_t i = 0; i < kParamSpecs.size(); ++i)
    {
        auto value = kParamSpecs[i].defaultValue;

        if (parameters != nullptr)
        {
            for (auto* param : parameters->getChildWithTagNameIterator ("PARAM"))
            {
                if (param->getStringAttribute ("id") != kParamSpecs[i].id)
                    continue;

                const auto text = param->getStringAttribute ("value").trim();

                if (text.containsOnly ("0123456789.-+eE") && text.containsAnyOf ("0123456789"))
                {
                    const auto v = (float) text.getDoubleValue() * scale;

                    if (std::isfinite (v))
                        value = v;
                }

                break;
            }
        }

        into.setValueAt (i, juce::jlimit (0.0f, kParamScale, value));
    }
}

std::optional<Preset> read (const juce::XmlElement& xml, juce::String& error)
{
    if (! xml.hasTagName ("OberliPreset") && ! xml.hasTagName ("BobPreset") && ! xml.hasTagName ("QuellePreset"))
    {
        error = "Not a Horizon Pad preset.";
        return std::nullopt;
    }

    const auto* state = xml.getChildByName ("HorizonPadState");

    if (state == nullptr)
    {
        error = "This preset is for another Oberli Audio plugin.";
        return std::nullopt;
    }

    const auto version = state->getIntAttribute ("version", 1);

    if (version > kStateVersion)
    {
        error = "Made with a newer Horizon Pad (" + xml.getStringAttribute ("pluginVersion", "format " + juce::String (version))
              + "). Update the plugin to load it.";
        return std::nullopt;
    }

    Preset preset;
    preset.name = xml.getStringAttribute ("name").trim();
    preset.description = xml.getStringAttribute ("description");
    preset.category = xml.getStringAttribute ("category");
    preset.author = xml.getStringAttribute ("author");
    readParameters (state->getChildByName ("PARAMETERS"), version, preset);
    return preset;
}

std::optional<Preset> read (const juce::File& file, juce::String& error)
{
    const auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr)
    {
        error = "Not a Horizon Pad preset.";
        return std::nullopt;
    }

    auto preset = read (*xml, error);

    if (preset)
    {
        preset->slug = file.getFileNameWithoutExtension();
        preset->file = file;

        if (preset->name.isEmpty())
            preset->name = preset->slug;
    }

    return preset;
}

juce::String formatValue (float value)
{
    for (int digits = 6; digits <= 9; ++digits)
    {
        std::ostringstream o;
        o.imbue (std::locale::classic());
        o << std::setprecision (digits) << value;
        const juce::String text (o.str());

        if ((float) text.getDoubleValue() == value || digits == 9)
            return text;
    }

    return {};
}

std::unique_ptr<juce::XmlElement> toXml (const Preset& preset, bool factory)
{
    auto xml = std::make_unique<juce::XmlElement> ("OberliPreset");
    xml->setAttribute ("name", preset.name);
    xml->setAttribute ("author", preset.author);
    xml->setAttribute ("category", preset.category);

    if (preset.description.isNotEmpty())
        xml->setAttribute ("description", preset.description);

    xml->setAttribute ("pluginVersion", JucePlugin_VersionString);
    xml->setAttribute ("created", juce::Time::getCurrentTime().formatted ("%Y-%m-%d"));
    xml->setAttribute ("factory", factory ? "true" : "false");

    auto* state = xml->createNewChildElement ("HorizonPadState");
    state->setAttribute ("version", kStateVersion);
    state->setAttribute ("dspVersion", kDspVersion);

    auto* parameters = state->createNewChildElement ("PARAMETERS");

    for (size_t i = 0; i < kParamSpecs.size(); ++i)
    {
        auto* param = parameters->createNewChildElement ("PARAM");
        param->setAttribute ("id", kParamSpecs[i].id);
        param->setAttribute ("value", formatValue (preset.valueAt (i)));
    }

    return xml;
}

} // namespace horizon::PresetFile
