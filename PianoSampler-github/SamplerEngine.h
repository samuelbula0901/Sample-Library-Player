#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cmath>

//==============================================================================
// One sample zone: audio + the key range and velocity range it responds to.
struct PianoSound : public juce::SynthesiserSound
{
    using Ptr = juce::ReferenceCountedObjectPtr<PianoSound>;

    PianoSound (juce::AudioBuffer<float>&& audio, double sourceRate, int root,
                int lowKey, int highKey, int lowVelocity, int highVelocity, bool single)
        : data (std::move (audio)), sampleRate (sourceRate), rootNote (root),
          lowNote (lowKey), highNote (highKey), lowVel (lowVelocity),
          highVel (highVelocity), singleLayer (single) {}

    bool appliesToNote (int note) override   { return note >= lowNote && note <= highNote; }
    bool appliesToChannel (int) override     { return true; }

    juce::AudioBuffer<float> data;
    double sampleRate;
    int rootNote, lowNote, highNote, lowVel, highVel;
    bool singleLayer;
};

//==============================================================================
class PianoVoice : public juce::SynthesiserVoice
{
public:
    explicit PianoVoice (std::atomic<float>* releaseSeconds) : release (releaseSeconds) {}

    bool canPlaySound (juce::SynthesiserSound* s) override
    {
        return dynamic_cast<PianoSound*> (s) != nullptr;
    }

    void startNote (int midiNote, float velocity, juce::SynthesiserSound* s, int) override
    {
        sound = static_cast<PianoSound*> (s);
        position = 0.0;
        pitchRatio = std::pow (2.0, (midiNote - sound->rootNote) / 12.0)
                     * sound->sampleRate / getSampleRate();

        // Velocity layers already carry their own dynamics, so scale less.
        gain = sound->singleLayer ? (0.25f + 0.75f * velocity)
                                  : (0.80f + 0.20f * velocity);
        releasing = false;
        releaseGain = 1.0f;
    }

    void stopNote (float, bool allowTailOff) override
    {
        if (allowTailOff && sound != nullptr)
        {
            const double seconds = juce::jmax (0.01f, release->load());
            releaseDelta = (float) (1.0 / (seconds * getSampleRate()));
            releasing = true;
        }
        else
        {
            sound = nullptr;
            clearCurrentNote();
        }
    }

    void pitchWheelMoved (int) override {}
    void controllerMoved (int, int) override {}

    using juce::SynthesiserVoice::renderNextBlock;
    void renderNextBlock (juce::AudioBuffer<float>& out, int startSample, int numSamples) override
    {
        if (sound == nullptr)
            return;

        const auto& data = sound->data;
        const int length = data.getNumSamples();
        const float* inL = data.getReadPointer (0);
        const float* inR = data.getReadPointer (data.getNumChannels() > 1 ? 1 : 0);

        float* outL = out.getWritePointer (0, startSample);
        float* outR = out.getNumChannels() > 1 ? out.getWritePointer (1, startSample) : nullptr;

        for (int i = 0; i < numSamples; ++i)
        {
            const int p = (int) position;
            if (p + 1 >= length)
            {
                sound = nullptr;
                clearCurrentNote();
                break;
            }

            const float f = (float) (position - p);
            const float l = inL[p] * (1.0f - f) + inL[p + 1] * f;
            const float r = inR[p] * (1.0f - f) + inR[p + 1] * f;
            const float g = gain * releaseGain;

            outL[i] += l * g;
            if (outR != nullptr)
                outR[i] += r * g;

            position += pitchRatio;

            if (releasing)
            {
                releaseGain -= releaseDelta;
                if (releaseGain <= 0.0f)
                {
                    sound = nullptr;
                    clearCurrentNote();
                    break;
                }
            }
        }
    }

private:
    std::atomic<float>* release;
    PianoSound* sound = nullptr;
    double position = 0.0, pitchRatio = 1.0;
    float gain = 1.0f, releaseGain = 1.0f, releaseDelta = 0.001f;
    bool releasing = false;
};

//==============================================================================
// Synth that picks the zone by note AND velocity (JUCE's default only uses note).
class PianoSynth : public juce::Synthesiser
{
public:
    void noteOn (int midiChannel, int midiNote, float velocity) override
    {
        const juce::ScopedLock sl (lock);

        const int vel = juce::jlimit (1, 127, (int) std::lround (velocity * 127.0f));

        PianoSound* chosen = nullptr;
        for (auto* s : sounds)
        {
            if (auto* p = dynamic_cast<PianoSound*> (s))
            {
                if (p->appliesToNote (midiNote) && vel >= p->lowVel && vel <= p->highVel)
                {
                    chosen = p;
                    break;
                }
            }
        }

        if (chosen == nullptr)
            return;

        // Let any previous instance of this note ring out its release.
        for (auto* v : voices)
            if (v->getCurrentlyPlayingNote() == midiNote && v->isPlayingChannel (midiChannel))
                stopVoice (v, 1.0f, true);

        if (auto* voice = findFreeVoice (chosen, midiChannel, midiNote, isNoteStealingEnabled()))
            startVoice (voice, chosen, midiChannel, midiNote, velocity);
    }
};
