#include "HybridProcessor.h"

#include "HybridEditor.h"

namespace zyv
{

namespace
{
// The callback lock drains an in-flight block and serialises lifecycle writers.
// Preserve host suspension, including when these scopes are nested.
class ScopedSuspension
{
public:
    explicit ScopedSuspension (juce::AudioProcessor& p)
        : processor (p), lock (p.getCallbackLock()), wasSuspended (p.isSuspended())
    {
        processor.suspendProcessing (true);
    }

    ~ScopedSuspension() { processor.suspendProcessing (wasSuspended); }

private:
    juce::AudioProcessor& processor;
    const juce::ScopedLock lock;
    const bool wasSuspended;
};
} // namespace

HybridProcessor::HybridProcessor()
    : juce::AudioProcessor (
#if JucePlugin_IsSynth
        BusesProperties()
            .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
#else
        BusesProperties()
            .withInput ("Input", juce::AudioChannelSet::stereo(), true)
            .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
#endif
      )
{
    bypassParam = new juce::AudioParameterBool ("bypass", "Bypass", false);
    addParameter (bypassParam);
    bypassParam->addListener (this);

    chain.setChangeCallback ([this] { reportLatencyToHost(); });

    // Safety net for latency changes that arrive without a chain mutation
    // (e.g. a hosted plugin switching its own oversampling).
    startTimerHz (4);
}

void HybridProcessor::prepareToPlay (double sampleRate, int blockSize)
{
    const ScopedSuspension suspension (*this);
    lastSampleRate = sampleRate;
    lastBlockSize = blockSize;
    prepared = false;

    chain.prepare (sampleRate, blockSize);

    prepared = true;
    updateBypassDelay();
    reportLatencyToHost();
}

void HybridProcessor::releaseResources()
{
    const ScopedSuspension suspension (*this);
    chain.release();
    prepared = false;
}

void HybridProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const juce::ScopedTryLock callbackLock (getCallbackLock());

    if (! callbackLock.isLocked() || isSuspended())
    {
        buffer.clear();
        midi.clear();
        return;
    }

    const auto bypassed = ! chain.isReady() || bypassParam->get();
    processDryDelay (buffer, bypassed);

    if (bypassed)
        return;

    chain.process (buffer, midi, getPlayHead());
}

void HybridProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const juce::ScopedTryLock callbackLock (getCallbackLock());

    if (! callbackLock.isLocked() || isSuspended())
    {
        buffer.clear();
        midi.clear();
        return;
    }

    processDryDelay (buffer, true);
}

void HybridProcessor::updateBypassDelay()
{
    // Lifecycle thread only, under the callback lock. Sizing follows the latency
    // we report, so a bypassed chain stays aligned with the host's compensation.
    const auto latency = juce::jmax (0, chain.latencySamples());
    const auto channels = getTotalNumInputChannels();

    if (channels <= 0 || latency <= 0)
    {
        bypassDelay.setSize (0, 0);
        bypassDelayPosition = 0;
        return;
    }

    if (bypassDelay.getNumSamples() != latency || bypassDelay.getNumChannels() != channels)
    {
        bypassDelay.setSize (channels, latency);
        bypassDelay.clear();
        bypassDelayPosition = 0;
    }
}

void HybridProcessor::processDryDelay (juce::AudioBuffer<float>& buffer, bool replaceOutput)
{
    const auto delay = bypassDelay.getNumSamples();
    const auto channels = juce::jmin (buffer.getNumChannels(), bypassDelay.getNumChannels());

    if (delay <= 0 || channels <= 0)
        return;

    const auto numSamples = buffer.getNumSamples();
    auto* ringL = bypassDelay.getWritePointer (0);
    auto* outL = buffer.getWritePointer (0);
    auto* ringR = channels > 1 ? bypassDelay.getWritePointer (1) : nullptr;
    auto* outR = channels > 1 ? buffer.getWritePointer (1) : nullptr;

    int pos = bypassDelayPosition;

    if (replaceOutput)
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            const float dryL = ringL[pos];
            ringL[pos] = outL[sample];
            outL[sample] = dryL;

            if (ringR != nullptr && outR != nullptr)
            {
                const float dryR = ringR[pos];
                ringR[pos] = outR[sample];
                outR[sample] = dryR;
            }

            if (++pos >= delay)
                pos = 0;
        }
    }
    else
    {
        for (int sample = 0; sample < numSamples; ++sample)
        {
            ringL[pos] = outL[sample];
            if (ringR != nullptr && outR != nullptr)
                ringR[pos] = outR[sample];

            if (++pos >= delay)
                pos = 0;
        }
    }

    bypassDelayPosition = pos;
}

bool HybridProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.outputBuses.size() != 1)
        return false;

    if (layouts.inputBuses.isEmpty())
        return true;

    if (layouts.inputBuses.size() > 1)
        return false;

    const auto in = layouts.getMainInputChannelSet();
    return in == juce::AudioChannelSet::stereo() || in.isDisabled();
}

void HybridProcessor::parameterValueChanged (int, float)
{
    // Audio-thread-safe: nothing to do beyond the atomic parameter read.
}

void HybridProcessor::timerCallback()
{
    reportLatencyToHost();
}

void HybridProcessor::reportLatencyToHost()
{
    // setLatencySamples forwards a latencyChanged notification to the host and
    // only fires when the value actually differs.
    const juce::ScopedLock callbackLock (getCallbackLock());
    const auto latency = juce::jmax (0, chain.latencySamples());

    setLatencySamples (latency);
    updateBypassDelay();
}

void HybridProcessor::withChainSuspended (const std::function<void()>& mutation)
{
    {
        const ScopedSuspension suspension (*this);
        mutation();
    }

    reportLatencyToHost();
}

void HybridProcessor::setBypassed (bool shouldBeBypassed)
{
    *bypassParam = shouldBeBypassed;
}

void HybridProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const ScopedSuspension suspension (*this);
    juce::ValueTree root ("ZYVST");
    root.setProperty ("bypass", bypassParam->get(), nullptr);
    root.appendChild (chain.saveState(), nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void HybridProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr)
        return;

    const auto root = juce::ValueTree::fromXml (*xml);

    if (! root.hasType ("ZYVST"))
        return;

    // GUI lifetime comes before chain locks: an open hosted editor must be
    // destroyed synchronously before restore destroys its AudioProcessor.
    const juce::MessageManagerLock messageLock;

    if (auto* editor = dynamic_cast<HybridEditor*> (getActiveEditor()))
        editor->closeHostedWindows();

    withChainSuspended ([this, root]
    {
        *bypassParam = static_cast<bool> (root.getProperty ("bypass", false));

        if (auto chainState = root.getChildWithName ("CHAIN"); chainState.isValid())
            chain.restoreState (chainState);
    });
}

juce::AudioProcessorEditor* HybridProcessor::createEditor()
{
    return new HybridEditor (*this);
}

} // namespace zyv
