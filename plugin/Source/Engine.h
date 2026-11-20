#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

namespace zyv
{

/** The seam between the AU shell and whatever actually produces audio.
    Implemented by ChainEngine (in-process VST3 chain). There is no remote or
    helper engine: those were removed with the engine selector. */
class DspEngine
{
public:
    virtual ~DspEngine() = default;

    virtual bool prepare (double sampleRate, int blockSize) = 0;
    virtual void release() = 0;

    /** Called from the audio thread. Must not allocate or block. */
    virtual void process (juce::AudioBuffer<float>&, juce::MidiBuffer&, juce::AudioPlayHead* = nullptr) = 0;

    virtual int  latencySamples() const = 0;
    virtual bool isReady() const = 0;
    virtual juce::String statusText() const = 0;

    virtual juce::ValueTree saveState() const = 0;
    virtual bool restoreState (const juce::ValueTree&) = 0;
};

} // namespace zyv
