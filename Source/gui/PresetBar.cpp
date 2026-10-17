#include "PresetBar.h"
#include "../PluginProcessor.h"

namespace horizon::ui
{

namespace
{
    // Highlight fade (gold in, gold out), pill grow/shrink, scroll glide.
    constexpr float kHighlightIn = 0.07f;
    constexpr float kHighlightOut = 0.16f;
    constexpr float kPresenceOmega = 14.0f;   // ~0.35 s
    constexpr float kScrollOmega = 11.0f;     // ~0.45 s
    constexpr int kScrollEdgeRoom = 40;       // keep a scrolled-to pill clear of the edge fade
    constexpr int kPillGap = 10;

    // The ⇄ copy flash: full green for a moment, then fades.
    constexpr double kSwapFlashHold = 0.25;
    constexpr float kSwapFlashFade = 0.22f;

    // An active preset whose sound has been edited since: half the gold.
    constexpr float kEditedHighlight = 0.45f;

    float highlightTarget (bool active, bool edited)
    {
        return active ? (edited ? kEditedHighlight : 1.0f) : 0.0f;
    }

    // Buttons answer a click without taking keyboard focus, so the space bar
    // keeps reaching the host's transport; Tab still reaches them.
    void keepFocusWithHost (juce::Button& b)
    {
        b.setMouseClickGrabsKeyboardFocus (false);
    }

    int factoryPillWidth (const juce::TextButton& b)
    {
        return juce::jmax (70, b.getButtonText().length() * 9 + 28);
    }
}

//==============================================================================
PresetBar::UserPresetPill::UserPresetPill (juce::String presetName)
    : name (std::move (presetName))
{
    nameButton.setButtonText (name);
    nameButton.setClickingTogglesState (false);
    nameButton.getProperties().set ("noBorder", true);
    nameButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    nameButton.setColour (juce::TextButton::textColourOffId, Palette::text);
    nameButton.setTooltip ("Recall \"" + name + "\"");
    nameButton.onClick = [this] { if (onApply != nullptr) onApply(); };
    keepFocusWithHost (nameButton);
    addAndMakeVisible (nameButton);

    deleteButton.setButtonText (juce::String::fromUTF8 ("\xc3\x97")); // "x"
    deleteButton.setClickingTogglesState (false);
    deleteButton.getProperties().set ("noBorder", true);
    deleteButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    deleteButton.setColour (juce::TextButton::textColourOffId, Palette::cancelBorder); // was textFaint - unreadable against the pill's near-transparent background
    deleteButton.setTooltip ("Delete \"" + name + "\"");
    deleteButton.onClick = [this] { if (onDelete != nullptr) onDelete(); };
    deleteButton.setTitle ("Delete preset " + name);
    keepFocusWithHost (deleteButton);
    addAndMakeVisible (deleteButton);

    presence.snapTo (1.0f);
}

void PresetBar::UserPresetPill::snapHighlight()
{
    highlight.value = highlightTarget (active, modified);
    applyHighlight();
}

void PresetBar::UserPresetPill::startGrowing()
{
    presence.snapTo (0.0f);
    presence.target = 1.0f;
    setAlpha (0.0f);
}

void PresetBar::UserPresetPill::startShrinking()
{
    shrinking = true;
    presence.target = 0.0f;
    setInterceptsMouseClicks (false, false);
}

bool PresetBar::UserPresetPill::advance (float dt)
{
    if (highlight.advance (highlightTarget (active, modified), dt, kHighlightIn, kHighlightOut, 2.0e-3f))
        applyHighlight();

    if (! presence.advance (dt, kPresenceOmega, 2.0e-3f))
        return false;

    // Mostly transparent while narrow, so the name never shows squashed.
    setAlpha (juce::jlimit (0.0f, 1.0f, presence.value * presence.value));
    return true;
}

void PresetBar::UserPresetPill::applyHighlight()
{
    nameButton.setColour (juce::TextButton::textColourOffId, Palette::text.interpolatedWith (Palette::gold, highlight.value));
    repaint();
}

int PresetBar::UserPresetPill::preferredWidth() const
{
    return juce::jmax (70, name.length() * 9 + 28) + kDeleteButtonWidth;
}

int PresetBar::UserPresetPill::currentWidth() const
{
    return juce::roundToInt ((float) preferredWidth() * juce::jlimit (0.0f, 1.0f, presence.value));
}

void PresetBar::UserPresetPill::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced (0.5f);
    const auto corner = juce::jmin (bounds.getHeight(), bounds.getWidth()) * 0.5f;
    const auto amount = highlight.value;

