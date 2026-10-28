#include "HorizonLookAndFeel.h"

#include <UiData.h>

#include <map>
#include <tuple>

namespace horizon::ui
{

namespace
{
    /** The five Rubik weights, registered once and kept while any editor
        (its LookAndFeel holds one) is alive. */
    struct Typefaces
    {
        Typefaces()
        {
            const std::pair<const char*, int> files[] {
                { UiData::RubikRegular_ttf,   UiData::RubikRegular_ttfSize },
                { UiData::RubikMedium_ttf,    UiData::RubikMedium_ttfSize },
                { UiData::RubikSemiBold_ttf,  UiData::RubikSemiBold_ttfSize },
                { UiData::RubikBold_ttf,      UiData::RubikBold_ttfSize },
                { UiData::RubikExtraBold_ttf, UiData::RubikExtraBold_ttfSize },
            };

            for (size_t i = 0; i < faces.size(); ++i)
                faces[i] = juce::Typeface::createSystemTypefaceFor (files[i].first, (size_t) files[i].second);
        }

        std::array<juce::Typeface::Ptr, 5> faces;

        // Every style the editor draws, built once: a copy of a Font shares its
        // shaped native font, so paint() never builds one again. Message thread.
        std::map<std::tuple<int, int, int, bool>, juce::Font> fonts;
    };

    juce::Typeface::Ptr typefaceFor (type::Weight weight)
    {
        const juce::SharedResourcePointer<Typefaces> typefaces;
        return typefaces->faces[(size_t) weight];
    }

    juce::Font buildFont (juce::Typeface::Ptr face, float px, float trackingEm, bool tabular)
    {
        auto options = juce::FontOptions (face).withPointHeight (px);

        if (tabular)
            options = options.withFeatureEnabled ("tnum");

        juce::Font f (options);

        // JUCE adds tracking as a multiple of the line height; the tokens give it in em.
        if (! juce::exactlyEqual (trackingEm, 0.0f))
            f.setExtraKerningFactor (trackingEm * px / f.getHeight());

        return f;
    }

    std::unique_ptr<juce::Drawable> drawableFrom (const char* data, int size)
    {
        return juce::Drawable::createFromImageData (data, (size_t) size);
    }

    // Icon card path data, on the 16-unit grid.
    const char* iconPath (icons::Id id)
    {
        switch (id)
        {
            case icons::Id::chevronLeft:  return "M10 3.5 5.5 8l4.5 4.5";
            case icons::Id::chevronRight: return "M6 3.5 10.5 8 6 12.5";
            case icons::Id::menu:         return "M3 4.5h10M3 8h10M3 11.5h10";
            case icons::Id::copy:         return "M2.5 5.5h9M9 3l2.5 2.5L9 8M13.5 10.5h-9M7 8l-2.5 2.5L7 13";
            case icons::Id::load:         return "M8 9.5v-7M5 5.5l3-3 3 3M3 11v2.5h10V11";
            case icons::Id::save:         return "M8 2.5v7M5 6.5l3 3 3-3M3 11v2.5h10V11";
            case icons::Id::about:        return "M8 7.5v3.5M8 5h.01";
            case icons::Id::close:        return "M4.5 4.5l7 7M11.5 4.5l-7 7";
        }

        return "";
    }
}

//==============================================================================
juce::Font type::font (Weight weight, float px, float trackingEm, bool tabular)
{
    const juce::SharedResourcePointer<Typefaces> typefaces;
    const auto key = std::make_tuple ((int) weight, juce::roundToInt (px * 100.0f), juce::roundToInt (trackingEm * 1000.0f), tabular);

    if (const auto found = typefaces->fonts.find (key); found != typefaces->fonts.end())
        return found->second;

    return typefaces->fonts.emplace (key, buildFont (typefaces->faces[(size_t) weight], px, trackingEm, tabular)).first->second;
}

void type::draw (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> lineBox,
                 const juce::Font& f, juce::Colour colour, juce::Justification justification)
{
    g.setFont (f);
    g.setColour (colour);
    g.drawText (text, lineBox, justification, false);
}

float type::width (const juce::Font& f, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidth (f, text);
}

//==============================================================================
void icons::draw (juce::Graphics& g, Id id, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto scale = juce::jmin (area.getWidth(), area.getHeight()) / 16.0f;
    const auto transform = juce::AffineTransform::scale (scale)
                               .translated (area.getCentreX() - 8.0f * scale, area.getCentreY() - 8.0f * scale);

    auto path = juce::Drawable::parseSVGPath (iconPath (id));

    if (id == Id::about)
        path.addEllipse (2.0f, 2.0f, 12.0f, 12.0f);

    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  transform);
}

