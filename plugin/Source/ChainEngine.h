#pragma once

#include "Engine.h"
#include "MidiClock.h"

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace zyv
{

/** Local, in-process serial chain of VST3 plugins (DES-2).

    The wrapper itself is an AU (that is what Logic loads); the plugins it hosts
    are VST3 only. See docs/errors/au-hosting-scope.md.

    Threading contract (REQ-8 / INV-1):
      - process() runs on the audio thread and only try-locks.
      - every mutation happens on the message thread while the host processor is
        suspended (INV-2) and takes the same lock exclusively. */
class ChainEngine final : public DspEngine,
                          private juce::AudioProcessorListener,
                          private juce::Timer
{
public:
    ChainEngine();
    ~ChainEngine() override;

    bool prepare (double sampleRate, int blockSize) override;
    void release() override;
    void process (juce::AudioBuffer<float>&, juce::MidiBuffer&, juce::AudioPlayHead* = nullptr) override;

    int  latencySamples() const override;
    bool isReady() const override { return ! processLayoutError.load (std::memory_order_relaxed); }
    juce::String statusText() const override;

    juce::ValueTree saveState() const override;
    bool restoreState (const juce::ValueTree&) override;

    //--- message-thread API ---------------------------------------------------
    juce::AudioPluginFormatManager& formats() noexcept { return formatManager; }

    /** VST3 bundles found in the standard system/user locations.
        Filesystem only: no plugin code is loaded to build this list. */
    static juce::Array<juce::File> findVST3Bundles();

    /** Resolves the plugin type(s) inside one bundle, loading its VST3 module.
        Called only when the user actually chooses a bundle. */
    bool describeBundle (const juce::File&, juce::Array<juce::PluginDescription>& out, juce::String& error);

    /** Best available vendor name without loading the plugin module:
        moduleinfo.json "Factory Info" → "Vendor", else a vendor folder,
        else the bundle-identifier's organisation token, else "Other". */
    static juce::String vendorForBundle (const juce::File&);

    /** Called on the message thread after any change that alters latency. */
    void setChangeCallback (std::function<void()> cb) { onChange = std::move (cb); }

    bool addPlugin (const juce::PluginDescription&, juce::String& error);
    void removePlugin (int index);
    /** Hard dry bypass: the skipped child contributes no latency. */
    void setSlotBypassed (int index, bool shouldBeBypassed);
    /** Exclusive audition: while any slot is soloed, only soloed slots process.
        Bypass still wins, so a soloed+bypassed slot stays silent. */
    void setSlotSoloed (int index, bool shouldBeSoloed);
    void moveSlot (int from, int to);

    int getNumSlots() const;
    juce::String getSlotName (int index) const;
    juce::String getSlotVendor (int index) const;
    bool isSlotBypassed (int index) const;
    bool isSlotSoloed (int index) const;
    bool isAnySlotSoloed() const;
    juce::AudioPluginInstance* getSlotInstance (int index) const;

private:
    friend struct ChainEngineTestAccess;
    struct Slot
    {
        ~Slot()
        {
            if (instance != nullptr && listener != nullptr)
                instance->removeListener (listener);
        }
        juce::PluginDescription desc;
        std::unique_ptr<juce::AudioPluginInstance> instance;
        std::atomic<bool> bypassed { false };
        std::atomic<bool> soloed { false };
        juce::String savedData;
        juce::String loadError;
        juce::AudioProcessorListener* listener = nullptr;
    };

    struct ActiveSlot
    {
        juce::AudioPluginInstance* instance = nullptr;
        std::atomic<bool>* bypassed = nullptr;
        std::atomic<bool>* soloed = nullptr;
    };

    struct ActiveChain
    {
        std::vector<ActiveSlot> slots;
    };

    bool configureInstance (juce::AudioPluginInstance&, juce::String& error) const;
    void prepareInstance (juce::AudioPluginInstance&) const;
    void watchInstance (Slot&);
    void rebuildActiveChain();
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override;
    void timerCallback() override;
    void updateLatency();
    void notifyChange();

    juce::AudioPluginFormatManager formatManager;
    std::vector<std::unique_ptr<Slot>> slots;
    mutable juce::SpinLock slotLock;
    std::atomic<const ActiveChain*> activeChain { nullptr };
    std::unique_ptr<ActiveChain> activeChainStorage;
    MidiClock midiClock;
    double currentSampleRate = 44100.0;
    int currentBlockSize = 512;
    std::atomic<int> cachedLatency { 0 };
    std::atomic<bool> latencyDirty { false };
    std::atomic<bool> processLayoutError { false };
    std::function<void()> onChange;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainEngine)
};

} // namespace zyv
