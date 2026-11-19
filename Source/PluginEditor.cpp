#include "PluginEditor.h"
#include "BinaryData.h"

using C = soy::Colours;

namespace { juce::String u8 (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); } }

//============================================================================== Schriften
namespace
{
juce::Typeface::Ptr load (const void* data, int size)
{
    return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
}
juce::Typeface::Ptr tfDisplay()  { static auto t = load (BinaryData::FredokaSemiBold_ttf, BinaryData::FredokaSemiBold_ttfSize); return t; }
juce::Typeface::Ptr tfBody()     { static auto t = load (BinaryData::NunitoSemiBold_ttf,  BinaryData::NunitoSemiBold_ttfSize);  return t; }
juce::Typeface::Ptr tfBodyBold() { static auto t = load (BinaryData::NunitoBold_ttf,      BinaryData::NunitoBold_ttfSize);      return t; }
juce::Typeface::Ptr tfMono()     { static auto t = load (BinaryData::DMMonoMedium_ttf,    BinaryData::DMMonoMedium_ttfSize);    return t; }

juce::Font make (juce::Typeface::Ptr t, float h)
{
    if (t == nullptr) return juce::Font (juce::FontOptions (h));
    return juce::Font (juce::FontOptions (t).withHeight (h));
}
}

juce::Font soy::Fonts::display (float h)  { return make (tfDisplay(), h); }
juce::Font soy::Fonts::body (float h)     { return make (tfBody(), h); }
juce::Font soy::Fonts::bodyBold (float h) { return make (tfBodyBold(), h); }
juce::Font soy::Fonts::mono (float h)     { return make (tfMono(), h); }

juce::ColourGradient soy::brandGradient (juce::Rectangle<float> r)
{
    juce::ColourGradient g (C::sun, r.getX(), r.getBottom(), C::rose, r.getRight(), r.getY(), false);
    g.addColour (0.55, C::coral);
    return g;
}

//============================================================================== LookAndFeel
soy::LookAndFeel::LookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, C::panel);
    setColour (juce::PopupMenu::textColourId, C::ink);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, C::soft);
    setColour (juce::PopupMenu::highlightedTextColourId, C::ink);
    setColour (juce::ComboBox::textColourId, C::ink);
    setColour (juce::ComboBox::arrowColourId, C::inkMuted);
    setColour (juce::TextButton::textColourOffId, C::ink);
    setColour (juce::TextButton::textColourOnId, C::ink);
    setColour (juce::Label::textColourId, C::ink);
    setColour (juce::BubbleComponent::backgroundColourId, C::panel);
    setColour (juce::BubbleComponent::outlineColourId, C::line);
    setColour (juce::TooltipWindow::textColourId, C::ink);
}

juce::Typeface::Ptr soy::LookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
        if (auto t = f.isBold() ? tfBodyBold() : tfBody())
            return t;
    return LookAndFeel_V4::getTypefaceForFont (f);
}