//==============================================================================
std::unique_ptr<juce::Drawable> logo::nameOnBone()
{
    return drawableFrom (UiData::oberlibone_svg, UiData::oberlibone_svgSize);
}

std::unique_ptr<juce::Drawable> logo::lockupOnCharcoal()
{
    return drawableFrom (UiData::oberliaudiocharcoal_svg, UiData::oberliaudiocharcoal_svgSize);
}

//==============================================================================
void drawKnob (juce::Graphics& g, juce::Rectangle<float> box, float proportion, bool focused)
{
    const auto size = juce::jmin (box.getWidth(), box.getHeight());
    const auto centre = box.getCentre();
    const auto v = juce::jlimit (0.0f, 1.0f, proportion);

    // Angles clockwise from 12 o'clock, as Path::addCentredArc takes them.
    const auto start = juce::degreesToRadians (-135.0f);
    const auto end = juce::degreesToRadians (135.0f);
    const auto angle = start + (end - start) * v;

    const auto radius = 0.46f * size;
    const auto stroke = size <= 32.0f ? 2.0f : 3.0f;
    const juce::PathStrokeType arcStroke (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, end, true);
    g.setColour (Tokens::lineStrong);
    g.strokePath (track, arcStroke);

    if (angle - start > 0.009f)   // half a degree
    {
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, start, angle, true);
        g.setColour (Tokens::accent);
        g.strokePath (arc, arcStroke);
    }

    const auto capRadius = 0.36f * size;
    const auto cap = juce::Rectangle<float> (2.0f * capRadius, 2.0f * capRadius).withCentre (centre);
    g.setColour (Tokens::surfacePanel);
    g.fillEllipse (cap);
    g.setColour (Tokens::lineControl);
    g.drawEllipse (cap, 1.5f);

    // The pointer, from the 100-unit master drawing.
    const auto unit = size / 100.0f;
    juce::Path pointer;
    pointer.addRoundedRectangle (box.getX() + (box.getWidth() - size) * 0.5f + 43.88f * unit,
                                 box.getY() + (box.getHeight() - size) * 0.5f + 19.4f * unit,
                                 12.24f * unit, 24.48f * unit, 6.12f * unit);
    g.setColour (Tokens::accent);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle, centre.x, centre.y));

    if (focused)
        drawFocusRing (g, cap, capRadius);
}

void drawFocusRing (juce::Graphics& g, juce::Rectangle<float> controlBounds, float corner)
{
    // 3 px gap, then 2 px of ink: the stroke's centre sits 4 px out.
    g.setColour (Tokens::ink);
    g.drawRoundedRectangle (controlBounds.expanded (4.0f), corner + 4.0f, 2.0f);
}

void drawPopCard (juce::Graphics& g, juce::Rectangle<float> bounds, float corner,
                  juce::Colour fill, juce::Colour border)
{
    juce::Path shape;
    shape.addRoundedRectangle (bounds, corner);
    Tokens::popShadow().drawForPath (g, shape);

    g.setColour (fill);
    g.fillPath (shape);
    g.setColour (border);
    g.drawRoundedRectangle (bounds.reduced (0.5f), corner - 0.5f, 1.0f);
}

//==============================================================================
HorizonLookAndFeel::HorizonLookAndFeel()
{
    // Holds the typefaces for as long as this LookAndFeel lives.
    static_cast<void> (typefaceFor (type::Weight::regular));

    setColour (juce::ResizableWindow::backgroundColourId, Tokens::ground);
    setColour (juce::TextEditor::textColourId,            Tokens::ink);
    setColour (juce::TextEditor::backgroundColourId,      Tokens::surfaceSunken);
    setColour (juce::TextEditor::outlineColourId,         Tokens::line);
    setColour (juce::TextEditor::focusedOutlineColourId,  Tokens::ink);
    setColour (juce::TextEditor::highlightColourId,       Tokens::lineControl);
    setColour (juce::TextEditor::highlightedTextColourId, Tokens::ink);
    setColour (juce::CaretComponent::caretColourId,       Tokens::ink);
    setColour (juce::ScrollBar::thumbColourId,            Tokens::lineControl);
    setColour (juce::TooltipWindow::textColourId,         Tokens::onReadout);
    setColour (juce::AlertWindow::backgroundColourId,     Tokens::surfaceRaised);
    setColour (juce::AlertWindow::textColourId,           Tokens::ink);
    setColour (juce::AlertWindow::outlineColourId,        Tokens::lineControl);
    setColour (juce::TextButton::buttonColourId,          Tokens::surfacePanel);
    setColour (juce::TextButton::textColourOffId,         Tokens::ink);
    setColour (juce::Label::textColourId,                 Tokens::ink);
}

