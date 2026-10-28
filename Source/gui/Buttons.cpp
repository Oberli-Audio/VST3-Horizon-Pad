#include "Buttons.h"

namespace horizon::ui
{

FlatButton::FlatButton (const juce::String& name)
    : juce::Button (name)
{
    setMouseClickGrabsKeyboardFocus (false);
    setWantsKeyboardFocus (true);
    setTitle (name);
}

void FlatButton::focusGained (FocusChangeType)
{
    if (auto* parent = getParentComponent())
        parent->repaint (getBounds().expanded (6));
}

void FlatButton::focusLost (FocusChangeType)
{
    if (auto* parent = getParentComponent())
        parent->repaint (getBounds().expanded (6));
}

void drawChildFocusRings (juce::Graphics& g, juce::Component& parent)
{
    for (auto* child : parent.getChildren())
        if (auto* button = dynamic_cast<FlatButton*> (child))
            if (button->isVisible() && button->hasKeyboardFocus (false))
                drawFocusRing (g, button->getBounds().toFloat(), button->getCorner());
}

//==============================================================================
IconButton::IconButton (icons::Id iconToUse, const juce::String& label, bool pillShaped)
    : FlatButton (label), icon (iconToUse), pill (pillShaped)
{
    setTooltip (label);
}

void IconButton::setActive (bool isActive)
{
    if (active != isActive)
    {
        active = isActive;
        repaint();
    }
}

void IconButton::setIconColour (juce::Colour c)
{
    colour = c;
    repaint();
}

void IconButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat();

    if (active || highlighted || down)
    {
        g.setColour (Tokens::surfacePanel);
        g.fillRoundedRectangle (bounds, getCorner());
    }

    icons::draw (g, icon, bounds.withSizeKeepingCentre (16.0f, 16.0f), colour.withMultipliedAlpha (isEnabled() ? 1.0f : 0.45f));
}

//==============================================================================
ActionButton::ActionButton (const juce::String& text, Variant v, bool bone, std::optional<icons::Id> icon)
    : FlatButton (text), variant (v), onBone (bone), leadingIcon (icon)
{
}

int ActionButton::getPreferredWidth() const
{
    const auto text = juce::roundToInt (std::ceil (type::width (type::control(), getButtonText())));
    return text + (leadingIcon ? 4 + 16 + 4 + 8 : 24);
}

void ActionButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto ink = onBone ? Tokens::Bone::ink : Tokens::ink;
    auto text = ink;

    switch (variant)
    {
        case Variant::primary:
            g.setColour (down ? Tokens::accent.darker (0.15f) : (highlighted ? Tokens::accent.brighter (0.08f) : Tokens::accent));
            g.fillRoundedRectangle (bounds, Tokens::radiusMd);
            text = Tokens::onAccent;
            break;

        case Variant::secondary:
            if (highlighted || down)
            {
                g.setColour (onBone ? Tokens::Bone::surfaceRaised : Tokens::surfaceRaised);
                g.fillRoundedRectangle (bounds, Tokens::radiusMd);
            }

            g.setColour (onBone ? Tokens::Bone::lineControl : Tokens::lineControl);
            g.drawRoundedRectangle (bounds.reduced (0.5f), Tokens::radiusMd - 0.5f, 1.0f);
            break;

        case Variant::ghost:
            if (highlighted || down)
            {
                g.setColour (onBone ? Tokens::Bone::surfaceSunken : Tokens::surfacePanel);
                g.fillRoundedRectangle (bounds, Tokens::radiusSm);
            }
            break;
    }

    if (! isEnabled())
        text = text.withMultipliedAlpha (0.45f);

    auto area = bounds;

    if (leadingIcon)
    {
        area.removeFromLeft (4.0f);
        icons::draw (g, *leadingIcon, area.removeFromLeft (16.0f).withSizeKeepingCentre (16.0f, 16.0f), text);
        area.removeFromLeft (4.0f);
        type::draw (g, getButtonText(), area, type::control(), text, juce::Justification::centredLeft);
    }
    else
    {
        type::draw (g, getButtonText(), area, type::control(), text, juce::Justification::centred);
    }
}

} // namespace horizon::ui
