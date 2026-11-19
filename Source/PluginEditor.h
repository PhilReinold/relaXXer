#pragma once
#include "PluginProcessor.h"

// Look im Stil der Marke "Boy made of Soy" (helles Theme): Fredoka / Nunito / DM Mono,
// Verlauf Sonne -> Koralle -> Rosé, weiche Flächen, Pillen-Buttons, Bogen-Regler
namespace soy
{
struct Colours
{
    static inline const juce::Colour bg       { 0xfff7f0fa }; // surface-100
    static inline const juce::Colour panel    { 0xffffffff }; // surface-200
    static inline const juce::Colour soft     { 0xffefe3f5 }; // surface-300
    static inline const juce::Colour line     { 0xffdccbe6 };
    static inline const juce::Colour lineStrong { 0xff8a7aa0 };
    static inline const juce::Colour ink      { 0xff24182f };
    static inline const juce::Colour inkMuted { 0xff5e4d70 };
    static inline const juce::Colour accentInk { 0xff9a4308 };
    static inline const juce::Colour sun      { 0xfff29b38 };
    static inline const juce::Colour coral    { 0xffef5b4a };
    static inline const juce::Colour rose     { 0xffe96bb8 };
    static inline const juce::Colour violet   { 0xff9b3fe4 };
    static inline const juce::Colour sky      { 0xff6f97e8 };
    static inline const juce::Colour mist     { 0xfff1e5f6 };
    static inline const juce::Colour focus    { 0xff2f5fc4 };
};

struct Fonts
{
    static juce::Font display (float h);  // Fredoka SemiBold
    static juce::Font body (float h);     // Nunito SemiBold
    static juce::Font bodyBold (float h); // Nunito Bold
    static juce::Font mono (float h);     // DM Mono Medium
};

juce::ColourGradient brandGradient (juce::Rectangle<float> r);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return Fonts::bodyBold (15.0f); }
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getPopupMenuFont() override { return Fonts::body (15.0f); }
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
};
} // namespace soy

// Bogen-Regler mit Titel oben und Wert (DM Mono) unten
class SoyKnob : public juce::Component
{
public:
    SoyKnob (juce::AudioProcessorValueTreeState&, const juce::String& paramID, const juce::String& title, bool bipolar = false);
    void paint (juce::Graphics&) override;
    void resized() override;
    juce::Slider slider;

private:
    juce::String title;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// Segment-Schalter (z. B. Soft | Hard) für Auswahl-Parameter
class SoySegment : public juce::Component
{
public:
    SoySegment (juce::AudioProcessorValueTreeState&, const juce::String& paramID, juce::StringArray labels);
    void resized() override;

private:
    juce::OwnedArray<juce::TextButton> buttons;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};

// Spektrum, Absenkung und Fokus-Kurve mit ziehbaren Knoten
class SpectrumView : public juce::Component
{
public:
    explicit SpectrumView (RelaxxerProcessor&);
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void refresh();

private:
    struct Node { const char* freqID; const char* sensID; const char* qID; juce::String name; juce::Colour colour; };
    juce::Rectangle<float> plot() const;
    float xOf (float hz) const;
    float hzOf (float x) const;
    float sensY (float db) const;
    juce::Point<float> nodePos (int i) const;
    int nodeAt (juce::Point<float>) const;
    juce::RangedAudioParameter* prm (const char* id) const;
    float val (const char* id) const;
    void setVal (const char* id, float v);

    RelaxxerProcessor& proc;
    std::vector<Node> nodes;
    std::array<float, relaxxer::Processor::displayPoints> level {}, red {};
    float maxRed = 0.0f, inDb = -120.0f, outDb = -120.0f, brightDb = 0.0f, makeupDb = 0.0f;
    int drag = -1, hover = -1;
};

class RelaxxerEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit RelaxxerEditor (RelaxxerProcessor&);
    ~RelaxxerEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void stepPreset (int delta);

    soy::LookAndFeel laf; // zuerst deklariert => zuletzt zerstört
    RelaxxerProcessor& proc;

    SpectrumView spectrum;
    SoyKnob depth, sharpness, selectivity, attack, release, link, balance, mix, trim, recover, brighten, contour;
    SoySegment mode, stereo, quality;
    juce::TextButton delta { "Delta" }, sidechain { "Sidechain" }, autoGain;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> deltaAtt, scAtt, agAtt;

    juce::ComboBox presetBox;
    juce::TextButton prevButton { "<" }, nextButton { ">" };

    juce::Image grain;
    juce::Rectangle<int> appbar, panelCore, panelTone, panelTime, panelStereo, panelOut;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RelaxxerEditor)
};
