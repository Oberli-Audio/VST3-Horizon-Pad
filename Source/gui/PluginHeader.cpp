#include "PluginHeader.h"
#include "../PluginProcessor.h"

namespace horizon::ui
{

namespace
{
    constexpr int kSide = 16;          // space-4
    constexpr int kGap = 12;           // space-3, between header items
    constexpr int kControlHeight = 28;
    constexpr int kHit = 24;           // hit-min
    constexpr int kNameMinWidth = 148;

    const juce::String editedMark (" *");
}

//==============================================================================
/** One half of the A/B segmented control: the selected one in `accent`
    with `on-accent` text, the other `ink-muted`. */
class PluginHeader::SegmentButton final : public FlatButton
{
public:
    SegmentButton (const juce::String& letter, bool isLeft)
        : FlatButton (letter), left (isLeft)
    {
        setButtonText (letter);
        setClickingTogglesState (false);
    }

    float getCorner() const override { return Tokens::radiusMd; }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const auto on = getToggleState();

        if (on || highlighted || down)
        {
            // Inside the group's 1 px border, its outer corners rounded.
            const auto inner = bounds.reduced (left ? 1.0f : 0.0f, 1.0f).withTrimmedRight (left ? 0.0f : 1.0f);
            juce::Path p;
            p.addRoundedRectangle (inner.getX(), inner.getY(), inner.getWidth(), inner.getHeight(),
                                   Tokens::radiusMd - 1.0f, Tokens::radiusMd - 1.0f, left, ! left, left, ! left);
            g.setColour (on ? Tokens::accent : Tokens::surfacePanel);
            g.fillPath (p);
        }

        type::draw (g, getButtonText(), bounds, type::control(), on ? Tokens::onAccent : Tokens::inkMuted,
                    juce::Justification::centred);
    }

private:
    const bool left;
};

//==============================================================================
/** The preset's name inside the pill; a click opens the drawer. */
class PluginHeader::PresetNameButton final : public FlatButton
{
public:
    PresetNameButton() : FlatButton ("Presets")
    {
        setTooltip ("All presets: browse, search and audition them.");
    }

    void set (const juce::String& newName, bool isEdited, bool isEmpty)
    {
        name = newName;
        edited = isEdited;
        empty = isEmpty;
        setTitle ("Preset: " + name + (edited ? ", edited" : "") + ". Opens the preset list.");
        repaint();
    }

    int getPreferredWidth() const
    {
        const auto text = type::width (type::control(), name + (edited ? editedMark : juce::String()));
        return juce::jmax (kNameMinWidth, juce::roundToInt (std::ceil (text)) + 16);
    }

    float getCorner() const override { return Tokens::radiusSm; }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto bounds = getLocalBounds().toFloat();

        if (highlighted || down)
        {
            g.setColour (Tokens::surfacePanel);
            g.fillRoundedRectangle (bounds, Tokens::radiusSm);
        }

        const auto font = type::control();
        const auto nameWidth = type::width (font, name);
        const auto markWidth = edited ? type::width (font, editedMark) : 0.0f;
        auto line = bounds.withSizeKeepingCentre (nameWidth + markWidth, bounds.getHeight());

        type::draw (g, name, line.removeFromLeft (nameWidth), font, empty ? Tokens::inkMuted : Tokens::ink);

        if (edited)
            type::draw (g, editedMark, line, font, Tokens::accentInk);
    }

private:
    juce::String name;
    bool edited = false, empty = false;
};

//==============================================================================
PluginHeader::PluginHeader (HorizonPadAudioProcessor& processorToUse)
    : processor (processorToUse),
      mark (logo::nameOnBone()),
      slotA (std::make_unique<SegmentButton> ("A", true)),
      slotB (std::make_unique<SegmentButton> ("B", false)),
      presetName (std::make_unique<PresetNameButton>())
{
    setOpaque (true);

    slotA->setTitle ("Sound A");
    slotB->setTitle ("Sound B");
    slotA->setTooltip ("Switch to A. A and B each keep their own settings, so you can compare two versions of a sound.");
    slotB->setTooltip ("Switch to B. A and B each keep their own settings, so you can compare two versions of a sound.");
    slotA->onClick = [this] { processor.switchBuffer (0); refresh(); };
    slotB->onClick = [this] { processor.switchBuffer (1); refresh(); };

    copyButton.onClick = [this]
    {
        const auto active = processor.getActiveBufferIndex();
        processor.copyActiveBufferToOtherBuffer();

        if (onCopied != nullptr)
            onCopied (active == 0 ? "Copied A to B" : "Copied B to A", copyButton.getBounds());
    };

    previousButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (1); };
    nextButton.setTooltip ("Next preset: the factory presets, then your own");

    presetName->onClick = [this] { if (onPresetNameClicked != nullptr) onPresetNameClicked(); };
    menuButton.onClick = [this] { if (onMenuClicked != nullptr) onMenuClicked(); };

    for (juce::Component* c : { (juce::Component*) slotA.get(), (juce::Component*) slotB.get(), (juce::Component*) &copyButton,
                                (juce::Component*) &previousButton, (juce::Component*) presetName.get(),
                                (juce::Component*) &nextButton, (juce::Component*) &menuButton })
        addAndMakeVisible (c);

    refresh();
}

PluginHeader::~PluginHeader() = default;

juce::Component& PluginHeader::getMenuButton() noexcept        { return menuButton; }
juce::Component& PluginHeader::getPresetNameButton() noexcept  { return *presetName; }