void soy::LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float startAngle, float endAngle, juce::Slider& s)
{
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float size = juce::jmin (b.getWidth(), b.getHeight());
    const auto c = b.getCentre();
    const float r = size * 0.5f - 4.0f;
    const float stroke = juce::jmax (4.0f, size * 0.075f);
    const bool bipolar = (bool) s.getProperties().getWithDefault ("bipolar", false);

    // Spur
    juce::Path track;
    track.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, endAngle, true);
    g.setColour (C::soft);
    g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Wert-Bogen im Marken-Verlauf
    const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
    const float to = startAngle + pos * (endAngle - startAngle);
    if (std::abs (to - from) > 0.01f)
    {
        juce::Path val;
        val.addCentredArc (c.x, c.y, r, r, 0.0f, juce::jmin (from, to), juce::jmax (from, to), true);
        g.setGradientFill (brandGradient ({ c.x - r, c.y - r, 2 * r, 2 * r }));
        g.strokePath (val, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Kappe
    const float capR = r * 0.66f;
    g.setColour (juce::Colour (0x22000000));
    g.fillEllipse (c.x - capR, c.y - capR + 2.0f, capR * 2, capR * 2);
    g.setColour (s.isMouseOverOrDragging() ? C::mist : C::soft);
    g.fillEllipse (c.x - capR, c.y - capR, capR * 2, capR * 2);
    g.setColour (C::lineStrong);
    g.drawEllipse (c.x - capR, c.y - capR, capR * 2, capR * 2, 1.5f);

    // Zeiger
    const float ang = to;
    const auto p1 = c.getPointOnCircumference (capR * 0.35f, ang);
    const auto p2 = c.getPointOnCircumference (capR * 0.82f, ang);
    g.setColour (C::ink);
    g.drawLine ({ p1, p2 }, juce::jmax (2.5f, size * 0.04f));
}

void soy::LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hi, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (1.0f);
    const float rad = r.getHeight() * 0.5f;
    const bool on = b.getToggleState();
    g.setColour (on ? C::soft : (hi ? C::mist : C::panel));
    g.fillRoundedRectangle (r, rad);
    if (on)
    {
        g.setColour (C::accentInk);
        g.drawRoundedRectangle (r.reduced (1.0f), rad - 1.0f, 2.0f);
    }
    else
    {
        g.setColour (down ? C::lineStrong : C::line);
        g.drawRoundedRectangle (r, rad, 1.5f);
    }
}

void soy::LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    auto r = b.getLocalBounds();
    const bool on = b.getToggleState();
    if (on && b.getProperties().getWithDefault ("chip", false))
    {
        // kleiner Punkt im Verlauf als "an"-Anzeige
        auto dot = r.removeFromLeft (r.getHeight()).toFloat().withSizeKeepingCentre (9.0f, 9.0f).translated (6.0f, 0.0f);
        g.setGradientFill (brandGradient (dot));
        g.fillEllipse (dot);
    }
    g.setColour (on ? C::ink : C::inkMuted);
    float fs = 14.0f; // Schrift verkleinern, falls der Platz knapp ist
    while (fs > 11.0f && Fonts::bodyBold (fs).getStringWidthFloat (b.getButtonText()) > (float) r.getWidth() - 10.0f) fs -= 0.5f;
    g.setFont (Fonts::bodyBold (fs));
    g.drawText (b.getButtonText(), r, juce::Justification::centred);
}

void soy::LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1.0f);
    g.setColour (box.isMouseOver (true) ? C::mist : C::panel);
    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
    g.setColour (C::line);
    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.5f);
    // Pfeil
    juce::Path p;
    const float ax = (float) w - 22.0f, ay = (float) h * 0.5f;
    p.startNewSubPath (ax - 4.5f, ay - 2.0f); p.lineTo (ax, ay + 2.5f); p.lineTo (ax + 4.5f, ay - 2.0f);
    g.setColour (C::inkMuted);
    g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void soy::LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (14, 1, box.getWidth() - 40, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

void soy::LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (C::panel);
    g.setColour (C::line);
    g.drawRect (0, 0, w, h, 1);
}

//============================================================================== Regler
SoyKnob::SoyKnob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& t, bool bipolar)
    : title (t)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.getProperties().set ("bipolar", bipolar);
    slider.setMouseDragSensitivity (220);
    slider.setVelocityBasedMode (false);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
    if (auto* p = s.getParameter (id))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

void SoyKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (18);
    r.removeFromBottom (20);
    slider.setBounds (r);
}

void SoyKnob::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (C::inkMuted);
    g.setFont (soy::Fonts::bodyBold (13.0f));
    g.drawText (title, r.removeFromTop (18), juce::Justification::centred);
    g.setColour (C::ink);
    g.setFont (soy::Fonts::mono (14.0f));
    auto txt = slider.getTextFromValue (slider.getValue());
    g.drawText (txt, r.removeFromBottom (20), juce::Justification::centred);
}

