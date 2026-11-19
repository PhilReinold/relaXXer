#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "RelaxxerDSP.h"

class RelaxxerProcessor : public juce::AudioProcessor
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
    relaxxer::Processor dsp; // GUI liest nur die lock-freien Anzeige-Werte
    std::atomic<bool> sidechainConnected { false };

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    std::atomic<float>* raw (const char* id) const { return apvts.getRawParameterValue (id); }
    int currentProgram = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RelaxxerProcessor)
};
