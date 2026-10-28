#pragma once

#include "HorizonLookAndFeel.h"

namespace horizon::ui
{

/**
    The base of every button in the editor: it never takes keyboard focus
    on a click (the space bar keeps reaching the host's transport), Tab
    still reaches it, and on a focus change it asks its parent to repaint,
    because the 2 px `ink` focus ring sits 3 px outside its bounds and the
    parent draws it (drawChildFocusRings(), from paintOverChildren()).
*/
class FlatButton : public juce::Button
{
public:
    explicit FlatButton (const juce::String& name);

    /** The corner of the button's shape, for its focus ring. */
    virtual float getCorner() const { return Tokens::radiusSm; }

    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;
};

/** Draws the focus ring of whichever direct child of `parent` that is a
    FlatButton has keyboard focus. Call from the parent's paintOverChildren(). */
void drawChildFocusRings (juce::Graphics&, juce::Component& parent);

/** A ghost icon button (Icon card): a 16 px thin-line icon in a 24 px hit
    area, `surface-panel` under the pointer or while its menu is open. The
    tooltip and the accessible name are the same words. */
class IconButton final : public FlatButton
{
public:
    IconButton (icons::Id, const juce::String& label, bool pillShaped = false);

    void setActive (bool isActive);
    void setIconColour (juce::Colour);

    float getCorner() const override { return pill ? (float) getHeight() * 0.5f : Tokens::radiusSm; }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    const icons::Id icon;
    const bool pill;
    bool active = false;
    juce::Colour colour = Tokens::ink;
};

/** A text button (Button card): `control` text, 28 px or more, `radius-md`,
    12 px side padding. Primary: `accent` fill, `on-accent` text, one per
    view. Secondary: a `line-control` outline. Ghost: no border, for Cancel
    and quiet actions. On the bone back of the panel the same three use the
    Bone theme's tokens. */
class ActionButton final : public FlatButton
{
public:
    enum class Variant { primary, secondary, ghost };

    ActionButton (const juce::String& text, Variant, bool onBone = false, std::optional<icons::Id> leadingIcon = {});

    /** The width the label needs, padding included. */
    int getPreferredWidth() const;

    float getCorner() const override { return Tokens::radiusMd; }
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    const Variant variant;
    const bool onBone;
    const std::optional<icons::Id> leadingIcon;
};

} // namespace horizon::ui