    g.setColour (Palette::pillInactiveUserBg.interpolatedWith (Palette::pillActiveBg, amount));
    g.fillRoundedRectangle (bounds, corner);

    g.setColour (Palette::pillBorder.interpolatedWith (Palette::gold, amount));
    g.drawRoundedRectangle (bounds, corner, 1.0f);
}

void PresetBar::UserPresetPill::resized()
{
    // Laid out at full width and clipped while the pill grows or shrinks,
    // so the name and the "x" slide rather than squash. (Full width is the
    // preferred width less the 2 px layOutScrollContent() trims each side.)
    auto r = getLocalBounds().withWidth (juce::jmax (getWidth(), preferredWidth() - 4));
    deleteButton.setBounds (r.removeFromRight (kDeleteButtonWidth));
    nameButton.setBounds (r);
}

//==============================================================================
PresetBar::PresetBar (HorizonPadAudioProcessor& processorToUse)
    : processor (processorToUse)
{
    presetViewport.setViewedComponent (&presetScrollContent, false);
    // No visible scrollbar (it also made the presets row shorter than the
    // fixed Save/A-B row next to it, since Viewport reserves space for the
    // bar even at rest) - scrolling still works via wheel/trackpad and via
    // click-drag anywhere in the row, plus the edge fade in
    // paintOverChildren() cues that there's more to see.
    // Viewport::useMouseWheelMoveIfNeeded only scrolls an axis whose
    // scrollbar is actually visible, *unless* the "allow scrolling without
    // scrollbar" flags below are set - since the bars are hidden (see the
    // comment above), leaving those flags false silently swallowed every
    // trackpad/wheel scroll and left only click-drag working.
    presetViewport.setScrollBarsShown (false, false, false, true);
    presetViewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::all);
    addAndMakeVisible (presetViewport);

    const auto& presets = processor.getPresets();

    for (int i = 0; i < (int) presets.size(); ++i)
    {
        auto* b = presetButtons.add (new juce::TextButton (presets[(size_t) i].name));
        b->setClickingTogglesState (false);
        b->setTooltip (presets[(size_t) i].description);
        // The highlight is drawn from "activeAmount" (see applyFactoryHighlight()),
        // so the toggled-on colour must not add a fill of its own.
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colours::transparentBlack);
        b->onClick = [this, i] { processor.setCurrentProgram (i); refreshFromProcessor(); };
        keepFocusWithHost (*b);
        presetScrollContent.addAndMakeVisible (b);
    }

    factoryHighlights.resize ((size_t) presetButtons.size());

    saveButton.setClickingTogglesState (false);
    saveButton.getProperties().set ("dashedBorder", true);
    saveButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    saveButton.setColour (juce::TextButton::textColourOffId, Palette::textKnobLabel);
    saveButton.setTooltip ("Save the current sound as a new preset in your library.");
    saveButton.onClick = [this] { beginSavingNewPreset(); };
    keepFocusWithHost (saveButton);
    addAndMakeVisible (saveButton);

    saveNameEditor.setSelectAllWhenFocused (true);
    saveNameEditor.setTooltip ("Type a name, then press Enter or click Save.");
    // Background/outline drawn by PresetBar::paint() instead (a rounded
    // pill, matching every other geometry here) - JUCE's own TextEditor
    // outline is a plain square-cornered rectangle.
    saveNameEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    saveNameEditor.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    saveNameEditor.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    saveNameEditor.setColour (juce::TextEditor::textColourId, Palette::text);
    saveNameEditor.setFont (juce::Font (juce::FontOptions().withHeight (14.0f)));
    saveNameEditor.setJustification (juce::Justification::centredLeft);
    saveNameEditor.setTextToShowWhenEmpty ("Preset name", Palette::textFaint);
    saveNameEditor.onReturnKey = [this] { commitSavingNewPreset(); };
    saveNameEditor.onEscapeKey = [this] { cancelSavingNewPreset(); };
    saveNameEditor.setVisible (false);
    addChildComponent (saveNameEditor);

    saveConfirmButton.setClickingTogglesState (false);
    saveConfirmButton.getProperties().set ("borderColour", (int) Palette::saveConfirmBorder.getARGB());
    saveConfirmButton.setColour (juce::TextButton::buttonColourId, Palette::saveConfirmBg);
    saveConfirmButton.setColour (juce::TextButton::textColourOffId, Palette::text);
    saveConfirmButton.setTooltip ("Save this preset.");
    saveConfirmButton.onClick = [this] { commitSavingNewPreset(); };
    keepFocusWithHost (saveConfirmButton);
    saveConfirmButton.setVisible (false);
    addChildComponent (saveConfirmButton);

    saveCancelButton.setClickingTogglesState (false);
    saveCancelButton.getProperties().set ("borderColour", (int) Palette::cancelBorder.getARGB());
    saveCancelButton.setColour (juce::TextButton::buttonColourId, Palette::cancelBg);
    saveCancelButton.setColour (juce::TextButton::textColourOffId, Palette::cancelBorder);
    saveCancelButton.setTooltip ("Cancel without saving.");
    saveCancelButton.onClick = [this] { cancelSavingNewPreset(); };
    keepFocusWithHost (saveCancelButton);
    saveCancelButton.setVisible (false);
    addChildComponent (saveCancelButton);

    slotAButton.setTooltip ("Switch to buffer A - its own independent snapshot of every parameter, for A/B comparison.");
    slotBButton.setTooltip ("Switch to buffer B - its own independent snapshot of every parameter, for A/B comparison.");

    for (auto* b : { &slotAButton, &slotBButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (0xa5);
        b->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        b->setColour (juce::TextButton::textColourOffId, Palette::textFaint);
        keepFocusWithHost (*b);
        addAndMakeVisible (b);
    }

    slotAButton.setTitle ("Buffer A");
    slotBButton.setTitle ("Buffer B");

    slotAButton.setToggleState (true, juce::dontSendNotification);
    slotAButton.onClick = [this] { processor.switchBuffer (0); refreshFromProcessor(); };
    slotBButton.onClick = [this] { processor.switchBuffer (1); refreshFromProcessor(); };

    swapButton.setClickingTogglesState (false);
    swapButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    swapButton.setColour (juce::TextButton::textColourOffId, Palette::textFaint);
    swapButton.setTooltip ("Copy the active slot onto the other one");
    swapButton.onClick = [this]
    {
        processor.copyActiveBufferToOtherBuffer();

        // A "copied" flash - the same green used for the save-confirm
        // button - since this action has no other visible effect to look at
        // (it silently overwrites the other, currently-hidden A/B slot).
        // Shown at once; the next frame times its hold and fade.
        swapFlash.value = 1.0f;
        swapFlashTimed = false;
        applySwapFlash();
    };
    swapButton.setTitle ("Copy the active buffer onto the other one");
    keepFocusWithHost (swapButton);
    addAndMakeVisible (swapButton);

    // --- Previous / next preset: the row shows six of seventeen at once.
    for (auto* b : { &previousButton, &nextButton })
    {
        b->setClickingTogglesState (false);
        b->getProperties().set ("noBorder", true);
        b->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        b->setColour (juce::TextButton::textColourOffId, Palette::textKnobLabel);
        keepFocusWithHost (*b);
        addAndMakeVisible (b);
    }

    previousButton.setTitle ("Previous preset");
    nextButton.setTitle ("Next preset");
    previousButton.setTooltip ("Previous preset");
    nextButton.setTooltip ("Next preset - the factory presets, then your own");
    previousButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };

    syncUserPresetPills();
    refreshFromProcessor();

    // Open on the current state as it is: no fades, no scroll glide.
    for (int i = 0; i < presetButtons.size(); ++i)
    {
        factoryHighlights[(size_t) i].value = highlightTarget (presetButtons[i]->getToggleState(), activeModified);
        applyFactoryHighlight (i);
    }

    for (auto* pill : userPresetPills)
        pill->snapHighlight();
}

