#include "PluginProcessor.h"
#include "PluginEditor.h"

using Float  = juce::AudioParameterFloat;
using Choice = juce::AudioParameterChoice;
using Bool   = juce::AudioParameterBool;
using Range  = juce::NormalisableRange<float>;
using Attr   = juce::AudioParameterFloatAttributes;

namespace
{
Range freqRange (float lo, float hi, float centre)
{
    Range r (lo, hi);
    r.setSkewForCentre (centre);
    return r;
}

Attr hzAttr()
{
    return Attr().withLabel ("Hz").withStringFromValueFunction ([] (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " k" : juce::String (juce::roundToInt (v));
    });
}
}

juce::AudioProcessorValueTreeState::ParameterLayout RelaxxerProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto one = [] (float v, int) { return juce::String (v, 1); };
    l.add (std::make_unique<Float>  (juce::ParameterID { "depth", 1 },       "Depth",       Range (0.0f, 20.0f, 0.01f), 5.0f, Attr().withStringFromValueFunction (one)));
    l.add (std::make_unique<Float>  (juce::ParameterID { "sharpness", 1 },   "Sharpness",   Range (0.0f, 10.0f, 0.01f), 4.0f, Attr().withStringFromValueFunction (one)));
    l.add (std::make_unique<Float>  (juce::ParameterID { "selectivity", 1 }, "Selectivity", Range (0.0f, 10.0f, 0.01f), 3.5f, Attr().withStringFromValueFunction (one)));
    l.add (std::make_unique<Float>  (juce::ParameterID { "attack", 1 },      "Attack",      Range (0.5f, 100.0f, 0.01f, 0.4f), 5.0f,  Attr().withLabel ("ms").withStringFromValueFunction (one)));
    l.add (std::make_unique<Float>  (juce::ParameterID { "release", 1 },     "Release",     Range (5.0f, 1000.0f, 0.1f, 0.4f), 60.0f, Attr().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)); })));
    l.add (std::make_unique<Choice> (juce::ParameterID { "mode", 1 },        "Mode",        juce::StringArray { "Soft", "Hard" }, 0));
    l.add (std::make_unique<Choice> (juce::ParameterID { "stereo", 1 },      "Stereo Mode", juce::StringArray { "L/R", "M/S" }, 0));
    l.add (std::make_unique<Float>  (juce::ParameterID { "link", 1 },        "Link",        Range (0.0f, 100.0f, 0.1f), 100.0f, Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)); })));
    l.add (std::make_unique<Float>  (juce::ParameterID { "balance", 1 },     "Balance",     Range (-100.0f, 100.0f, 0.1f), 0.0f, Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)); })));
    l.add (std::make_unique<Float>  (juce::ParameterID { "mix", 1 },         "Mix",         Range (0.0f, 100.0f, 0.1f), 100.0f, Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)); })));
    l.add (std::make_unique<Float>  (juce::ParameterID { "trim", 1 },        "Trim",        Range (-12.0f, 12.0f, 0.01f), 0.0f, Attr().withLabel ("dB").withStringFromValueFunction (one)));
    l.add (std::make_unique<Bool>   (juce::ParameterID { "delta", 1 },       "Delta",       false));
    l.add (std::make_unique<Bool>   (juce::ParameterID { "sidechain", 1 },   "Sidechain",   false));
    l.add (std::make_unique<Choice> (juce::ParameterID { "quality", 1 },     "Quality",     juce::StringArray { "Normal", "High", "Live" }, 0));
    // inspiriert von Gullfoss
    l.add (std::make_unique<Float>  (juce::ParameterID { "recover", 1 },     "Recover",     Range (0.0f, 100.0f, 0.1f), 0.0f, Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)); })));
    l.add (std::make_unique<Float>  (juce::ParameterID { "brighten", 1 },    "Brighten",    Range (-100.0f, 100.0f, 0.1f), 0.0f, Attr().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v)); })));
    l.add (std::make_unique<Float>  (juce::ParameterID { "contour", 1 },     "Contour",     Range (-10.0f, 10.0f, 0.01f), 0.0f, Attr().withLabel ("dB").withStringFromValueFunction (one)));
    l.add (std::make_unique<Bool>   (juce::ParameterID { "autogain", 1 },    "Keep Loudness", false));
    l.add (std::make_unique<Float>  (juce::ParameterID { "lowcut", 1 },      "Low Cut",     freqRange (20.0f, 2000.0f, 150.0f), 40.0f, hzAttr()));
    l.add (std::make_unique<Float>  (juce::ParameterID { "highcut", 1 },     "High Cut",    freqRange (1000.0f, 20000.0f, 6000.0f), 20000.0f, hzAttr()));
    l.add (std::make_unique<Float>  (juce::ParameterID { "b1freq", 1 },      "Band 1 Freq", freqRange (20.0f, 20000.0f, 1000.0f), 300.0f, hzAttr()));
    l.add (std::make_unique<Float>  (juce::ParameterID { "b1sens", 1 },      "Band 1 Sens", Range (-12.0f, 12.0f, 0.01f), 0.0f, Attr().withLabel ("dB").withStringFromValueFunction (one)));
    l.add (std::make_unique<Float>  (juce::ParameterID { "b1q", 1 },         "Band 1 Q",    Range (0.3f, 6.0f, 0.01f, 0.5f), 1.0f, Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); })));
    l.add (std::make_unique<Float>  (juce::ParameterID { "b2freq", 1 },      "Band 2 Freq", freqRange (20.0f, 20000.0f, 1000.0f), 4000.0f, hzAttr()));
    l.add (std::make_unique<Float>  (juce::ParameterID { "b2sens", 1 },      "Band 2 Sens", Range (-12.0f, 12.0f, 0.01f), 0.0f, Attr().withLabel ("dB").withStringFromValueFunction (one)));
    l.add (std::make_unique<Float>  (juce::ParameterID { "b2q", 1 },         "Band 2 Q",    Range (0.3f, 6.0f, 0.01f, 0.5f), 1.0f, Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); })));
    return l;
}

