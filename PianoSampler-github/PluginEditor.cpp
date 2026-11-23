#include "PluginEditor.h"

PianoSamplerAudioProcessorEditor::PianoSamplerAudioProcessorEditor (PianoSamplerAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p),
      volumeAttachment (p.apvts, "volume", volumeSlider),
      releaseAttachment (p.apvts, "release", releaseSlider),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    titleLabel.setText ("Piano Sampler", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (titleLabel);

    browseButton.onClick = [this] { browse(); };
    addAndMakeVisible (browseButton);

    folderLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    folderLabel.setMinimumHorizontalScale (0.7f);
    addAndMakeVisible (folderLabel);

    statusLabel.setColour (juce::Label::textColourId, juce::Colour (0xff9ad1ff));
    addAndMakeVisible (statusLabel);

    for (auto* s : { &volumeSlider, &releaseSlider })
    {
        s->setSliderStyle (juce::Slider::LinearHorizontal);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
        addAndMakeVisible (s);
    }
    volumeSlider.setTextValueSuffix (" dB");
    releaseSlider.setTextValueSuffix (" s");

    for (auto* l : { &volumeLabel, &releaseLabel })
    {
        l->setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (l);
    }

    keyboard.setAvailableRange (21, 108);
    keyboard.setLowestVisibleKey (21);
    addAndMakeVisible (keyboard);

    setSize (780, 330);
    startTimerHz (8);
    timerCallback();
}

PianoSamplerAudioProcessorEditor::~PianoSamplerAudioProcessorEditor()
{
    stopTimer();
}

void PianoSamplerAudioProcessorEditor::browse()
{
    auto start = proc.getLibraryFolder();
    if (! start.isDirectory())
        start = juce::File::getSpecialLocation (juce::File::userMusicDirectory);

    chooser = std::make_unique<juce::FileChooser> ("Select your piano sample folder", start);

    juce::Component::SafePointer<PianoSamplerAudioProcessorEditor> safe (this);

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories,
                          [safe] (const juce::FileChooser& fc)
                          {
                              const auto result = fc.getResult();
                              if (safe != nullptr && result.isDirectory())
                                  safe->proc.loadLibrary (result);
                          });
}

void PianoSamplerAudioProcessorEditor::timerCallback()
{
    const auto folder = proc.getLibraryFolder();
    folderLabel.setText (folder.getFullPathName().isEmpty() ? "No folder selected"
                                                             : folder.getFullPathName(),
                         juce::dontSendNotification);
    statusLabel.setText (proc.getStatus(), juce::dontSendNotification);
}

void PianoSamplerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1d22));
    g.setColour (juce::Colour (0xff2a2d35));
    g.fillRect (0, 0, getWidth(), 52);
}

void PianoSamplerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (52).reduced (16, 10);
    titleLabel.setBounds (header);

    area.reduce (16, 10);

    auto row = area.removeFromTop (30);
    browseButton.setBounds (row.removeFromLeft (150));
    row.removeFromLeft (10);
    folderLabel.setBounds (row);

    area.removeFromTop (6);
    statusLabel.setBounds (area.removeFromTop (24));

    area.removeFromTop (10);
    auto r1 = area.removeFromTop (28);
    volumeLabel.setBounds (r1.removeFromLeft (70));
    volumeSlider.setBounds (r1.removeFromLeft (360));

    area.removeFromTop (6);
    auto r2 = area.removeFromTop (28);
    releaseLabel.setBounds (r2.removeFromLeft (70));
    releaseSlider.setBounds (r2.removeFromLeft (360));

    area.removeFromTop (10);
    keyboard.setBounds (area);
    keyboard.setKeyWidth ((float) area.getWidth() / 52.0f);
}