PresetBar::~PresetBar() = default;

PresetBar::UserPresetPill* PresetBar::addUserPresetPill (const juce::String& name)
{
    auto* pill = userPresetPills.add (new UserPresetPill (name));

    // Looked up by pill, not by a captured index: a deleted pill leaves the
    // row without a rebuild, which shifts every later index.
    pill->onApply = [this, pill]
    {
        const auto index = userPresetPills.indexOf (pill);

        if (index >= 0)
        {
            processor.applyUserPreset (index);
            refreshFromProcessor();
        }
    };

    pill->onDelete = [pill] { pill->startShrinking(); };

    presetScrollContent.addAndMakeVisible (pill);
    return pill;
}

void PresetBar::syncUserPresetPills()
{
    const auto& userPresets = processor.getUserPresets();
    const auto count = (int) userPresets.size();

    if (count == userPresetPills.size())
        return;

    if (count == userPresetPills.size() + 1 && isShowing())
    {
        // Just saved (saveCurrentAsUserPreset() appends): grow its pill in.
        addUserPresetPill (userPresets.back().name)->startGrowing();
    }
    else
    {
        userPresetPills.clear();

        for (const auto& preset : userPresets)
            addUserPresetPill (preset.name);
    }

    layOutScrollContent();
}

void PresetBar::beginSavingNewPreset()
{
    saveNameEditor.setText ({}, juce::dontSendNotification);
    saveButton.setVisible (false);
    saveNameEditor.setVisible (true);
    saveConfirmButton.setVisible (true);
    saveCancelButton.setVisible (true);
    resized();
    saveNameEditor.grabKeyboardFocus();
}