void PluginHeader::setDrawerOpen (bool isOpen)
{
    if (drawerOpen != isOpen)
    {
        drawerOpen = isOpen;
        repaint (pillBounds.expanded (1));
    }
}

void PluginHeader::setMenuOpen (bool isOpen)
{
    menuButton.setActive (isOpen);
}

void PluginHeader::refresh()
{
    auto name = processor.getActivePresetName();
    const auto none = name.isEmpty() || processor.getActivePresetKind() == HorizonPadAudioProcessor::PresetKind::none;

    if (none)
        name = "No preset";

    const auto edited = ! none && processor.isActivePresetModified();
    const auto previousWidth = presetName->getPreferredWidth();

    presetName->set (name, edited, none);

    if (presetName->getPreferredWidth() != previousWidth)
    {
        resized();
        repaint();
    }

    const auto active = processor.getActiveBufferIndex();

    if (slotA->getToggleState() != (active == 0))
    {
        slotA->setToggleState (active == 0, juce::dontSendNotification);
        slotB->setToggleState (active == 1, juce::dontSendNotification);

        const auto copyText = active == 0 ? "Copy A to B" : "Copy B to A";
        copyButton.setTooltip (copyText);
        copyButton.setTitle (copyText);
    }
}

void PluginHeader::stepPreset (int delta)
{
    // One list: the factory presets, then the player's own, wrapping round.
    const auto numFactory = (int) processor.getPresets().size();
    const auto total = numFactory + (int) processor.getUserPresets().size();

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
        processor.applyFactoryPreset (target);
    else
        processor.applyUserPreset (target - numFactory);

    refresh();
}

void PluginHeader::resized()
{
    auto r = getLocalBounds().withTrimmedBottom (1).reduced (kSide, 0);
    const auto centreY = (float) r.getCentreY();

    // --- Left: the mark, a hairline, the plugin's name.
    markBounds = juce::Rectangle<float> ((float) r.getX(), centreY - 9.0f, 69.0f, 18.0f);
    r.removeFromLeft (69 + 5 + kGap);
    dividerBounds = r.removeFromLeft (1).withSizeKeepingCentre (1, 20);
    r.removeFromLeft (kGap);
    nameBounds = r.removeFromLeft (juce::roundToInt (std::ceil (type::width (type::pluginName(), "Horizon Pad"))) + 2);

    // --- Right, from the edge in: menu, version, preset pill, copy, A/B.
    const auto centred = [&] (juce::Rectangle<int> column, int height) { return column.withSizeKeepingCentre (column.getWidth(), height); };

    menuButton.setBounds (centred (r.removeFromRight (kHit), kHit));
    r.removeFromRight (kGap);

    versionBounds = r.removeFromRight (juce::roundToInt (std::ceil (type::width (type::meta(), JucePlugin_VersionString))) + 1);
    r.removeFromRight (kGap);

    const auto nameWidth = presetName->getPreferredWidth();
    pillBounds = centred (r.removeFromRight (2 + kHit + nameWidth + kHit + 2), kControlHeight);
    {
        auto pill = pillBounds.reduced (2, 0);
        previousButton.setBounds (centred (pill.removeFromLeft (kHit), kHit));
        nextButton.setBounds (centred (pill.removeFromRight (kHit), kHit));
        presetName->setBounds (centred (pill, kHit));
    }
    r.removeFromRight (kGap);

    copyButton.setBounds (centred (r.removeFromRight (kHit), kHit));
    r.removeFromRight (kGap);

    abBounds = centred (r.removeFromRight (64), kControlHeight);
    {
        auto ab = abBounds;
        slotA->setBounds (ab.removeFromLeft (32));
        slotB->setBounds (ab);
    }
}

void PluginHeader::paint (juce::Graphics& g)
{
    g.fillAll (Tokens::surfaceRaised);

    g.setColour (Tokens::line);
    g.fillRect (getLocalBounds().removeFromBottom (1));
    g.fillRect (dividerBounds);

    if (mark != nullptr)
        mark->drawWithin (g, markBounds, juce::RectanglePlacement::centred, 1.0f);

    type::draw (g, "Horizon Pad", nameBounds.toFloat(), type::pluginName(), Tokens::ink);
    type::draw (g, JucePlugin_VersionString, versionBounds.toFloat(), type::meta(), Tokens::inkSubtle);

    // The A/B group's outline, drawn over by the selected half's fill.
    g.setColour (Tokens::lineControl);
    g.drawRoundedRectangle (abBounds.toFloat().reduced (0.5f), Tokens::radiusMd - 0.5f, 1.0f);

    // The preset pill.
    const auto pill = pillBounds.toFloat();
    g.setColour (Tokens::surfaceSunken);
    g.fillRoundedRectangle (pill, pill.getHeight() * 0.5f);
    g.setColour (drawerOpen ? Tokens::ink : Tokens::lineControl);
    g.drawRoundedRectangle (pill.reduced (0.5f), pill.getHeight() * 0.5f - 0.5f, 1.0f);
}

void PluginHeader::paintOverChildren (juce::Graphics& g)
{
    // The A/B outline again, over the selected half's square inner edge.
    g.setColour (Tokens::lineControl);
    g.drawRoundedRectangle (abBounds.toFloat().reduced (0.5f), Tokens::radiusMd - 0.5f, 1.0f);

    drawChildFocusRings (g, *this);
}

} // namespace horizon::ui
