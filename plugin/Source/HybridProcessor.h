#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ChainEngine.h"

namespace zyv
{

/** The AU shell: one in-process VST3 chain, and a truthful latency report.

    There is no engine selector any more: the local chain is the product. Latency
    is reported with `setLatencySamples`, which JUCE forwards to the host through
    `updateHostDisplay (…latencyChanged…)`; the AU wrapper answers
    `kAudioUnitProperty_Latency` from the same value. */
class HybridProcessor final : public juce::AudioProcessor,
                              private juce::AudioProcessorParameter::Listener,
                              private juce::Timer
{
public:
    HybridProcessor();
    ~HybridProcessor() override = default;

    void prepareToPlay (double sampleRate, int blockSize) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override
    {
#ifdef JucePlugin_Name
        return JucePlugin_Name;
#else
        return "zyvst";
#endif
    }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    bool isBusesLayoutSupported (const BusesLayout&) const override;

    //--- editor-facing API (message thread only) ------------------------------
    ChainEngine& getChain() noexcept { return chain; }
    bool isBypassed() const { return bypassParam != nullptr && bypassParam->get(); }
    void setBypassed (bool shouldBeBypassed);

    /** Runs a chain mutation with the audio path suspended. */
    void withChainSuspended (const std::function<void()>& mutation);

    /** Pushes the chain's current latency to the host if it changed. */
    void reportLatencyToHost();

private:
    void parameterValueChanged (int parameterIndex, float newValue) override;
    void parameterGestureChanged (int, bool) override {}
    void timerCallback() override;
    void updateBypassDelay();
    void processDryDelay (juce::AudioBuffer<float>&, bool replaceOutput);

    ChainEngine chain;

    juce::AudioParameterBool* bypassParam = nullptr;

    juce::AudioBuffer<float> bypassDelay;
    int bypassDelayPosition = 0;

    double lastSampleRate = 44100.0;
    int lastBlockSize = 512;
    bool prepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HybridProcessor)
};

} // namespace zyv
