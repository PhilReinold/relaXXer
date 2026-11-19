#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "RelaxxerDSP.h"

class RelaxxerProcessor : public juce::AudioProcessor, private juce::AsyncUpdater
{
public:
    RelaxxerProcessor();
    ~RelaxxerProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "RelaXXer"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.03; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Aktuelle Parameter als DSP-Struktur (auch fürs GUI: Fokus-Kurve)
    relaxxer::Params readParams() const;

    juce::AudioProcessorValueTreeState apvts;
    // zwei Engines: Normal/Hoch (~21 ms) und Live (~5 ms); GUI liest die aktive (lock-freie Anzeige-Werte)
    const relaxxer::Processor& display() const { return liveActive.load() ? dspLive : dsp; }
    std::atomic<bool> sidechainConnected { false };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    std::atomic<float>* raw (const char* id) const { return apvts.getRawParameterValue (id); }
    void handleAsyncUpdate() override; // Latenz an den Host melden (Message-Thread)
    int currentProgram = 0;
    relaxxer::Processor dsp, dspLive;
    std::atomic<bool> liveActive { false };
    bool lastLive = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RelaxxerProcessor)
};