void PresetBar::commitSavingNewPreset()
{
    const auto name = saveNameEditor.getText().trim();

    saveNameEditor.setVisible (false);
    saveConfirmButton.setVisible (false);
    saveCancelButton.setVisible (false);
    saveButton.setVisible (true);

    if (name.isNotEmpty())
    {
        processor.saveCurrentAsUserPreset (name);
        refreshFromProcessor();
    }

    resized();
}

void PresetBar::cancelSavingNewPreset()
{
    saveNameEditor.setVisible (false);
    saveConfirmButton.setVisible (false);
    saveCancelButton.setVisible (false);
    saveButton.setVisible (true);
    resized();
}

void PresetBar::refreshFromProcessor()
{
    syncUserPresetPills();

    const auto activeKind = processor.getActivePresetKind();
    const auto activeIndex = processor.getActivePresetIndex();

    for (int i = 0; i < presetButtons.size(); ++i)
        presetButtons[i]->setToggleState (activeKind == HorizonPadAudioProcessor::PresetKind::factory && i == activeIndex,
                                          juce::dontSendNotification);

    activeModified = processor.isActivePresetModified();

    for (int i = 0; i < userPresetPills.size(); ++i)
        userPresetPills[i]->setActive (activeKind == HorizonPadAudioProcessor::PresetKind::user && i == activeIndex,
                                       activeModified);

    const auto activeBuffer = processor.getActiveBufferIndex();
    slotAButton.setToggleState (activeBuffer == 0, juce::dontSendNotification);
    slotBButton.setToggleState (activeBuffer == 1, juce::dontSendNotification);

    // A newly active preset - recalled here, by the host, or just saved -
    // scrolls into view if it is not already. Before the row has a size
    // (the editor is still being built) that waits for resized().
    if ((int) activeKind != shownPresetKind || activeIndex != shownPresetIndex)
    {
        const auto firstLook = shownPresetKind < 0;
        shownPresetKind = (int) activeKind;
        shownPresetIndex = activeIndex;

        if (presetViewport.getWidth() <= 0)
            needsInitialScroll = true;
        else if (auto* target = activePresetComponent())
            scrollToShow (target, ! firstLook);
    }
}