juce::Typeface::Ptr HorizonLookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    // Anything asking for the default face gets Rubik: JUCE's own text
    // editors, alert windows and tooltips included.
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()
        || f.getTypefaceName() == juce::Font::getDefaultSerifFontName())
        return typefaceFor (f.isBold() ? type::Weight::bold : type::Weight::regular);

    return LookAndFeel_V4::getTypefaceForFont (f);
}

//==============================================================================
std::unique_ptr<juce::TextLayout> HorizonLookAndFeel::layoutTooltip (const juce::String& text) const
{
    // The parameter table's sentence, then its generated range and default
    // (" Range ...") on a line of its own at 75 %.
    const auto sentence = text.upToFirstOccurrenceOf (" Range ", false, false).trim();
    const auto range = text.fromFirstOccurrenceOf (" Range ", true, false).trim();

    juce::AttributedString s;
    s.setJustification (juce::Justification::topLeft);
    s.setWordWrap (juce::AttributedString::byWord);
    s.setLineSpacing (5.0f);
    s.append (sentence, type::font (type::Weight::regular, 12.0f), Tokens::onReadout);

    if (range.isNotEmpty())
        s.append ("\n" + range, type::font (type::Weight::regular, 12.0f, 0.0f, true), Tokens::onReadout.withAlpha (0.75f));

    auto layout = std::make_unique<juce::TextLayout>();
    layout->createLayoutWithBalancedLineLengths (s, 240.0f - 2.0f * 8.0f);
    return layout;
}

juce::Rectangle<int> HorizonLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                           juce::Rectangle<int> parentArea)
{
    const auto layout = layoutTooltip (tipText);
    const auto w = juce::roundToInt (layout->getWidth() + 16.0f);
    const auto h = juce::roundToInt (layout->getHeight() + 12.0f);

    // Below and right of the pointer, so the control's own value stays visible.
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 12,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 8) : screenPos.y + 20,
                                 w, h).constrainedWithin (parentArea);
}

void HorizonLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto bounds = juce::Rectangle<float> ((float) width, (float) height);

    g.setColour (Tokens::readout);
    g.fillRoundedRectangle (bounds, Tokens::radiusSm);

    layoutTooltip (text)->draw (g, bounds.reduced (8.0f, 6.0f));
}

//==============================================================================
void HorizonLookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.findColour (juce::TextEditor::backgroundColourId));
    g.fillRoundedRectangle (juce::Rectangle<float> ((float) width, (float) height), Tokens::radiusMd);
}

void HorizonLookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    const auto bounds = juce::Rectangle<float> ((float) width, (float) height).reduced (0.5f);
    g.setColour (editor.findColour (editor.hasKeyboardFocus (true) ? juce::TextEditor::focusedOutlineColourId
                                                                   : juce::TextEditor::outlineColourId));
    g.drawRoundedRectangle (bounds, Tokens::radiusMd - 0.5f, 1.0f);
}

void HorizonLookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                        bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                                        bool isMouseOver, bool isMouseDown)
{
    auto thumb = isScrollbarVertical ? juce::Rectangle<int> (x, thumbStartPosition, width, thumbSize)
                                     : juce::Rectangle<int> (thumbStartPosition, y, thumbSize, height);
    thumb = thumb.reduced (2);

    g.setColour ((isMouseOver || isMouseDown) ? Tokens::inkSubtle : Tokens::lineControl);
    g.fillRoundedRectangle (thumb.toFloat(), (float) juce::jmin (thumb.getWidth(), thumb.getHeight()) * 0.5f);
}

} // namespace horizon::ui
