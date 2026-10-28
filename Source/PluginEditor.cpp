#include "PluginEditor.h"
#include "Diagnostics.h"

#include <limits>

using namespace horizon::ui;

namespace
{
    // Frames: about 60 per second is plenty for this much motion, and the
    // scene (a full repaint of the sky while it moves) is stepped at about
    // 30 - it only ever moves slowly.
    constexpr double kMinFrameInterval = 1.0 / 75.0;
    constexpr float kSceneFrameInterval = 1.0f / 32.0f;

    // Without a vertical blank for this long (no display, a host that hides
    // the window oddly) the fallback timer drives the frames instead.
    constexpr double kVBlankTimeout = 0.25;

    // The recall wave: one knob after another, left to right.
    constexpr double kWaveStep = 0.04;

    // Sunrise on open: the knobs start sweeping once the sky has begun to lighten.
    constexpr double kSunriseKnobDelay = 0.15;

    // How long a confirmation readout stays before it fades.
    constexpr double kToastSeconds = 2.2;

    // The menu and the save card hang from the header's right end, 4 px up
    // into it (the Menu card: top 44, right 16).
    constexpr int kOverlayTop = 44;
    constexpr int kOverlayRight = 16;

    bool isWithin (juce::Component& overlay, juce::Component* c)
    {
        return c != nullptr && (&overlay == c || overlay.isParentOf (c));
    }
}

HorizonPadAudioProcessorEditor::HorizonPadAudioProcessorEditor (HorizonPadAudioProcessor& processorToUse)
    : AudioProcessorEditor (processorToUse),
      horizonProcessor (processorToUse),
      header (processorToUse),
      strip (processorToUse),
      drawer (processorToUse),
      tooltipWindow (this, 600),
      vblank (this, [this]
      {
          lastVBlankTime = clockNow();
          advanceFrame (lastVBlankTime);
      })
{
    setLookAndFeel (&lookAndFeel);

    auto& apvts = horizonProcessor.getAPVTS();
    filterParam = apvts.getRawParameterValue (horizon::ParamID::filterMacro);
    reverbParam = apvts.getRawParameterValue (horizon::ParamID::reverbMacro);
    widthParam = apvts.getRawParameterValue (horizon::ParamID::widthMacro);

    addAndMakeVisible (header);
    addAndMakeVisible (sky);
    addAndMakeVisible (strip);
    addChildComponent (drawer);
    addChildComponent (menu);
    addChildComponent (saveCard);
    addChildComponent (about);
    addChildComponent (dragReadout);
    addChildComponent (toast);

    // Overlays close on a click anywhere else.
    addMouseListener (this, true);

    // The ground fills the window edge to edge, so nothing behind the editor
    // ever shows: the host need not paint or blend it.
    setOpaque (true);

    setResizable (false, false);
    setSize (kWindowWidth, kWindowHeight);

    // Recalls that happened before the editor existed are not news.
    seenRecallCount = horizonProcessor.getPresetRecallCount();
    seenSwitchCount = horizonProcessor.getBufferSwitchCount();

    // Sunrise on the first open of this instance: everything starts from
    // night and zero here, before the first paint, so the first frame on
    // screen is already the dark one. The first advanceFrame() starts the
    // clock. Reopening the window later shows the editor as it is.
    pullSceneTargets();
    sky.getScene().snapToTargets();

    if (horizonProcessor.takeFirstEditorOpen())
    {
        sky.getScene().prepareSunrise();
        strip.beginWave (std::numeric_limits<double>::max(), kWaveStep, true);
    }
    else
    {
        sunrisePending = false;
    }

    // Tab walks the controls in reading order: the header, then the row.
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);

    header.onMenuClicked = [this] { toggleMenu(); };
    header.onPresetNameClicked = [this] { togglePresetDrawer(); };
    header.onCopied = [this] (const juce::String& message, juce::Rectangle<int> anchor)
    {
        showToast (message, anchor + header.getPosition());
    };

    menu.onDismiss = [this] { header.setMenuOpen (false); };
    menu.onLoadPreset = [this] { importPresetFile(); };
    menu.onSavePreset = [this] { showSavePresetCard(); };
    menu.onReportProblem = [this] { reportProblem(); };
    menu.onAbout = [this] { showAbout(); };
    menu.onScaleChosen = [this] (float scale)
    {
        horizonProcessor.setEditorScale (scale);
        applyScale();
    };

    drawer.onDismiss = [this] { header.setDrawerOpen (false); };
    drawer.onExport = [this] { exportPresetFile(); };

    saveCard.onSave = [this] (const juce::String& name)
    {
        horizonProcessor.saveCurrentAsUserPreset (name);
        header.refresh();
    };

    applyScale();

    startTimerHz (30);
}

HorizonPadAudioProcessorEditor::~HorizonPadAudioProcessorEditor()
{
    stopTimer();
    removeMouseListener (this);
    setLookAndFeel (nullptr);
}