namespace
{
struct Preset
{
    const char* name;
    float depth, sharp, sel, att, rel; int mode; int stereo; float link, mix;
    float lc, hc, b1f, b1s, b1q, b2f, b2s, b2q;
    float recover = 0.0f, brighten = 0.0f, contour = 0.0f; bool autogain = false;
    bool delta = false; int quality = 0; float balance = 0.0f;
};

// Startpunkte – danach Tiefe nach Gehör einstellen (hochdrehen bis es zu viel ist, dann etwas zurück)
const Preset presets[] =
{
//    Name                              Tiefe Schärfe Sel  Att  Rel  Mode St  Link  Mix   LC    HC     B1f   B1s  B1q   B2f    B2s  B2q
    { "Standard",                        5.0f, 4.0f, 3.5f,  5.0f,  60.0f, 0, 0, 100, 100,   40, 20000,  300,  0.0f, 1.0f,  4000,  0.0f, 1.0f },
    { "Vocal – Härte weg",              6.0f, 4.5f, 4.0f,  4.0f,  70.0f, 0, 0, 100, 100,  150, 16000, 2500,  4.0f, 1.0f,  5000,  2.0f, 0.8f },
    { "Vocal – De-Esser",                8.0f, 3.0f, 2.5f,  1.5f,  40.0f, 0, 0, 100, 100, 3500, 16000, 6500,  6.0f, 1.2f, 10000,  2.0f, 1.0f },
    { "Vocal – nasal / Mitten",          5.0f, 6.0f, 5.0f,  8.0f,  90.0f, 0, 0, 100, 100,  200,  6000, 1000,  5.0f, 1.2f,  2000,  0.0f, 1.0f },
    { "Akustikgitarre – glätten",       5.0f, 5.0f, 4.5f,  6.0f,  80.0f, 0, 1, 100, 100,  100, 14000,  250,  2.0f, 1.0f,  3500,  2.0f, 1.0f },
    { "E-Gitarre – harsch (Hard)",       4.0f, 7.0f, 5.5f,  5.0f,  70.0f, 1, 0, 100, 100,  150, 12000, 2800,  4.0f, 1.0f,  5500,  2.0f, 1.0f },
    { "Bass – Dröhnen zähmen",         6.0f, 6.5f, 5.0f, 15.0f, 150.0f, 0, 0, 100, 100,   30,   800,  120,  3.0f, 1.0f,   300,  0.0f, 1.0f },
    { "Drums – Overheads",               5.0f, 8.0f, 6.0f,  1.0f,  50.0f, 0, 1, 100, 100,  400, 18000, 4000,  2.0f, 0.8f,  9000,  2.0f, 1.0f },
    { "Snare – Klingeln",                7.0f, 9.0f, 7.0f,  2.0f, 120.0f, 1, 0, 100, 100,  150,  3000,  400,  4.0f, 1.5f,   900,  2.0f, 1.5f },
    { "Synth / Lead zähmen",            6.0f, 6.0f, 4.0f,  3.0f,  60.0f, 0, 1,  80, 100,  200, 18000, 3000,  3.0f, 0.8f,  7000,  2.0f, 1.0f },
    { "Mix-Bus – sanft",                 2.5f, 3.0f, 4.0f, 10.0f, 120.0f, 0, 1, 100,  80,   30, 20000,  250,  0.0f, 1.0f,  3500,  1.0f, 0.7f, 15.0f, 0.0f, 0.0f, true },
    { "Mix-Bus – Klarheit (freilegen)",  3.0f, 3.0f, 4.0f,  8.0f, 120.0f, 0, 1, 100, 100,   30, 20000,  250,  0.0f, 1.0f,  3500,  0.0f, 0.7f, 45.0f, 10.0f, 0.0f, true },
    { "Mastering – hauchzart",           1.5f, 2.5f, 5.0f, 15.0f, 200.0f, 0, 1, 100,  70,   30, 20000,  250,  0.0f, 1.0f,  4000,  0.0f, 1.0f, 10.0f, 0.0f, 0.0f, true },
    { "Mastering – leise hören (Kontur)", 1.5f, 2.5f, 5.0f, 15.0f, 200.0f, 0, 1, 100, 100,   30, 20000,  250,  0.0f, 1.0f,  4000,  0.0f, 1.0f, 10.0f, 0.0f, 3.0f, true },
    { "Vocal – live (geringe Latenz)",   5.0f, 4.5f, 4.0f,  3.0f,  60.0f, 0, 0, 100, 100,  150, 16000, 2500,  3.0f, 1.0f,  5000,  2.0f, 0.8f },
    // ---- Busse ----
    { "Drum-Bus – Härte raus, Punch bleibt", 4.0f, 5.0f, 4.5f,  6.0f,  60.0f, 0, 1, 100,  85,   60, 18000,  400,  2.0f, 1.0f,  4500,  2.0f, 0.8f,  0.0f, 0.0f, 0.0f, true, false, 1 },
    { "Gitarren-Bus – Akustik",            4.0f, 4.0f, 4.0f,  8.0f, 100.0f, 0, 1, 100,  90,  100, 15000,  250,  2.0f, 1.0f,  3500,  2.0f, 1.0f, 10.0f, 0.0f, 0.0f, true },
    { "Gitarren-Bus – E-Gitarren",         4.5f, 6.0f, 5.0f,  5.0f,  80.0f, 0, 1, 100, 100,  120, 12000,  200,  3.0f, 1.0f,  3000,  3.0f, 0.8f,  0.0f, 0.0f, 0.0f, true },
    { "Background-Vocals-Bus",             6.0f, 4.0f, 3.5f,  4.0f,  80.0f, 0, 1,  80, 100,  150, 16000, 3000,  3.0f, 1.0f,  7000,  3.0f, 1.0f,  0.0f, -10.0f, 0.0f, true },
    { "Vocal-Bus – zusammenführen",        4.5f, 4.5f, 4.0f,  4.0f,  80.0f, 0, 1, 100, 100,  120, 16000, 2800,  2.0f, 1.0f,  6500,  2.0f, 1.0f, 20.0f, 0.0f, 0.0f, true },
    // ---- Kreativ / extrem (zum Ausprobieren, greifen massiv ein) ----
    { "Kreativ – Geisterstimme (nur Resonanzen)", 18.0f, 8.0f, 2.0f, 2.0f,  80.0f, 0, 0, 100, 100,   80, 18000, 1000,  0.0f, 1.0f,  4000,  0.0f, 1.0f,  0.0f, 0.0f, 0.0f, false, true },
    { "Kreativ – Flüstermaschine",         20.0f, 2.0f, 0.0f,  1.0f,  20.0f, 0, 0, 100, 100,   60, 20000,  500,  6.0f, 0.5f,  3000,  6.0f, 0.5f,  0.0f, 0.0f, 0.0f, true },
    { "Kreativ – Spektral-Zerstörer",      20.0f, 10.0f, 0.0f, 0.5f,   5.0f, 1, 0, 100, 100,   20, 20000,  300,  0.0f, 1.0f,  3000,  0.0f, 1.0f,  0.0f, 0.0f, 0.0f, true },
    { "Kreativ – Live-Roboter",            16.0f, 9.0f, 1.0f,  0.5f,  10.0f, 1, 0, 100, 100,   80, 16000, 1000,  4.0f, 0.6f,  2500,  0.0f, 1.0f,  0.0f, 0.0f, 0.0f, true, false, 2 },
    { "Kreativ – Stereo-Nebel",            15.0f, 3.0f, 1.0f,  5.0f, 400.0f, 0, 1,   0, 100,   80, 20000,  800,  0.0f, 1.0f,  5000,  4.0f, 0.7f,  0.0f, 0.0f, 0.0f, true, false, 0, 100.0f },
    { "Sidechain – Platz für Gesang",   6.0f, 3.0f, 2.0f,  5.0f, 100.0f, 0, 1, 100, 100,  200,  8000, 1500,  3.0f, 0.7f,  3500,  2.0f, 0.8f },
};
constexpr int numPresets = (int) (sizeof (presets) / sizeof (presets[0]));
}

