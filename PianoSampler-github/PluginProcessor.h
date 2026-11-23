#pragma once
#include <JuceHeader.h>
#include "SamplerEngine.h"

class PianoSamplerAudioProcessor : public juce::AudioProcessor
{
public:
    PianoSamplerAudioProcessor();
    ~PianoSamplerAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- Library handling (call from the message thread) ---
    void loadLibrary (const juce::File& folder);
    juce::File getLibraryFolder() const;
    juce::String getStatus() const;

    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboardState;

private:
    class LoaderThread;
    void runLoad (juce::Thread& thread, const juce::File& folder);
    void setStatus (const juce::String& s);
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    PianoSynth synth;
    juce::AudioFormatManager formatManager;
    std::unique_ptr<LoaderThread> loader;

    std::atomic<float>* volumeParam  = nullptr;
    std::atomic<float>* releaseParam = nullptr;

    mutable juce::CriticalSection statusLock;
    juce::String status { "No library loaded. Click \"Browse Library...\"." };
    juce::String loadedPath;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoSamplerAudioProcessor)
};