//============================================================================== Segment
SoySegment::SoySegment (juce::AudioProcessorValueTreeState& s, const juce::String& id, juce::StringArray labels)
{
    auto* prm = s.getParameter (id);
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (labels[i]));
        b->setClickingTogglesState (false);
        b->onClick = [this, i] { if (attachment) attachment->setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (b);
    }
    if (prm != nullptr)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*prm, [this] (float v)
        {
            const int idx = juce::roundToInt (v);
            for (int i = 0; i < buttons.size(); ++i) buttons[i]->setToggleState (i == idx, juce::dontSendNotification);
        });
        attachment->sendInitialUpdate();
    }
}

void SoySegment::resized()
{
    auto r = getLocalBounds();
    const int gap = 6, w = (r.getWidth() - gap * (buttons.size() - 1)) / juce::jmax (1, buttons.size());
    for (auto* b : buttons) { b->setBounds (r.removeFromLeft (w)); r.removeFromLeft (gap); }
}

//============================================================================== Spektrum
SpectrumView::SpectrumView (RelaxxerProcessor& p) : proc (p)
{
    nodes = {
        { "lowcut",  nullptr,  nullptr, "L", C::lineStrong },
        { "b1freq",  "b1sens", "b1q",   "1", C::sun },
        { "b2freq",  "b2sens", "b2q",   "2", C::rose },
        { "highcut", nullptr,  nullptr, "H", C::lineStrong },
    };
    level.fill (-120.0f);
    red.fill (0.0f);
    setRepaintsOnMouseActivity (false);
}

juce::RangedAudioParameter* SpectrumView::prm (const char* id) const { return proc.apvts.getParameter (id); }
float SpectrumView::val (const char* id) const { auto* p = prm (id); return p ? p->convertFrom0to1 (p->getValue()) : 0.0f; }
void SpectrumView::setVal (const char* id, float v) { if (auto* p = prm (id)) p->setValueNotifyingHost (p->convertTo0to1 (p->getNormalisableRange().snapToLegalValue (v))); }

juce::Rectangle<float> SpectrumView::plot() const { return getLocalBounds().toFloat().reduced (16.0f, 14.0f).withTrimmedBottom (16.0f); }
float SpectrumView::xOf (float hz) const { auto p = plot(); return p.getX() + p.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); }
float SpectrumView::hzOf (float x) const { auto p = plot(); return 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, (x - p.getX()) / p.getWidth())); }
float SpectrumView::sensY (float db) const { auto p = plot(); return p.getCentreY() + 0.12f * p.getHeight() - db / 12.0f * p.getHeight() * 0.36f; }

juce::Point<float> SpectrumView::nodePos (int i) const
{
    const auto& n = nodes[(size_t) i];
    const float x = xOf (val (n.freqID));
    const float y = n.sensID ? sensY (val (n.sensID)) : sensY (0.0f);
    return { x, y };
}

