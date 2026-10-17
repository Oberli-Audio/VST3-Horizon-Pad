#include "PluginEditor.h"

#include <limits>

using namespace horizon::ui;

namespace
{
    // Frames: about 60 per second is plenty for this much motion, and the
    // scene (a full-window repaint while it moves) is stepped at about 30 -
    // it only ever moves slowly.
    constexpr double kMinFrameInterval = 1.0 / 75.0;
    constexpr float kSceneFrameInterval = 1.0f / 32.0f;

    // Without a vertical blank for this long (no display, a host that hides
    // the window oddly) the fallback timer drives the frames instead.
    constexpr double kVBlankTimeout = 0.25;

    // The recall wave: one card after another, left to right, then the
    // MACROS card's rows top to bottom.
    constexpr double kWaveStep = 0.04;
    constexpr double kWaveRowStep = 0.03;

    // Sunrise on open: the knobs start sweeping once the sky has begun to lighten.
    constexpr double kSunriseKnobDelay = 0.15;
}

HorizonPadAudioProcessorEditor::HorizonPadAudioProcessorEditor (HorizonPadAudioProcessor& processorToUse)
    : AudioProcessorEditor (processorToUse),
      horizonProcessor (processorToUse),
      presetBar (processorToUse),
      pitchWheel (processorToUse, WheelSlider::Kind::pitch),
      modWheel (processorToUse, WheelSlider::Kind::mod),
      rootKnob (processorToUse, (int) horizon::LayerIndex::warmFoundation,
               "ROOT", "life grows", horizon::ParamID::rootVolume),
      clearingKnob (processorToUse, (int) horizon::LayerIndex::analogEnsemble,
                   "CLEARING", "light breaks in", horizon::ParamID::clearingVolume),
      expanseKnob (processorToUse, (int) horizon::LayerIndex::airyChoir,
                  "EXPANSE", "life opens up", horizon::ParamID::expanseVolume),
      bloomKnob (processorToUse, (int) horizon::LayerIndex::motionPad,
                "BLOOM", "life blooms", horizon::ParamID::bloomVolume),
      macrosPanel (processorToUse),
      outputMeter (processorToUse),
      footerBar (processorToUse),
      tooltipWindow (this),
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

    addAndMakeVisible (titleBanner);
    addAndMakeVisible (presetBar);
    addAndMakeVisible (pitchWheel);
    addAndMakeVisible (modWheel);
    addAndMakeVisible (rootKnob);
    addAndMakeVisible (clearingKnob);
    addAndMakeVisible (expanseKnob);
    addAndMakeVisible (bloomKnob);
    addAndMakeVisible (macrosPanel);
    addAndMakeVisible (outputMeter);
    addAndMakeVisible (footerBar);

    // The panel (HorizonScene) fills the window edge to edge, so nothing
    // behind the editor ever shows: the host need not paint or blend it.
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
    scene.snapToTargets();

    if (horizonProcessor.takeFirstEditorOpen())
    {
        scene.prepareSunrise();
        titleBanner.prepareSunrise();
        startWave (std::numeric_limits<double>::max(), true);
    }
    else
    {
        sunrisePending = false;
    }

    presetBar.setBackdropColour (scene.skyColourAt (presetRowProportion));

    // Tab walks the controls in reading order: presets, wheels, pads, macros.
    setFocusContainerType (FocusContainerType::keyboardFocusContainer);

    footerBar.onScaleChosen = [this] (float scale)
    {
        horizonProcessor.setEditorScale (scale);
        applyScale();
    };

    applyScale();

    startTimerHz (30);
}

HorizonPadAudioProcessorEditor::~HorizonPadAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

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
    scene.setTargets (filterParam->load (std::memory_order_relaxed),
                      reverbParam->load (std::memory_order_relaxed),
                      widthParam->load (std::memory_order_relaxed));
}

