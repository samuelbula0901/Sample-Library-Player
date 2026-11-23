#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <map>
#include <vector>

//==============================================================================
// Filename -> MIDI note parsing
//
// Recognises tokens such as  C4  C#4  Db3  A0  inside names like
//   "Piano_C4_v2.wav", "Steinway-F#3-soft.wav", "A0 mf.wav"
// If no note name is found, a bare number 21..108 is treated as a MIDI note
// ("piano_060_v1.wav").
//
// Octave convention: C4 = MIDI 60. If your files treat C3 as middle C,
// set kOctaveOffset to 1.
namespace
{
    constexpr int kOctaveOffset = 0;

    int parseNoteToken (const juce::String& tok)
    {
        if (tok.length() < 2 || tok.length() > 4)
            return -1;

        int base = 0;
        switch (juce::CharacterFunctions::toUpperCase (tok[0]))
        {
            case 'C': base = 0;  break;
            case 'D': base = 2;  break;
            case 'E': base = 4;  break;
            case 'F': base = 5;  break;
            case 'G': base = 7;  break;
            case 'A': base = 9;  break;
            case 'B': base = 11; break;
            default:  return -1;
        }

        int i = 1, accidental = 0;
        if (tok[i] == '#')      { accidental = 1;  ++i; }
        else if (tok[i] == 'b') { accidental = -1; ++i; }

        const auto octaveStr = tok.substring (i);
        if (octaveStr.length() != 1 || ! octaveStr.containsOnly ("0123456789"))
            return -1;

        const int midi = (octaveStr.getIntValue() + 1 + kOctaveOffset) * 12 + base + accidental;
        return (midi >= 0 && midi <= 127) ? midi : -1;
    }

    // Library naming scheme:  <midiNote>-b<bank>-l<layer>.wav   e.g. 021-b0-l00.wav
    // note = MIDI note (21 = A0), bank = b0/b1..., layer = velocity layer (l00 = softest).
    struct LibName { int note = -1, bank = 0, layer = 0; };

    LibName parseLibraryName (const juce::File& f)
    {
        LibName r;
        const auto t = juce::StringArray::fromTokens (f.getFileNameWithoutExtension(), "-_ ", "");
        if (t.size() < 3) return r;

        // Look at the last three tokens so a prefix (e.g. "a01906ac-021-b0-l00") is fine.
        const auto n = t[t.size() - 3], b = t[t.size() - 2], l = t[t.size() - 1];

        if (n.isNotEmpty() && n.containsOnly ("0123456789")
            && b.length() >= 2 && juce::CharacterFunctions::toLowerCase (b[0]) == 'b'
            && b.substring (1).containsOnly ("0123456789")
            && l.length() >= 2 && juce::CharacterFunctions::toLowerCase (l[0]) == 'l'
            && l.substring (1).containsOnly ("0123456789"))
        {
            const int note = n.getIntValue();
            if (note >= 0 && note <= 127)
            {
                r.note  = note;
                r.bank  = b.substring (1).getIntValue();
                r.layer = l.substring (1).getIntValue();
            }
        }
        return r;
    }

    int parseNoteFromFilename (const juce::File& f)
    {
        const auto tokens = juce::StringArray::fromTokens (f.getFileNameWithoutExtension(), " _-.()[]", "");

        for (auto& t : tokens)
        {
            const int n = parseNoteToken (t);
            if (n >= 0)
                return n;
        }

        for (auto& t : tokens)
        {
            if (t.isNotEmpty() && t.length() <= 3 && t.containsOnly ("0123456789"))
            {
                const int v = t.getIntValue();
                if (v >= 21 && v <= 108)
                    return v;
            }
        }

        return -1;
    }

    struct NaturalCompare
    {
        static int compareElements (const juce::File& a, const juce::File& b)
        {
            return a.getFullPathName().compareNatural (b.getFullPathName());
        }
    };
}

//==============================================================================
class PianoSamplerAudioProcessor::LoaderThread : public juce::Thread
{
public:
    explicit LoaderThread (PianoSamplerAudioProcessor& o)
        : juce::Thread ("Piano Sample Loader"), owner (o) {}

    void run() override { owner.runLoad (*this, folder); }

    juce::File folder;

private:
    PianoSamplerAudioProcessor& owner;
};

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout PianoSamplerAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "volume", 1 }, "Volume",
        juce::NormalisableRange<float> (-24.0f, 6.0f, 0.1f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel ("dB")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "release", 1 }, "Release",
        juce::NormalisableRange<float> (0.05f, 5.0f, 0.01f, 0.4f), 0.4f,
        juce::AudioParameterFloatAttributes().withLabel ("s")));

    return layout;
}

PianoSamplerAudioProcessor::PianoSamplerAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PianoSamplerState", createLayout())
{
    formatManager.registerBasicFormats();

    volumeParam  = apvts.getRawParameterValue ("volume");
    releaseParam = apvts.getRawParameterValue ("release");

    for (int i = 0; i < 64; ++i)
        synth.addVoice (new PianoVoice (releaseParam));

    loader = std::make_unique<LoaderThread> (*this);
}

PianoSamplerAudioProcessor::~PianoSamplerAudioProcessor()
{
    loader->stopThread (10000);
}

void PianoSamplerAudioProcessor::prepareToPlay (double sampleRate, int)
{
    synth.setCurrentPlaybackSampleRate (sampleRate);
    keyboardState.reset();
}

bool PianoSamplerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void PianoSamplerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);
    synth.renderNextBlock (buffer, midi, 0, buffer.getNumSamples());

    buffer.applyGain (juce::Decibels::decibelsToGain (volumeParam->load()));
}