int SpectrumView::nodeAt (juce::Point<float> pt) const
{
    int best = -1; float bd = 18.0f;
    for (int i = 0; i < (int) nodes.size(); ++i)
    {
        const float d = nodePos (i).getDistanceFrom (pt);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

void SpectrumView::refresh()
{
    const auto& d = proc.display();
    for (int i = 0; i < relaxxer::Processor::displayPoints; ++i)
    {
        level[(size_t) i] = d.getDisplayLevel (i);
        const float r = d.getDisplayReduction (i);
        // Absenkung optisch sanft abklingen lassen; Anhebungen (negativ) direkt zeigen
        red[(size_t) i] = r >= 0.0f ? juce::jmax (r, red[(size_t) i] * 0.88f) : r;
    }
    maxRed = juce::jmax (d.getMaxReductionDb(), maxRed * 0.92f);
    inDb = d.getInputDb(); outDb = d.getOutputDb(); brightDb = d.getBrightnessDeltaDb(); makeupDb = d.getMakeupDb();
    repaint();
}

void SpectrumView::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    // Panel
    g.setColour (juce::Colour (0x14000000));
    g.fillRoundedRectangle (bounds.translated (0, 3).reduced (2.0f), 20.0f);
    g.setColour (C::panel);
    g.fillRoundedRectangle (bounds.reduced (1.0f), 20.0f);
    g.setColour (C::line);
    g.drawRoundedRectangle (bounds.reduced (1.0f), 20.0f, 1.0f);

    const auto p = plot();
    const auto params = proc.readParams();

    // Raster
    g.setFont (soy::Fonts::mono (11.0f));
    const float freqs[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
    const char* names[] = { "50", "100", "200", "500", "1k", "2k", "5k", "10k" };
    for (int i = 0; i < 8; ++i)
    {
        const float x = xOf (freqs[i]);
        g.setColour (C::soft);
        g.drawVerticalLine ((int) x, p.getY(), p.getBottom());
        g.setColour (C::inkMuted);
        g.drawText (names[i], juce::Rectangle<float> (x - 20, p.getBottom() + 2, 40, 14), juce::Justification::centred);
    }

    // Bereich außerhalb Low/High Cut abdunkeln
    {
        const float xl = xOf (params.lowCutHz), xh = xOf (params.highCutHz);
        g.setColour (C::soft.withAlpha (0.65f));
        g.fillRect (juce::Rectangle<float> (p.getX(), p.getY(), juce::jmax (0.0f, xl - p.getX()), p.getHeight()));
        g.fillRect (juce::Rectangle<float> (xh, p.getY(), juce::jmax (0.0f, p.getRight() - xh), p.getHeight()));
    }

    // Eingangsspektrum
    {
        juce::Path s;
        s.startNewSubPath (p.getX(), p.getBottom());
        for (int i = 0; i < relaxxer::Processor::displayPoints; ++i)
        {
            const float f = relaxxer::Processor::displayFreq (i);
            const float db = juce::jlimit (-100.0f, 0.0f, level[(size_t) i] + 4.5f * std::log2 (f / 1000.0f)); // +4,5 dB/Okt. Neigung wie übliche Analyzer
            s.lineTo (xOf (f), p.getBottom() - (db + 100.0f) / 100.0f * p.getHeight());
        }
        s.lineTo (p.getRight(), p.getBottom());
        s.closeSubPath();
        g.setGradientFill (juce::ColourGradient (C::sky.withAlpha (0.35f), 0, p.getY(), C::mist.withAlpha (0.4f), 0, p.getBottom(), false));
        g.fillPath (s);
        g.setColour (C::sky.withAlpha (0.8f));
        g.strokePath (s, juce::PathStrokeType (1.2f));
    }

    // Absenkung (von oben, im Marken-Verlauf)
    {
        juce::Path r;
        r.startNewSubPath (p.getX(), p.getY());
        for (int i = 0; i < relaxxer::Processor::displayPoints; ++i)
        {
            const float f = relaxxer::Processor::displayFreq (i);
            const float d = juce::jlimit (0.0f, 1.0f, red[(size_t) i] / 24.0f);
            r.lineTo (xOf (f), p.getY() + d * p.getHeight() * 0.7f);
        }
        r.lineTo (p.getRight(), p.getY());
        r.closeSubPath();
        auto grad = soy::brandGradient (p);
        grad.multiplyOpacity (0.85f);
        g.setGradientFill (grad);
        g.fillPath (r);
    }

    // Anhebung durch "Freilegen" (von unten, violett)
    {
        bool any = false;
        juce::Path b;
        b.startNewSubPath (p.getX(), p.getBottom());
        for (int i = 0; i < relaxxer::Processor::displayPoints; ++i)
        {
            const float f = relaxxer::Processor::displayFreq (i);
            const float up = juce::jlimit (0.0f, 1.0f, -red[(size_t) i] / 8.0f);
            any |= up > 0.01f;
            b.lineTo (xOf (f), p.getBottom() - up * p.getHeight() * 0.35f);
        }
        b.lineTo (p.getRight(), p.getBottom());
        b.closeSubPath();
        if (any)
        {
            g.setColour (C::violet.withAlpha (0.35f));
            g.fillPath (b);
            g.setColour (C::violet.withAlpha (0.8f));
            g.strokePath (b, juce::PathStrokeType (1.2f));
        }
    }

    // Fokus-Kurve (Empfindlichkeit)
    {
        juce::Path c;
        for (int i = 0; i <= 200; ++i)
        {
            const float f = 20.0f * std::pow (1000.0f, (float) i / 200.0f);
            const float y = sensY (juce::jlimit (-14.0f, 14.0f, relaxxer::Processor::focusWeightDb (f, params)));
            if (i == 0) c.startNewSubPath (xOf (f), y); else c.lineTo (xOf (f), y);
        }
        g.setColour (C::ink.withAlpha (0.75f));
        g.strokePath (c, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Knoten
    for (int i = 0; i < (int) nodes.size(); ++i)
    {
        const auto pt = nodePos (i);
        const float rr = (i == drag || i == hover) ? 11.0f : 9.5f;
        g.setColour (juce::Colour (0x26000000));
        g.fillEllipse (pt.x - rr, pt.y - rr + 2.0f, rr * 2, rr * 2);
        g.setColour (C::panel);
        g.fillEllipse (pt.x - rr, pt.y - rr, rr * 2, rr * 2);
        g.setColour (nodes[(size_t) i].colour);
        g.drawEllipse (pt.x - rr, pt.y - rr, rr * 2, rr * 2, 3.0f);
        g.setColour (C::ink);
        g.setFont (soy::Fonts::bodyBold (12.0f));
        g.drawText (nodes[(size_t) i].name, juce::Rectangle<float> (pt.x - rr, pt.y - rr, rr * 2, rr * 2), juce::Justification::centred);

        if (i == drag || i == hover)
        {
            const auto& n = nodes[(size_t) i];
            juce::String t = prm (n.freqID)->getCurrentValueAsText() + " Hz";
            if (n.sensID) t << "  " << juce::String (val (n.sensID), 1) << " dB  Q " << juce::String (val (n.qID), 2);
            auto box = juce::Rectangle<float> (0, 0, n.sensID ? 190.0f : 90.0f, 24.0f)
                           .withCentre ({ juce::jlimit (p.getX() + 95.0f, p.getRight() - 95.0f, pt.x), pt.y - 28.0f });
            g.setColour (C::panel);
            g.fillRoundedRectangle (box, 12.0f);
            g.setColour (C::line);
            g.drawRoundedRectangle (box, 12.0f, 1.0f);
            g.setColour (C::ink);
            g.setFont (soy::Fonts::mono (12.0f));
            g.drawText (t, box, juce::Justification::centred);
        }
    }

    // Legende
    {
        // Legende: max. Absenkung, Pegel rein/raus, Helligkeit (wie die Meter bei Gullfoss)
        auto leg = juce::Rectangle<float> (p.getX() + 8, p.getY() + 6, 560, 22);
        g.setColour (C::panel.withAlpha (0.92f));
        g.fillRoundedRectangle (leg, 11.0f);
        auto item = [&] (const juce::String& label, const juce::String& value, float w)
        {
            auto r = leg.removeFromLeft (w);
            g.setColour (C::inkMuted);
            g.setFont (soy::Fonts::body (13.0f));
            const float lw = soy::Fonts::body (13.0f).getStringWidthFloat (label) + 6.0f;
            g.drawText (label, r.removeFromLeft (lw + 10).withTrimmedLeft (10), juce::Justification::centredLeft);
            g.setColour (C::ink);
            g.setFont (soy::Fonts::mono (13.0f));
            g.drawText (value, r, juce::Justification::centredLeft);
        };
        auto db = [] (float v) { return v <= -99.0f ? juce::String ("-inf") : juce::String (v, 1); };
        auto sgn = [] (float v) { return (v >= 0.0f ? "+" : "") + juce::String (v, 1); };
        item ("Absenkung", juce::String (-maxRed, 1) + " dB", 152);
        item ("Rein", db (inDb), 92);
        item ("Raus", db (outDb), 92);
        item ("Helligkeit", sgn (brightDb) + " dB", 140);
        if (std::abs (makeupDb) > 0.05f) item ("Ausgleich", sgn (makeupDb), 84);
    }

    if (params.useSidechain && ! proc.sidechainConnected.load())
    {
        auto w = juce::Rectangle<float> (p.getRight() - 236, p.getY() + 6, 228, 22);
        g.setColour (C::panel.withAlpha (0.95f));
        g.fillRoundedRectangle (w, 11.0f);
        g.setColour (C::accentInk);
        g.setFont (soy::Fonts::bodyBold (13.0f));
        g.drawText ("Sidechain ist nicht verbunden", w, juce::Justification::centred);
    }
    else if (params.delta)
    {
        auto w = juce::Rectangle<float> (p.getRight() - 236, p.getY() + 6, 228, 22);
        g.setColour (C::panel.withAlpha (0.95f));
        g.fillRoundedRectangle (w, 11.0f);
        g.setColour (C::accentInk);
        g.setFont (soy::Fonts::bodyBold (13.0f));
        g.drawText (u8 ("Delta: du hörst nur das Entfernte"), w, juce::Justification::centred);
    }
}

void SpectrumView::mouseMove (const juce::MouseEvent& e)
{
    const int h = nodeAt (e.position);
    if (h != hover) { hover = h; repaint(); }
    setMouseCursor (h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
}

void SpectrumView::mouseDown (const juce::MouseEvent& e)
{
    drag = nodeAt (e.position);
    if (drag < 0) return;
    const auto& n = nodes[(size_t) drag];
    prm (n.freqID)->beginChangeGesture();
    if (n.sensID) prm (n.sensID)->beginChangeGesture();
}

void SpectrumView::mouseDrag (const juce::MouseEvent& e)
{
    if (drag < 0) return;
    const auto& n = nodes[(size_t) drag];
    setVal (n.freqID, hzOf (e.position.x));
    if (n.sensID)
    {
        const auto p = plot();
        const float db = (p.getCentreY() + 0.12f * p.getHeight() - e.position.y) / (p.getHeight() * 0.36f) * 12.0f;
        setVal (n.sensID, juce::jlimit (-12.0f, 12.0f, db));
    }
    repaint();
}

void SpectrumView::mouseUp (const juce::MouseEvent&)
{
    if (drag < 0) return;
    const auto& n = nodes[(size_t) drag];
    prm (n.freqID)->endChangeGesture();
    if (n.sensID) prm (n.sensID)->endChangeGesture();
    drag = -1;
    repaint();
}

void SpectrumView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int i = nodeAt (e.position);
    if (i < 0 || nodes[(size_t) i].sensID == nullptr) return;
    for (auto id : { nodes[(size_t) i].sensID, nodes[(size_t) i].qID })
        if (auto* p = prm (id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
}

void SpectrumView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int i = nodeAt (e.position);
    if (i < 0 || nodes[(size_t) i].qID == nullptr) return;
    auto* p = prm (nodes[(size_t) i].qID);
    p->beginChangeGesture();
    setVal (nodes[(size_t) i].qID, val (nodes[(size_t) i].qID) * std::pow (1.25f, w.deltaY * 4.0f));
    p->endChangeGesture();
    hover = i;
    repaint();
}

//============================================================================== Editor
RelaxxerEditor::RelaxxerEditor (RelaxxerProcessor& p)
    : AudioProcessorEditor (&p), proc (p), spectrum (p),
      depth       (p.apvts, "depth",       "Tiefe"),
      sharpness   (p.apvts, "sharpness",   u8 ("Schärfe")),
      selectivity (p.apvts, "selectivity", u8 ("Selektivität")),
      attack      (p.apvts, "attack",      "Attack"),
      release     (p.apvts, "release",     "Release"),
      link        (p.apvts, "link",        "Link"),
      balance     (p.apvts, "balance",     "Balance", true),
      mix         (p.apvts, "mix",         "Mix"),
      trim        (p.apvts, "trim",        "Trim", true),
      recover     (p.apvts, "recover",     "Freilegen"),
      brighten    (p.apvts, "brighten",    "Hell/Dunkel", true),
      contour     (p.apvts, "contour",     "Kontur", true),
      mode    (p.apvts, "mode",    { "Soft", "Hard" }),
      stereo  (p.apvts, "stereo",  { "L/R", "M/S" }),
      quality (p.apvts, "quality", { "Normal", "Hoch", "Live" })
{
    setLookAndFeel (&laf);

    for (juce::Component* c : std::initializer_list<juce::Component*> { &spectrum, &depth, &sharpness, &selectivity, &attack, &release,
                                                                        &link, &balance, &mix, &trim, &mode, &stereo, &quality,
                                                                        &recover, &brighten, &contour, &autoGain,
                                                                        &delta, &sidechain, &presetBox, &prevButton, &nextButton })
        addAndMakeVisible (c);

    autoGain.setButtonText (u8 ("Lautheit halten"));
    for (auto* b : { &delta, &sidechain, &autoGain })
    {
        b->setClickingTogglesState (true);
        b->getProperties().set ("chip", true);
    }
    deltaAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "delta", delta);
    scAtt    = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "sidechain", sidechain);
    agAtt    = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "autogain", autoGain);
    delta.setTooltip (u8 ("Nur das hören, was RelaXXer entfernt"));

    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setSelectedId (proc.getCurrentProgram() + 1, juce::dontSendNotification);
    presetBox.onChange = [this] { proc.setCurrentProgram (presetBox.getSelectedId() - 1); };
    prevButton.onClick = [this] { stepPreset (-1); };
    nextButton.onClick = [this] { stepPreset (+1); };

    // Körnung für die Verlaufsfläche (wie .gt-grain im Design-System)
    grain = juce::Image (juce::Image::ARGB, 256, 256, true);
    {
        juce::Random rnd (2026);
        juce::Image::BitmapData bd (grain, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x)
            {
                const auto v = (juce::uint8) rnd.nextInt (256);
                bd.setPixelColour (x, y, juce::Colour (v, v, v, (juce::uint8) 30));
            }
    }

    setSize (1210, 610);
    startTimerHz (30);
}

RelaxxerEditor::~RelaxxerEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void RelaxxerEditor::stepPreset (int delta_)
{
    const int n = proc.getNumPrograms();
    const int idx = (presetBox.getSelectedId() - 1 + delta_ + n) % n;
    presetBox.setSelectedId (idx + 1, juce::sendNotificationSync);
}

void RelaxxerEditor::timerCallback()
{
    spectrum.refresh();
}

void RelaxxerEditor::resized()
{
    auto r = getLocalBounds().reduced (20);
    appbar = r.removeFromTop (60);
    {
        auto a = appbar.reduced (12, 10);
        nextButton.setBounds (a.removeFromRight (40));
        a.removeFromRight (6);
        presetBox.setBounds (a.removeFromRight (290));
        a.removeFromRight (6);
        prevButton.setBounds (a.removeFromRight (40));
    }
    r.removeFromTop (16);
    r.removeFromBottom (22); // Fußzeile
    auto bottom = r.removeFromBottom (200);
    r.removeFromBottom (16);
    spectrum.setBounds (r);

    const int gap = 16;
    const int wCore = 290, wTone = 260, wTime = 200, wStereo = 180;
    panelCore   = bottom.removeFromLeft (wCore);   bottom.removeFromLeft (gap);
    panelTone   = bottom.removeFromLeft (wTone);   bottom.removeFromLeft (gap);
    panelTime   = bottom.removeFromLeft (wTime);   bottom.removeFromLeft (gap);
    panelStereo = bottom.removeFromLeft (wStereo); bottom.removeFromLeft (gap);
    panelOut    = bottom;

    auto inner = [] (juce::Rectangle<int> pnl) { auto i = pnl.reduced (14, 12); i.removeFromTop (26); return i; };

    { // Kern: große Tiefe + Schärfe/Selektivität + Soft/Hard
        auto i = inner (panelCore);
        mode.setBounds (i.removeFromBottom (32));
        i.removeFromBottom (8);
        depth.setBounds (i.removeFromLeft (120));
        auto rest = i;
        sharpness.setBounds (rest.removeFromLeft (rest.getWidth() / 2));
        selectivity.setBounds (rest);
    }
    { // Klang (inspiriert von Gullfoss)
        auto i = inner (panelTone);
        autoGain.setBounds (i.removeFromBottom (32));
        i.removeFromBottom (8);
        const int w3 = i.getWidth() / 3;
        recover.setBounds (i.removeFromLeft (w3));
        brighten.setBounds (i.removeFromLeft (w3));
        contour.setBounds (i);
    }
    { // Tempo
        auto i = inner (panelTime);
        quality.setBounds (i.removeFromBottom (32));
        i.removeFromBottom (8);
        attack.setBounds (i.removeFromLeft (i.getWidth() / 2));
        release.setBounds (i);
    }
    { // Stereo
        auto i = inner (panelStereo);
        stereo.setBounds (i.removeFromBottom (32));
        i.removeFromBottom (8);
        link.setBounds (i.removeFromLeft (i.getWidth() / 2));
        balance.setBounds (i);
    }
    { // Ausgang
        auto i = inner (panelOut);
        auto row = i.removeFromBottom (32);
        const int bw = (row.getWidth() - 6) / 2;
        delta.setBounds (row.removeFromLeft (bw)); row.removeFromLeft (6);
        sidechain.setBounds (row);
        i.removeFromBottom (8);
        mix.setBounds (i.removeFromLeft (i.getWidth() / 2));
        trim.setBounds (i);
    }
}

void RelaxxerEditor::paint (juce::Graphics& g)
{
    g.fillAll (C::bg);

    // App-Leiste: Marken-Verlauf mit Körnung, Text nur auf hellen Plaketten
    {
        const auto a = appbar.toFloat();
        g.setGradientFill (soy::brandGradient (a));
        g.fillRoundedRectangle (a, 20.0f);
        g.saveState();
        juce::Path clip; clip.addRoundedRectangle (a, 20.0f);
        g.reduceClipRegion (clip);
        g.setTiledImageFill (grain, 0, 0, 1.0f);
        g.fillRect (a);
        g.restoreState();

        auto plate = juce::Rectangle<float> (a.getX() + 12, a.getY() + 10, 170, a.getHeight() - 20);
        g.setColour (C::panel.withAlpha (0.92f));
        g.fillRoundedRectangle (plate, plate.getHeight() * 0.5f);
        g.setColour (C::ink);
        g.setFont (soy::Fonts::display (26.0f));
        g.drawText ("RelaXXer", plate, juce::Justification::centred);

        auto tag = juce::Rectangle<float> (plate.getRight() + 10, plate.getY(), 210, plate.getHeight());
        g.setColour (C::panel.withAlpha (0.92f));
        g.fillRoundedRectangle (tag, tag.getHeight() * 0.5f);
        g.setColour (C::inkMuted);
        g.setFont (soy::Fonts::body (14.0f));
        g.drawText (u8 ("Resonanzen entspannen"), tag, juce::Justification::centred);
    }

    // Panels
    auto panel = [&] (juce::Rectangle<int> r, const juce::String& title)
    {
        const auto f = r.toFloat();
        g.setColour (juce::Colour (0x12000000));
        g.fillRoundedRectangle (f.translated (0, 3), 20.0f);
        g.setColour (C::panel);
        g.fillRoundedRectangle (f, 20.0f);
        g.setColour (C::line);
        g.drawRoundedRectangle (f.reduced (0.5f), 20.0f, 1.0f);
        g.setColour (C::ink);
        g.setFont (soy::Fonts::display (17.0f));
        g.drawText (title, r.reduced (16, 10).removeFromTop (22), juce::Justification::centredLeft);
    };
    panel (panelCore, "Kern");
    panel (panelTone, "Klang");
    panel (panelTime, "Tempo");
    panel (panelStereo, "Stereo");
    panel (panelOut, "Ausgang");

    // Hersteller unten rechts
    g.setColour (C::inkMuted);
    g.setFont (soy::Fonts::body (13.0f));
    g.drawText ("Philip Jin Plugins", getLocalBounds().reduced (24, 6).removeFromBottom (18), juce::Justification::centredRight);
}