juce::Component* PresetBar::activePresetComponent() const
{
    const auto index = processor.getActivePresetIndex();

    switch (processor.getActivePresetKind())
    {
        case HorizonPadAudioProcessor::PresetKind::factory: return presetButtons[index];
        case HorizonPadAudioProcessor::PresetKind::user:    return userPresetPills[index];
        case HorizonPadAudioProcessor::PresetKind::none:    break;
    }

    return nullptr;
}

void PresetBar::scrollToShow (juce::Component* target, bool animate)
{
    const auto viewX = presetViewport.getViewPositionX();
    const auto viewWidth = presetViewport.getViewWidth();

    // A pill that is still growing in has not reached its width yet.
    auto bounds = target->getBounds();

    if (auto* pill = dynamic_cast<UserPresetPill*> (target))
        bounds.setWidth (pill->preferredWidth());

    auto targetX = viewX;

    if (bounds.getX() - kScrollEdgeRoom < viewX)
        targetX = bounds.getX() - kScrollEdgeRoom;
    else if (bounds.getRight() + kScrollEdgeRoom > viewX + viewWidth)
        targetX = bounds.getRight() + kScrollEdgeRoom - viewWidth;

    // No upper clamp here: a pill still growing in has not widened the row
    // yet, and the viewport clamps every position it is given anyway.
    targetX = juce::jmax (0, targetX);

    if (targetX == viewX)
        return;

    if (! animate)
    {
        presetViewport.setViewPosition (targetX, 0);
        return;
    }

    scroll.snapTo ((float) viewX);
    scroll.target = (float) targetX;
    scrolling = true;
    lastScrollX = viewX;
}

void PresetBar::advance (double now, float dt)
{
    // --- Gold highlight: fades from the old preset to the new one.
    for (int i = 0; i < presetButtons.size(); ++i)
        if (factoryHighlights[(size_t) i].advance (highlightTarget (presetButtons[i]->getToggleState(), activeModified),
                                                   dt, kHighlightIn, kHighlightOut, 2.0e-3f))
            applyFactoryHighlight (i);

    // --- Saved pills: highlight, growing in, shrinking out (then deleted).
    bool layoutNeeded = false;

    for (auto* pill : userPresetPills)
        layoutNeeded = pill->advance (dt) || layoutNeeded;

    for (int i = userPresetPills.size(); --i >= 0;)
    {
        if (userPresetPills[i]->hasShrunk())
        {
            processor.deleteUserPreset (i);
            userPresetPills.remove (i);   // the pills mirror the list, so no rebuild
            layoutNeeded = true;
        }
    }

    if (layoutNeeded)
    {
        layOutScrollContent();
        repaint (presetViewport.getBounds());
        refreshFromProcessor();
    }

    // --- Scroll glide toward the active preset; a hand on the row wins.
    if (scrolling)
    {
        if (presetViewport.getViewPositionX() != lastScrollX)
        {
            scrolling = false;
        }
        else
        {
            scroll.advance (dt, kScrollOmega, 0.25f);
            presetViewport.setViewPosition (juce::roundToInt (scroll.value), 0);
            lastScrollX = presetViewport.getViewPositionX();   // after the viewport's own clamp
            scrolling = ! scroll.isSettled();
        }
    }

    // --- The ⇄ copy flash: hold, then fade.
    if (swapFlash.value > 0.0f)
    {
        if (! swapFlashTimed)
        {
            swapFlashHoldUntil = now + kSwapFlashHold;
            swapFlashTimed = true;
        }

        if (now >= swapFlashHoldUntil && swapFlash.advance (0.0f, dt, kSwapFlashFade, kSwapFlashFade, 2.0e-3f))
            applySwapFlash();
    }
}