void HorizonPadAudioProcessorEditor::startWave (double startTime, bool fromZero)
{
    PadKnob* pads[] { &rootKnob, &clearingKnob, &expanseKnob, &bloomKnob };

    for (int i = 0; i < (int) juce::numElementsInArray (pads); ++i)
        pads[i]->getKnob().beginWave (startTime + kWaveStep * i, fromZero);

    macrosPanel.beginWave (startTime + kWaveStep * juce::numElementsInArray (pads), kWaveRowStep, fromZero);
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
                titleBanner.showPresetTitle (presets[(size_t) index].name, presets[(size_t) index].description);

            break;
        }

        case HorizonPadAudioProcessor::PresetKind::user:
        {
            const auto& presets = horizonProcessor.getUserPresets();

            if (juce::isPositiveAndBelow (index, (int) presets.size()))
                titleBanner.showPresetTitle (presets[(size_t) index].name, {});

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
        startWave (now + kSunriseKnobDelay, false);
    }

    // --- A preset recall or A/B switch since the last frame: the knobs
    // sweep to the new values in a wave, the scene takes its slower glide,
    // and a recalled preset shows its title.
    const auto recalls = horizonProcessor.getPresetRecallCount();
    const auto switches = horizonProcessor.getBufferSwitchCount();

    if (recalls != seenRecallCount || switches != seenSwitchCount)
    {
        startWave (now, false);
        scene.beginRecallGlide();

        if (recalls != seenRecallCount)
            showRecalledPresetTitle();

        seenRecallCount = recalls;
        seenSwitchCount = switches;
    }

    // --- The scene.
    pullSceneTargets();
    sceneTime += dt;

    if (sceneTime >= kSceneFrameInterval)
    {
        switch (scene.advance (sceneTime))
        {
            case HorizonScene::Change::all:
                repaint();
                presetBar.setBackdropColour (scene.skyColourAt (presetRowProportion));
                break;

            case HorizonScene::Change::lower:
                repaint (HorizonScene::lowerRegion (getLocalBounds()));
                break;

            case HorizonScene::Change::none:
                break;
        }

        sceneTime = 0.0f;
    }

    // --- Everything else, each repainting only what it changed.
    titleBanner.advance (now, dt);
    presetBar.refreshFromProcessor();
    presetBar.advance (now, dt);

    pitchWheel.refreshFromProcessor();
    modWheel.refreshFromProcessor();

    rootKnob.advance (now, dt, horizonProcessor.getLayerLevel ((int) horizon::LayerIndex::warmFoundation));
    clearingKnob.advance (now, dt, horizonProcessor.getLayerLevel ((int) horizon::LayerIndex::analogEnsemble));
    expanseKnob.advance (now, dt, horizonProcessor.getLayerLevel ((int) horizon::LayerIndex::airyChoir));
    bloomKnob.advance (now, dt, horizonProcessor.getLayerLevel ((int) horizon::LayerIndex::motionPad));
    macrosPanel.advance (now, dt);
    outputMeter.advance (now, dt);

    footerBar.refreshFromProcessor();
}

void HorizonPadAudioProcessorEditor::paint (juce::Graphics& g)
{
    scene.paint (g, getLocalBounds().toFloat());

    g.setColour (Palette::dividerColor);
    g.fillRect (dividerBounds);
}

void HorizonPadAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (kSidePadding, kTopBottomPadding);

    // The banner also owns the 26 px strip below the wordmark, where a
    // recalled preset's title appears.
    titleBanner.setBounds (r.removeFromTop (TitleBanner::kHeaderHeight + 26));
    dividerBounds = r.removeFromTop (1);

    r.removeFromTop (22);
    presetBar.setBounds (r.removeFromTop (36));
    presetRowProportion = (float) presetBar.getBounds().getCentreY() / (float) juce::jmax (1, getHeight());

    r.removeFromTop (36);

    // The grid row's height is set by its tallest cards (PITCH/MOD/OUTPUT,
    // whose 190px vertical tracks/meters dominate), matching the design's
    // CSS grid (implicit row height = max content) rather than a fixed guess.
    constexpr int kGridRowHeight = 346;
    constexpr int kGridGap = 14;
    auto gridArea = r.removeFromTop (kGridRowHeight);

    // Eight equal-width columns: PITCH and MOD ride the wheels; four blend
    // the pads (ROOT/CLEARING/EXPANSE/BLOOM); the MACROS panel shapes the
    // tone (ATTACK/RELEASE, FILTER/REVERB, WIDTH/DETUNE); OUTPUT is the meter -
    // mirroring a Launchkey 25's eight knobs, per the design's mainGridStyle.
    juce::Component* columns[] {
        &pitchWheel, &modWheel, &rootKnob, &clearingKnob,
        &expanseKnob, &bloomKnob, &macrosPanel, &outputMeter,
    };

    const auto numColumns = (float) juce::numElementsInArray (columns);
    const auto totalGap = kGridGap * (numColumns - 1.0f);
    const auto colWidth = ((float) gridArea.getWidth() - totalGap) / numColumns;

    auto x = (float) gridArea.getX();

    for (auto* c : columns)
    {
        c->setBounds (juce::Rectangle<int> (juce::roundToInt (x), gridArea.getY(),
                                            juce::roundToInt (colWidth), gridArea.getHeight()));
        x += colWidth + (float) kGridGap;
    }

    r.removeFromTop (28);
    footerBar.setBounds (r.removeFromTop (24));
}