int RelaxxerProcessor::getNumPrograms() { return numPresets; }

const juce::String RelaxxerProcessor::getProgramName (int i)
{
    return juce::isPositiveAndBelow (i, numPresets) ? juce::String (juce::CharPointer_UTF8 (presets[i].name)) : juce::String();
}

void RelaxxerProcessor::setCurrentProgram (int i)
{
    if (! juce::isPositiveAndBelow (i, numPresets)) return;
    currentProgram = i;
    apvts.state.setProperty ("preset", i, nullptr);

    const auto& p = presets[i];
    auto set = [this] (const char* id, float v)
    {
        if (auto* prm = apvts.getParameter (id))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 (v));
            prm->endChangeGesture();
        }
    };
    set ("depth", p.depth); set ("sharpness", p.sharp); set ("selectivity", p.sel);
    set ("attack", p.att);  set ("release", p.rel);
    set ("mode", (float) p.mode); set ("stereo", (float) p.stereo); set ("link", p.link); set ("balance", p.balance);
    set ("mix", p.mix); set ("trim", 0.0f); set ("delta", p.delta ? 1.0f : 0.0f);
    set ("sidechain", juce::String (juce::CharPointer_UTF8 (p.name)).startsWith ("Sidechain") ? 1.0f : 0.0f);
    set ("quality", juce::String (juce::CharPointer_UTF8 (p.name)).contains ("live") ? 2.0f : (float) p.quality);
    set ("lowcut", p.lc); set ("highcut", p.hc);
    set ("b1freq", p.b1f); set ("b1sens", p.b1s); set ("b1q", p.b1q);
    set ("b2freq", p.b2f); set ("b2sens", p.b2s); set ("b2q", p.b2q);
    set ("recover", p.recover); set ("brighten", p.brighten); set ("contour", p.contour); set ("autogain", p.autogain ? 1.0f : 0.0f);
}