void PresetBar::stepPreset (int delta)
{
    // One list: the factory presets, then the player's own, wrapping round.
    const auto numFactory = presetButtons.size();
    const auto total = numFactory + userPresetPills.size();

    if (total <= 0)
        return;

    auto position = -1;

    switch (processor.getActivePresetKind())
    {
        case HorizonPadAudioProcessor::PresetKind::factory: position = processor.getActivePresetIndex(); break;
        case HorizonPadAudioProcessor::PresetKind::user:    position = numFactory + processor.getActivePresetIndex(); break;
        case HorizonPadAudioProcessor::PresetKind::none:    break;
    }

    const auto target = position < 0 ? (delta > 0 ? 0 : total - 1)
                                     : ((position + delta) % total + total) % total;

    if (target < numFactory)
        processor.setCurrentProgram (target);
    else
        processor.applyUserPreset (target - numFactory);

    refreshFromProcessor();
}

void PresetBar::applyFactoryHighlight (int index)
{
    auto& b = *presetButtons[index];
    const auto amount = factoryHighlights[(size_t) index].value;
    const auto textColour = Palette::text.interpolatedWith (Palette::gold, amount);

    b.getProperties().set ("activeAmount", amount);
    b.setColour (juce::TextButton::textColourOffId, textColour);
    b.setColour (juce::TextButton::textColourOnId, textColour);
    b.repaint();
}

void PresetBar::applySwapFlash()
{
    const auto f = swapFlash.value;

    if (f <= 0.0f)
        swapButton.getProperties().remove ("borderColour");
    else
        swapButton.getProperties().set ("borderColour",
                                        (int) Palette::pillBorder.interpolatedWith (Palette::saveConfirmBorder, f).getARGB());

    swapButton.setColour (juce::TextButton::buttonColourId, Palette::saveConfirmBg.withMultipliedAlpha (f));
    swapButton.setColour (juce::TextButton::textColourOffId, Palette::textFaint.interpolatedWith (Palette::text, f));
    swapButton.repaint();
}

void PresetBar::setBackdropColour (juce::Colour colour)
{
    if (colour == backdrop)
        return;

    backdrop = colour;

    if (presetScrollContent.getWidth() > presetViewport.getWidth())
        repaint (presetViewport.getBounds());
}

void PresetBar::resized()
{
    auto r = getLocalBounds();

    // --- A/B group, pinned to the right end of the row (margin-left:auto).
    auto abArea = r.removeFromRight (118).reduced (2);
    swapButton.setBounds (abArea.removeFromRight (32).reduced (1));
    abArea.removeFromRight (6);
    slotBButton.setBounds (abArea.removeFromRight (32).reduced (1));
    abArea.removeFromRight (6);
    slotAButton.setBounds (abArea.removeFromRight (32).reduced (1));

    r.removeFromRight (14);

    // --- "+ Save preset" (or its inline name-entry form): a fixed slot
    // right before the A/B group, outside the scrollable presets area below
    // - so it's always reachable no matter how many presets there are,
    // instead of potentially being scrolled out of the visible row.
    if (saveNameEditor.isVisible())
    {
        saveNameEditor.setBounds (r.removeFromRight (150).reduced (2));
        r.removeFromRight (8);
        saveConfirmButton.setBounds (r.removeFromRight (70).reduced (2));
        r.removeFromRight (8);
        saveCancelButton.setBounds (r.removeFromRight (80).reduced (2));
    }
    else
    {
        saveButton.setBounds (r.removeFromRight (130).reduced (2));
    }

    r.removeFromRight (14); // gap before the fade zone (see paint())

    // --- Previous / next either side of the scrolling row.
    constexpr int kArrowWidth = 26;
    nextButton.setBounds (r.removeFromRight (kArrowWidth));
    r.removeFromRight (4);
    previousButton.setBounds (r.removeFromLeft (kArrowWidth));
    r.removeFromLeft (4);

    // --- The scrollable presets row fills whatever's left: factory presets
    // then saved-preset pills, laid out left to right inside
    // presetScrollContent at whatever total width they need - that width
    // can exceed the viewport's visible width, which is the point.
    presetViewport.setBounds (r);
    layOutScrollContent();

    if (needsInitialScroll && presetViewport.getWidth() > 0)
    {
        needsInitialScroll = false;

        if (auto* target = activePresetComponent())
            scrollToShow (target, false);
    }
}

