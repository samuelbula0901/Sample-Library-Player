#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class PianoSamplerAudioProcessorEditor : public juce::AudioProcessorEditor,
                                         private juce::Timer
{
public:
    explicit PianoSamplerAudioProcessorEditor (PianoSamplerAudioProcessor&);
    ~PianoSamplerAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void browse();

    PianoSamplerAudioProcessor& proc;

    juce::TextButton browseButton { "Browse Library..." };
    juce::Label titleLabel, folderLabel, statusLabel;
    juce::Label volumeLabel { {}, "Volume" }, releaseLabel { {}, "Release" };
    juce::Slider volumeSlider, releaseSlider;
    juce::AudioProcessorValueTreeState::SliderAttachment volumeAttachment, releaseAttachment;
    juce::MidiKeyboardComponent keyboard;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoSamplerAudioProcessorEditor)
};