RelaxxerProcessor::RelaxxerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

bool RelaxxerProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (in != out || (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()))
        return false;
    const auto sc = l.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::mono() || sc == juce::AudioChannelSet::stereo();
}

void RelaxxerProcessor::prepareToPlay (double sampleRate, int)
{
    const int ch = juce::jmax (1, getMainBusNumInputChannels());
    dsp.prepare (sampleRate, ch, false);
    dspLive.prepare (sampleRate, ch, true);
    lastLive = raw ("quality")->load() > 1.5f;
    liveActive.store (lastLive);
    setLatencySamples ((lastLive ? dspLive : dsp).getLatencySamples());
}

relaxxer::Params RelaxxerProcessor::readParams() const
{
    relaxxer::Params p;
    p.depth        = raw ("depth")->load();
    p.sharpness    = raw ("sharpness")->load();
    p.selectivity  = raw ("selectivity")->load();
    p.attackMs     = raw ("attack")->load();
    p.releaseMs    = raw ("release")->load();
    p.hardMode     = raw ("mode")->load() > 0.5f;
    p.midSide      = raw ("stereo")->load() > 0.5f;
    p.link         = raw ("link")->load() * 0.01f;
    p.balance      = raw ("balance")->load() * 0.01f;
    p.mix          = raw ("mix")->load() * 0.01f;
    p.trimDb       = raw ("trim")->load();
    p.delta        = raw ("delta")->load() > 0.5f;
    p.useSidechain = raw ("sidechain")->load() > 0.5f;
    p.highQuality  = raw ("quality")->load() > 0.5f && raw ("quality")->load() < 1.5f;
    p.recover      = raw ("recover")->load() * 0.01f;
    p.brighten     = raw ("brighten")->load() * 0.01f;
    p.contourDb    = raw ("contour")->load();
    p.autoGain     = raw ("autogain")->load() > 0.5f;
    p.lowCutHz     = raw ("lowcut")->load();
    p.highCutHz    = raw ("highcut")->load();
    p.band1Hz = raw ("b1freq")->load(); p.band1Db = raw ("b1sens")->load(); p.band1Q = raw ("b1q")->load();
    p.band2Hz = raw ("b2freq")->load(); p.band2Db = raw ("b2sens")->load(); p.band2Q = raw ("b2q")->load();
    return p;
}

void RelaxxerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, numSamples);

    auto p = readParams();

    const float* scPtrs[2] = { nullptr, nullptr };
    int scChannels = 0;
    if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled() && scBus->getNumberOfChannels() > 0)
    {
        auto sc = getBusBuffer (buffer, true, 1);
        scChannels = juce::jmin (2, sc.getNumChannels());
        for (int c = 0; c < scChannels; ++c) scPtrs[c] = sc.getReadPointer (c);
    }
    sidechainConnected.store (scChannels > 0);
    if (scChannels == 0) p.useSidechain = false;

    const bool live = raw ("quality")->load() > 1.5f;
    if (live != lastLive)
    {
        (live ? dspLive : dsp).reset();
        lastLive = live;
        liveActive.store (live);
        triggerAsyncUpdate(); // neue Latenz melden
    }
    auto& engine = live ? dspLive : dsp;

    auto main = getBusBuffer (buffer, false, 0);
    if (main.getNumChannels() >= engine.getNumChannels()) // Sicherheit: nie über die Kanalzahl hinaus schreiben
        engine.process (main.getArrayOfWritePointers(), scChannels > 0 ? scPtrs : nullptr, scChannels, numSamples, p);
}

void RelaxxerProcessor::handleAsyncUpdate()
{
    setLatencySamples ((liveActive.load() ? dspLive : dsp).getLatencySamples());
}

juce::AudioProcessorEditor* RelaxxerProcessor::createEditor() { return new RelaxxerEditor (*this); }

void RelaxxerProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void RelaxxerProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            currentProgram = juce::jlimit (0, numPresets - 1, (int) apvts.state.getProperty ("preset", 0));
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RelaxxerProcessor(); }
