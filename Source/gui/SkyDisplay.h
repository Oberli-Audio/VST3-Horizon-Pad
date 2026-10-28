#pragma once

#include "HorizonScene.h"

namespace horizon::ui
{

/**
    The Scene's display (editor F, "sky over the strip"): HorizonScene in a
    1104 x 294 frame with `radius-lg` corners and a 1 px `line` border, and,
    after a preset recall, that preset's name (`title`, 22/28 bold) and its
    one-line description (13/19) at 24/20 px from the top-left - rising a
    few pixels as they fade in, held 4 s, then fading out. (They lived under
    the wordmark before; the wordmark went with editor F.)

    Text on the sky must pass 4.5:1 over the whole FILTER range (round 11).
    `ink` alone reaches 4.45:1 at the brightest corner of the range (FILTER,
    WIDTH and REVERB at 100 %, the bank's longest description), so the title
    sits on a soft `ground`-coloured shadow (65 %, 10 px blur) - darkening
    only the sky right behind the letters. Measured with snapshot_contrast.py
    on the renders at FILTER 0, 12, 50 and 100 % (docs/editor-f-review.md).
*/
class SkyDisplay final : public juce::Component
{
public:
    SkyDisplay();

    HorizonScene& getScene() noexcept { return scene; }

    /** Steps the scene's springs by `dt` (the editor steps it at about
        30 fps) and repaints what moved. */
    void advanceScene (float dt);

    /** One editor frame of the title's fade. */
    void advanceTitle (double now, float dt);

    /** Fades `name` and `description` in at the top-left. */
    void showPresetTitle (const juce::String& name, const juce::String& description);

    void paint (juce::Graphics&) override;

private:
    juce::Rectangle<int> titleArea() const;
    void renderTitle (float scale);

    HorizonScene scene;

    // The title and its shadow, rendered once per title at the screen's
    // pixel density: the sky repaints at ~30 fps while it glides, and the
    // shadow's blur is not redone each time.
    juce::Image titleImage;
    float titleImageScale = 0.0f;
    juce::String titleImageText;

    juce::String titleName, titleDescription, pendingName, pendingDescription;
    bool hasPending = false, startHold = false;
    double titleHoldUntil = 0.0;
    motion::Follower titleAlpha;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SkyDisplay)
};

} // namespace horizon::ui