//==============================================================================
void HorizonPadAudioProcessorEditor::closeOverlays()
{
    if (menu.isVisible())
    {
        menu.close();
        header.setMenuOpen (false);
    }

    if (drawer.isVisible())
    {
        drawer.close();
        header.setDrawerOpen (false);
    }

    if (saveCard.isVisible())
        saveCard.close();
}

void HorizonPadAudioProcessorEditor::toggleMenu()
{
    if (menu.isVisible())
    {
        closeOverlays();
        return;
    }

    showMenu();
}

void HorizonPadAudioProcessorEditor::showMenu (bool scaleSubmenu)
{
    closeOverlays();
    menu.open ({ getWidth() - kOverlayRight, kOverlayTop }, horizonProcessor.getEditorScale());
    header.setMenuOpen (true);

    if (scaleSubmenu)
        menu.openScaleSubmenu();
}

void HorizonPadAudioProcessorEditor::togglePresetDrawer()
{
    if (drawer.isVisible())
    {
        closeOverlays();
        return;
    }

    showPresetDrawer();
}

void HorizonPadAudioProcessorEditor::showPresetDrawer()
{
    closeOverlays();
    drawer.open ({ 0, PluginHeader::kHeight, PresetDrawer::kWidth, getHeight() - PluginHeader::kHeight });
    header.setDrawerOpen (true);
}

void HorizonPadAudioProcessorEditor::showSavePresetCard()
{
    closeOverlays();
    saveCard.open ({ getWidth() - kOverlayRight, kOverlayTop });
}

void HorizonPadAudioProcessorEditor::showAbout (bool openLicences)
{
    closeOverlays();
    about.setBounds (getLocalBounds());
    about.show (horizon::diagnosticsLines (horizonProcessor), openLicences);
}

bool HorizonPadAudioProcessorEditor::previewDrag (const juce::String& parameterId)
{
    if (auto* knob = strip.findKnob (parameterId))
    {
        knob->setDragPreview (true);
        return true;
    }

    return false;
}

void HorizonPadAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    // Every click in the editor passes here first (addMouseListener): one
    // outside the open overlay - and outside the control that toggles it -
    // closes it.
    auto* c = e.eventComponent;

    if (menu.isVisible() && ! isWithin (menu, c) && ! isWithin (header.getMenuButton(), c))
    {
        menu.close();
        header.setMenuOpen (false);
    }

    if (drawer.isVisible() && ! isWithin (drawer, c) && ! isWithin (header.getPresetNameButton(), c))
    {
        drawer.close();
        header.setDrawerOpen (false);
    }

    if (saveCard.isVisible() && ! isWithin (saveCard, c))
        saveCard.close();
}

//==============================================================================
void HorizonPadAudioProcessorEditor::showToast (const juce::String& text, juce::Rectangle<int> anchor)
{
    toast.flash (text, anchor.toFloat(), lastFrameTime, kToastSeconds);
}

void HorizonPadAudioProcessorEditor::reportProblem()
{
    // ship-bob feedback-loop.md: friends report by message, with what About
    // copies. Nothing opens, nothing is sent.
    juce::SystemClipboard::copyTextToClipboard (horizon::diagnosticsText (horizonProcessor));
    showToast (juce::String::fromUTF8 ("Copied. Paste it into your message to Bj\xc3\xb6" "rn."),
               header.getMenuButton().getBounds() + header.getPosition());
}

void HorizonPadAudioProcessorEditor::showDragReadout (juce::Component& source, juce::Rectangle<float> capInSource,
                                                      const juce::String& text)
{
    dragReadout.showAt (text, getLocalArea (&source, capInSource), 6.0f);
}

void HorizonPadAudioProcessorEditor::hideDragReadout()
{
    dragReadout.hide();
}

//==============================================================================
bool HorizonPadAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (horizon::PresetFile::hasPresetExtension (f))
            return true;

    return false;
}

void HorizonPadAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    for (const auto& f : files)
        if (horizon::PresetFile::hasPresetExtension (f))
            importPreset (juce::File (f));
}

void HorizonPadAudioProcessorEditor::importPreset (const juce::File& file)
{
    const auto error = horizonProcessor.importPreset (file);
    header.refresh();

    if (error.isNotEmpty())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                "Preset not imported", file.getFileName() + ": " + error);
}

void HorizonPadAudioProcessorEditor::importPresetFile()
{
    fileChooser = std::make_unique<juce::FileChooser> ("Load a Horizon Pad preset",
                                                       juce::File::getSpecialLocation (juce::File::userDesktopDirectory),
                                                       juce::String ("*") + horizon::PresetFile::kExtension
                                                           + ";*" + horizon::PresetFile::kPreviousExtensions[0]
                                                           + ";*" + horizon::PresetFile::kPreviousExtensions[1]);
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<HorizonPadAudioProcessorEditor> (this)] (const juce::FileChooser& chooser)
    {
        if (safe != nullptr && chooser.getResult() != juce::File())
            safe->importPreset (chooser.getResult());
    });
}

