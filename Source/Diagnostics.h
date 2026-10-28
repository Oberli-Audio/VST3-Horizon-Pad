#pragma once

#include <JuceHeader.h>

class HorizonPadAudioProcessor;

namespace horizon
{

/** The About panel's diagnostics block (plugin-ui-bob audio-plugins.md 14,
    ship-bob feedback-loop.md 3): the fourteen fields every support document
    cites, one "Label: value" line each, nothing else - no user name, no paths.
    A friend pastes it into a report. Message thread. */
juce::StringArray diagnosticsLines (const HorizonPadAudioProcessor& processor);

inline juce::String diagnosticsText (const HorizonPadAudioProcessor& processor)
{
    return diagnosticsLines (processor).joinIntoString ("\n");
}

} // namespace horizon
