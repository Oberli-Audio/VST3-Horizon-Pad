#pragma once

#include "HorizonLookAndFeel.h"

class HorizonPadAudioProcessor;

namespace horizon::ui
{

/** The bottom strip: a thin top divider above the product, its version and
    format (left - what support asks for first), the "Wäg zom Läbe - a life
    of joy" tagline (centred), and "4 LAYERS - 6 MACROS" with the SIZE menu
    (right): 75 / 100 / 125 / 150 % of the designed window, remembered per
    instance and for the next new one. (Until 2026-10-17 the right side read
    "BUFFER A/B - ...", repeating the A/B buttons' state at a contrast the
    review measured at 2.2:1.) The mountain-skyline silhouette lives in the
    shared panel background (HorizonScene), not here. */
class FooterBar final : public juce::Component
{
public:
    explicit FooterBar (HorizonPadAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Called with the scale picked from the SIZE menu. */
    std::function<void (float)> onScaleChosen;

    /** Re-reads what the strip shows; repaints only if it changed. */
    void refreshFromProcessor();

private:
    class SizeButton final : public juce::Button
    {
    public:
        SizeButton() : juce::Button ("Window size") {}
        void setScale (float s) { scale = s; repaint(); }
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
        int preferredWidth() const;

    private:
        float scale = 1.0f;
    };

    void showSizeMenu();

    HorizonPadAudioProcessor& processor;
    SizeButton sizeButton;
    float shownScale = -1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FooterBar)
};

} // namespace horizon::ui