void HorizonPadAudioProcessorEditor::exportPresetFile()
{
    const auto name = horizonProcessor.getActivePresetName().isNotEmpty() ? horizonProcessor.getActivePresetName()
                                                                          : juce::String ("Horizon Pad");
    const auto start = juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                           .getChildFile (horizon::PresetFile::slugFor (name) + horizon::PresetFile::kExtension);

    fileChooser = std::make_unique<juce::FileChooser> ("Export the current sound", start,
                                                       juce::String ("*") + horizon::PresetFile::kExtension);
    fileChooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<HorizonPadAudioProcessorEditor> (this)] (const juce::FileChooser& chooser)
    {
        if (safe == nullptr || chooser.getResult() == juce::File())
            return;

        const auto file = chooser.getResult().withFileExtension (horizon::PresetFile::kExtension);

        if (const auto error = safe->horizonProcessor.exportPreset (file); error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Preset not exported", error);
    });
}

//==============================================================================
void HorizonPadAudioProcessorEditor::setScaleFactor (float newScale)
{
    hostScale = newScale;
    applyScale();
}

void HorizonPadAudioProcessorEditor::applyScale()
{
    AudioProcessorEditor::setScaleFactor (hostScale * horizonProcessor.getEditorScale());
}

void HorizonPadAudioProcessorEditor::timerCallback()
{
    // Only on screen: the sunrise must not play out before anyone can see it.
    if (! isShowing())
        return;

    const auto now = clockNow();

    if (lastVBlankTime < 0.0 || now - lastVBlankTime > kVBlankTimeout)
        advanceFrame (now);
}

void HorizonPadAudioProcessorEditor::pullSceneTargets()
{
    sky.getScene().setTargets (horizon::toUnit (filterParam->load (std::memory_order_relaxed)),
                               horizon::toUnit (reverbParam->load (std::memory_order_relaxed)),
                               horizon::toUnit (widthParam->load (std::memory_order_relaxed)));
}

void HorizonPadAudioProcessorEditor::showRecalledPresetTitle()
{
    const auto index = horizonProcessor.getActivePresetIndex();

    switch (horizonProcessor.getActivePresetKind())
    {
        case HorizonPadAudioProcessor::PresetKind::factory:
        {
            const auto& presets = horizonProcessor.getPresets();

            if (juce::isPositiveAndBelow (index, (int) presets.size()))
                sky.showPresetTitle (presets[(size_t) index].name, presets[(size_t) index].description);

            break;
        }

        case HorizonPadAudioProcessor::PresetKind::user:
        {
            const auto& presets = horizonProcessor.getUserPresets();

            if (juce::isPositiveAndBelow (index, (int) presets.size()))
                sky.showPresetTitle (presets[(size_t) index].name, {});

            break;
        }

        case HorizonPadAudioProcessor::PresetKind::none:
            break;
    }
}

void HorizonPadAudioProcessorEditor::advanceFrame (double now)
{
    if (lastFrameTime >= 0.0 && now - lastFrameTime < kMinFrameInterval)
        return;

    const auto dt = lastFrameTime < 0.0 ? 1.0f / 60.0f : (float) juce::jmin (0.1, now - lastFrameTime);
    lastFrameTime = now;

    if (sunrisePending)
    {
        sunrisePending = false;
        strip.beginWave (now + kSunriseKnobDelay, kWaveStep, false);
    }

    // --- A preset recall or A/B switch since the last frame: the knobs
    // sweep to the new values in a wave, the sky takes its slower glide,
    // and a recalled preset shows its title.
    const auto recalls = horizonProcessor.getPresetRecallCount();
    const auto switches = horizonProcessor.getBufferSwitchCount();

    if (recalls != seenRecallCount || switches != seenSwitchCount)
    {
        strip.beginWave (now, kWaveStep, false);
        sky.getScene().beginRecallGlide();

        if (recalls != seenRecallCount)
            showRecalledPresetTitle();

        seenRecallCount = recalls;
        seenSwitchCount = switches;
    }

    // --- The sky.
    pullSceneTargets();
    sceneTime += dt;

    if (sceneTime >= kSceneFrameInterval)
    {
        sky.advanceScene (sceneTime);
        sceneTime = 0.0f;
    }

    sky.advanceTitle (now, dt);

    // --- Everything else, each repainting only what it changed.
    header.refresh();
    strip.advance (now, dt);
    drawer.refresh();
    toast.advance (now, dt);
    about.advance (dt);
}

//==============================================================================
void HorizonPadAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (Tokens::ground);
}

void HorizonPadAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();
    header.setBounds (r.removeFromTop (PluginHeader::kHeight));

    r.reduce (kBodyPadding, kBodyPadding);
    sky.setBounds (r.removeFromTop (kSkyHeight));
    r.removeFromTop (kSkyToStrip);
    strip.setBounds (r.removeFromTop (ControlStrip::kHeight));

    if (about.isVisible())
        about.setBounds (getLocalBounds());
}