void PresetBar::layOutScrollContent()
{
    const auto rowHeight = presetViewport.getHeight();
    int x = 0;

    for (auto* b : presetButtons)
    {
        const auto w = factoryPillWidth (*b);
        b->setBounds (juce::Rectangle<int> (x, 0, w, rowHeight).reduced (2));
        x += w + kPillGap;
    }

    // Growing and shrinking pills take their share of the width (and of the
    // gap after them) as they go, so the pills beside them slide.
    for (auto* pill : userPresetPills)
    {
        const auto w = pill->currentWidth();
        pill->setBounds (juce::Rectangle<int> (x, 0, w, rowHeight).reduced (w > 4 ? 2 : 0, 2));
        x += w + juce::roundToInt ((float) kPillGap * juce::jlimit (0.0f, 1.0f, pill->getPresence()));
    }

    presetScrollContent.setSize (juce::jmax (presetViewport.getWidth(), x), rowHeight);
}

void PresetBar::paint (juce::Graphics& g)
{
    // A/B group background: one rounded card behind the A/B/swap buttons,
    // matching the design's abGroupStyle exactly.
    auto abArea = getLocalBounds().removeFromRight (118).toFloat();
    g.setColour (Palette::cardBg);
    g.fillRoundedRectangle (abArea.reduced (2.0f), 10.0f);

    // The name-entry field's own background/border - a full pill/capsule,
    // the same shape (corner = height/2) every button on this row uses via
    // drawButtonBackground(), not the tighter fixed-radius rounding an
    // ordinary rounded-rectangle panel gets. saveNameEditor's own colours
    // are transparent (see its setup in the constructor) so this is the
    // only thing drawing it.
    if (saveNameEditor.isVisible())
    {
        auto bounds = saveNameEditor.getBounds().toFloat();
        const auto corner = bounds.getHeight() * 0.5f;
        g.setColour (Palette::saveInputBg);
        g.fillRoundedRectangle (bounds, corner);
        g.setColour (Palette::gold);
        g.drawRoundedRectangle (bounds.reduced (0.5f), corner, 1.0f);
    }
}

void PresetBar::paintOverChildren (juce::Graphics& g)
{
    // --- A soft fade over whichever edge of the scrollable presets has more
    // content just out of view - a visual cue that it scrolls, not a
    // functional mask (the viewport itself already clips anything scrolled
    // out of sight). Only shown once there's actually something to scroll
    // to, and only on the edge that currently has it. Drawn over the
    // children (paint() runs *before* them) so it actually shows on top of
    // whatever preset pill happens to sit at that edge, not underneath it.
    // It fades into `backdrop`, the sky behind this row, which changes with
    // FILTER (setBackdropColour()).
    if (presetScrollContent.getWidth() <= presetViewport.getWidth())
        return;

    constexpr int fadeWidth = 36;
    const auto viewportBounds = presetViewport.getBounds();
    const auto scrollX = presetViewport.getViewPositionX();

    if (scrollX + presetViewport.getWidth() < presetScrollContent.getWidth())
    {
        auto fadeArea = viewportBounds.withTrimmedLeft (viewportBounds.getWidth() - fadeWidth).toFloat();
        juce::ColourGradient fade (backdrop.withAlpha (0.0f), fadeArea.getX(), 0.0f,
                                   backdrop, fadeArea.getRight(), 0.0f, false);
        g.setGradientFill (fade);
        g.fillRect (fadeArea);
    }

    if (scrollX > 0)
    {
        auto fadeArea = viewportBounds.withTrimmedRight (viewportBounds.getWidth() - fadeWidth).toFloat();
        juce::ColourGradient fade (backdrop, fadeArea.getX(), 0.0f,
                                   backdrop.withAlpha (0.0f), fadeArea.getRight(), 0.0f, false);
        g.setGradientFill (fade);
        g.fillRect (fadeArea);
    }
}

} // namespace horizon::ui