juce::AudioProcessorEditor* PianoSamplerAudioProcessor::createEditor()
{
    return new PianoSamplerAudioProcessorEditor (*this);
}

//==============================================================================
void PianoSamplerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PianoSamplerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));

            const auto folder = getLibraryFolder();
            if (folder.isDirectory())
                loadLibrary (folder);
        }
    }
}

//==============================================================================
juce::File PianoSamplerAudioProcessor::getLibraryFolder() const
{
    const auto path = apvts.state.getProperty ("libraryPath").toString();
    return path.isEmpty() ? juce::File() : juce::File (path);
}

juce::String PianoSamplerAudioProcessor::getStatus() const
{
    const juce::ScopedLock sl (statusLock);
    return status;
}

void PianoSamplerAudioProcessor::setStatus (const juce::String& s)
{
    const juce::ScopedLock sl (statusLock);
    status = s;
}

void PianoSamplerAudioProcessor::loadLibrary (const juce::File& folder)
{
    const auto path = folder.getFullPathName();

    // Hosts often call setStateInformation repeatedly; don't reload the same library.
    if (path == loadedPath && loader->isThreadRunning())
        return;

    loadedPath = path;
    apvts.state.setProperty ("libraryPath", path, nullptr);

    loader->stopThread (10000);
    loader->folder = folder;
    loader->startThread();
}

void PianoSamplerAudioProcessor::runLoad (juce::Thread& thread, const juce::File& folder)
{
    setStatus ("Scanning " + folder.getFileName() + "...");

    juce::Array<juce::File> files;
    for (auto& f : folder.findChildFiles (juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac"))
        if (! f.getFileName().startsWith ("."))      // skip macOS "._" resource files
            files.add (f);

    if (files.isEmpty())
    {
        setStatus ("No .wav / .aif / .flac files found in that folder.");
        return;
    }

    NaturalCompare cmp;
    files.sort (cmp);

    // Group files by note. Preferred: <note>-b<bank>-l<layer> naming; fallback: note names.
    std::map<int, std::vector<juce::File>> byNote;
    int unparsed = 0, otherBanks = 0;

    int minBank = 1000;
    for (auto& f : files)
    {
        const auto ln = parseLibraryName (f);
        if (ln.note >= 0) minBank = juce::jmin (minBank, ln.bank);
    }

    std::map<int, std::map<int, juce::File>> structured;   // note -> layer -> file
    for (auto& f : files)
    {
        const auto ln = parseLibraryName (f);
        if (ln.note >= 0)
        {
            if (ln.bank == minBank) structured[ln.note][ln.layer] = f;
            else                    ++otherBanks;          // extra banks are ignored for now
            continue;
        }

        const int n = parseNoteFromFilename (f);
        if (n >= 0) byNote[n].push_back (f);
        else        ++unparsed;
    }

    for (auto& noteEntry : structured)
        for (auto& layerEntry : noteEntry.second)          // std::map = ordered by layer
            byNote[noteEntry.first].push_back (layerEntry.second);

    bool chromaticFallback = false;
    if (byNote.empty())
    {
        // No note info anywhere: map files in order, chromatically, from A0.
        chromaticFallback = true;
        int n = 21;
        for (auto& f : files)
        {
            if (n > 127) break;
            byNote[n++].push_back (f);
        }
    }

    std::vector<int> roots;
    int totalFiles = 0;
    for (auto& kv : byNote)
    {
        roots.push_back (kv.first);
        totalFiles += (int) kv.second.size();
    }

    std::vector<PianoSound::Ptr> sounds;
    int done = 0;

    for (size_t i = 0; i < roots.size(); ++i)
    {
        const int root = roots[i];
        const int lo = (i == 0) ? 0 : (roots[i - 1] + root) / 2 + 1;
        const int hi = (i + 1 == roots.size()) ? 127 : (root + roots[i + 1]) / 2;

        auto& layerFiles = byNote[root];
        const int numLayers = (int) layerFiles.size();

        // Several files on the same note = velocity layers, split evenly over 1..127.
        for (int j = 0; j < numLayers; ++j)
        {
            if (thread.threadShouldExit())
                return;

            ++done;
            setStatus ("Loading " + juce::String (done) + " / " + juce::String (totalFiles) + "...");

            std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (layerFiles[(size_t) j]));
            if (reader == nullptr || reader->lengthInSamples <= 1)
                continue;

            const int length = (int) reader->lengthInSamples;
            juce::AudioBuffer<float> buf (juce::jmin (2, (int) reader->numChannels), length);
            reader->read (&buf, 0, length, 0, true, true);

            const int vLo = (j * 127) / numLayers + 1;
            const int vHi = ((j + 1) * 127) / numLayers;

            sounds.push_back (new PianoSound (std::move (buf), reader->sampleRate, root,
                                              lo, hi, vLo, vHi, numLayers == 1));
        }
    }

    if (sounds.empty())
    {
        setStatus ("Couldn't read any audio files in that folder.");
        return;
    }

    synth.allNotesOff (0, false);
    synth.clearSounds();
    for (auto& s : sounds)
        synth.addSound (s.get());

    auto msg = "Loaded " + juce::String ((int) sounds.size()) + " samples on "
               + juce::String ((int) roots.size()) + " notes from \"" + folder.getFileName() + "\"";
    if (chromaticFallback)
        msg << " (no note names found in filenames - mapped in order from A0)";
    else if (unparsed > 0)
        msg << " (" + juce::String (unparsed) + " files skipped: no note name)";
    if (otherBanks > 0)
        msg << " [" + juce::String (otherBanks) + " files in other banks ignored]";

    setStatus (msg);
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PianoSamplerAudioProcessor();
}
