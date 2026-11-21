#include "ChainEngine.h"

namespace zyv
{

namespace
{
constexpr const char* chainTag = "CHAIN";
constexpr const char* slotTag  = "SLOT";
constexpr const char* bypassTag = "bypassed";
constexpr const char* soloTag   = "soloed";
constexpr const char* descTag   = "desc";
constexpr const char* dataTag   = "data";
} // namespace

ChainEngine::ChainEngine()
{
    // Hosted format is VST3 only. The AU in this project is the outer component
    // that Logic loads, not something the engine hosts (docs/errors/au-hosting-scope.md).
    formatManager.addFormat (std::make_unique<juce::VST3PluginFormat>());
    startTimerHz (20);
}

ChainEngine::~ChainEngine()
{
    stopTimer();
    slots.clear();
}

bool ChainEngine::configureInstance (juce::AudioPluginInstance& instance, juce::String& error) const
{
    auto layout = instance.getBusesLayout();
    if (layout.outputBuses.isEmpty())
    {
        error = "Plugin requires at least one output bus";
        return false;
    }

    // Require stereo main output bus, disable auxiliary outputs
    layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
    for (int i = 1; i < layout.outputBuses.size(); ++i)
        layout.outputBuses.getReference (i) = juce::AudioChannelSet::disabled();

    const bool hasInputs = ! layout.inputBuses.isEmpty();
    for (int i = 1; i < layout.inputBuses.size(); ++i)
        layout.inputBuses.getReference (i) = juce::AudioChannelSet::disabled();

    bool configured = false;

    // 1. Try configuring with stereo main input (audio effect or hybrid synth)
    if (hasInputs)
    {
        layout.inputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        if (instance.setBusesLayout (layout) && instance.getBusesLayout().getMainOutputChannelSet() == juce::AudioChannelSet::stereo())
            configured = true;
    }

    // 2. If stereo input is not supported (e.g. pure synth / instrument), configure with disabled (0-channel) input
    if (! configured)
    {
        if (hasInputs)
            layout.inputBuses.getReference (0) = juce::AudioChannelSet::disabled();

        if (instance.setBusesLayout (layout) && instance.getBusesLayout().getMainOutputChannelSet() == juce::AudioChannelSet::stereo())
            configured = true;
    }

    if (! configured)
    {
        error = "Plugin cannot provide stereo main output";
        return false;
    }

    return true;
}

void ChainEngine::watchInstance (Slot& slot)
{
    slot.listener = this;
    slot.instance->addListener (this);
}

void ChainEngine::audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details)
{
    // May be called by the child on the audio thread: no message posting/locking.
    if (details.latencyChanged)
        latencyDirty.store (true, std::memory_order_relaxed);
}

void ChainEngine::timerCallback()
{
    if (latencyDirty.exchange (false, std::memory_order_relaxed))
    {
        updateLatency();
        notifyChange();
    }
}

juce::Array<juce::File> ChainEngine::findVST3Bundles()
{
    juce::Array<juce::File> bundles;
    juce::StringArray seen;

    const juce::File roots[] = {
        juce::File ("/Library/Audio/Plug-Ins/VST3"),
        juce::File::getSpecialLocation (juce::File::userHomeDirectory)
            .getChildFile ("Library/Audio/Plug-Ins/VST3"),
        juce::File ("/usr/lib/vst3"),
        juce::File ("/usr/local/lib/vst3"),
        juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".vst3")
    };

    for (const auto& root : roots)
    {
        if (! root.isDirectory())
            continue;

        // Filesystem listing only: no plugin module is loaded here.
        for (const auto& entry : juce::RangedDirectoryIterator (root, true, "*.vst3",
                                                               juce::File::findDirectories))
        {
            const auto path = entry.getFile().getFullPathName();

            if (seen.contains (path))
                continue;

            seen.add (path);
            bundles.add (entry.getFile());
        }
    }

    std::sort (bundles.begin(), bundles.end(),
               [] (const juce::File& a, const juce::File& b)
               { return a.getFileName().compareIgnoreCase (b.getFileName()) < 0; });

    return bundles;
}

bool ChainEngine::describeBundle (const juce::File& bundle,
                                  juce::Array<juce::PluginDescription>& out,
                                  juce::String& error)
{
    out.clearQuick();
    error.clear();

    if (! bundle.exists() || bundle.getFileExtension().compareIgnoreCase (".vst3") != 0)
    {
        error = "Not a VST3 bundle: " + bundle.getFullPathName();
        return false;
    }

    juce::AudioPluginFormat* vst3 = nullptr;

    for (int i = 0; i < formatManager.getNumFormats(); ++i)
        if (formatManager.getFormat (i)->getName().containsIgnoreCase ("VST3"))
            vst3 = formatManager.getFormat (i);

    if (vst3 == nullptr)
    {
        error = "No VST3 format registered";
        return false;
    }

    juce::OwnedArray<juce::PluginDescription> found;
    vst3->findAllTypesForFile (found, bundle.getFullPathName());

    for (const auto* desc : found)
        out.add (*desc);

    if (out.isEmpty())
    {
        error = "No plugin types found in " + bundle.getFileName();
        return false;
    }

    return true;
}

namespace
{
/** Reads the first quoted value after a JSON-ish key, tolerating trailing commas
    and other non-strict JSON that some VST3 moduleinfo files contain. */
juce::String quotedValueAfterKey (const juce::String& text, const juce::String& key)
{
    const int keyPos = text.indexOfIgnoreCase (key);

    if (keyPos < 0)
        return {};

    int pos = keyPos + key.length();

    while (pos < text.length() && text[pos] != ':')
        ++pos;

    if (pos >= text.length())
        return {};

    ++pos;

    while (pos < text.length() && (text[pos] == ' ' || text[pos] == '\t'))
        ++pos;

    if (pos >= text.length() || text[pos] != '"')
        return {};

    const int start = ++pos;

    while (pos < text.length() && text[pos] != '"')
        ++pos;

    return text.substring (start, pos).trim();
}

/** Reads <key>KEY</key><string>VALUE</string> from an XML plist. */
juce::String xmlPlistValue (const juce::String& text, const juce::String& key)
{
    const int keyPos = text.indexOfIgnoreCase ("<key>" + key + "</key>");

    if (keyPos < 0)
        return {};

    const int valueStart = text.indexOfIgnoreCase (keyPos, "<string>");

    if (valueStart < 0)
        return {};

    const int from = valueStart + 8;
    const int to = text.indexOfIgnoreCase (from, "</string>");

    if (to < 0)
        return {};

    return text.substring (from, to).trim();
}

bool isStandardVST3Root (const juce::File& dir)
{
    const auto path = dir.getFullPathName();

    return path == "/Library/Audio/Plug-Ins/VST3"
        || path == "/usr/lib/vst3"
        || path == "/usr/local/lib/vst3"
        || path == juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".vst3").getFullPathName()
        || path == juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                        .getChildFile ("Library/Audio/Plug-Ins/VST3").getFullPathName();
}
} // namespace

juce::String ChainEngine::vendorForBundle (const juce::File& bundle)
{
    if (! bundle.exists() || bundle.getFileExtension().compareIgnoreCase (".vst3") != 0)
        return "Other";

    // 1. The VST3 module information carries the real vendor name.
    const auto moduleInfo = bundle.getChildFile ("Contents/Resources/moduleinfo.json");

    if (moduleInfo.existsAsFile())
    {
        const auto vendor = quotedValueAfterKey (moduleInfo.loadFileAsString(), "\"Vendor\"");

        if (vendor.isNotEmpty())
            return vendor;
    }

    // 2. Manufacturer folders are common in VST3 installs.
    const auto parent = bundle.getParentDirectory();

    if (! isStandardVST3Root (parent) && ! isStandardVST3Root (parent.getParentDirectory())
        && parent.getFileName().isNotEmpty())
        return parent.getFileName();

    // 3. Bundle identifier, e.g. com.softube.X -> "Softube".
    const auto plist = bundle.getChildFile ("Contents/Info.plist");

    if (plist.existsAsFile())
    {
        const auto text = plist.loadFileAsString();

        if (text.contains ("<plist"))
        {
            const auto identifier = xmlPlistValue (text, "CFBundleIdentifier");
            const auto parts = juce::StringArray::fromTokens (identifier, ".", "");

            if (parts.size() > 1 && parts[0].equalsIgnoreCase ("com") && parts[1].isNotEmpty())
                return parts[1].substring (0, 1).toUpperCase() + parts[1].substring (1);
        }
    }

    return "Other";
}

void ChainEngine::rebuildActiveChain()
{
    auto newChain = std::make_unique<ActiveChain>();
    for (auto& s : slots)
    {
        if (s->instance != nullptr)
            newChain->slots.push_back ({ s->instance.get(), &s->bypassed, &s->soloed });
    }
    activeChain.store (newChain.get(), std::memory_order_release);
    activeChainStorage = std::move (newChain);
}

void ChainEngine::prepareInstance (juce::AudioPluginInstance& instance) const
{
    instance.setRateAndBufferSizeDetails (currentSampleRate, currentBlockSize);
    instance.prepareToPlay (currentSampleRate, currentBlockSize);
}

bool ChainEngine::prepare (double sampleRate, int blockSize)
{
    currentSampleRate = sampleRate;
    currentBlockSize = blockSize;
    midiClock.prepare (sampleRate);

    {
        const juce::SpinLock::ScopedLockType lock (slotLock);

        for (auto& slot : slots)
            if (slot->instance != nullptr)
                prepareInstance (*slot->instance);

        rebuildActiveChain();
    }

    updateLatency();
    return true;
}

void ChainEngine::release()
{
    midiClock.reset();
    const juce::SpinLock::ScopedLockType lock (slotLock);

    for (auto& slot : slots)
        if (slot->instance != nullptr)
            slot->instance->releaseResources();
}

void ChainEngine::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, juce::AudioPlayHead* playHead)
{
    const auto numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    // Never pass non-stereo buffers into the stereo serial chain
    if (buffer.getNumChannels() != 2)
    {
        processLayoutError.store (true, std::memory_order_relaxed);
        buffer.clear();
        midi.clear();
        return;
    }

    // 1. Full sample-accurate MIDI clock & transport generation
    midiClock.process (playHead, numSamples, midi);

    // 2. Lock-free active chain retrieval (zero spinlock, zero audio dropout risk)
    const auto* chain = activeChain.load (std::memory_order_acquire);
    if (chain == nullptr || chain->slots.empty())
        return;

    // 3. Process serial chain with full MIDI and instrument support
    juce::MidiBuffer currentMidi = midi;

    // Solo is exclusive: while anything is soloed, only soloed stages run. The
    // flags are plain atomics, so this stays lock-free on the audio thread.
    bool anySoloed = false;

    for (const auto& slot : chain->slots)
        if (slot.soloed != nullptr && slot.soloed->load (std::memory_order_relaxed))
        {
            anySoloed = true;
            break;
        }

    for (const auto& slot : chain->slots)
    {
        if (slot.instance == nullptr || (slot.bypassed != nullptr && slot.bypassed->load (std::memory_order_relaxed)))
            continue;

        if (anySoloed && (slot.soloed == nullptr || ! slot.soloed->load (std::memory_order_relaxed)))
            continue;

        auto* instance = slot.instance;

        // Provide host playhead to hosted VST3 plugin for tempo & beat sync
        instance->setPlayHead (playHead);

        const int inChannels = instance->getMainBusNumInputChannels();
        const int outChannels = instance->getMainBusNumOutputChannels();

        // Valid configurations: stereo out (2), and either stereo in (2) or synth in (0)
        if (outChannels != 2 || (inChannels != 2 && inChannels != 0))
        {
            processLayoutError.store (true, std::memory_order_relaxed);
            continue;
        }

        juce::MidiBuffer slotMidi = currentMidi;

        instance->processBlock (buffer, slotMidi);

        // If the plugin produces MIDI, propagate newly generated MIDI events to downstream plugins
        if (instance->producesMidi() && ! slotMidi.isEmpty())
        {
            currentMidi.addEvents (slotMidi, 0, numSamples, 0);
        }
    }

    processLayoutError.store (false, std::memory_order_relaxed);
    midi = currentMidi;
}

int ChainEngine::latencySamples() const
{
    return cachedLatency.load (std::memory_order_relaxed);
}

juce::String ChainEngine::statusText() const
{
    const juce::SpinLock::ScopedLockType lock (slotLock);
    if (processLayoutError.load (std::memory_order_relaxed))
        return "Local chain requires stereo main output; child bus mismatch detected";
    for (const auto& slot : slots)
        if (slot->loadError.isNotEmpty())
            return slot->desc.name + ": " + slot->loadError + " (state retained)";
    return juce::String (static_cast<int> (slots.size())) + " plugin(s), latency "
           + juce::String (cachedLatency.load()) + " samples";
}

bool ChainEngine::addPlugin (const juce::PluginDescription& desc, juce::String& error)
{
    error.clear();

    juce::String loadError;
    auto instance = formatManager.createPluginInstance (desc, currentSampleRate, currentBlockSize, loadError);

    if (instance == nullptr)
    {
        error = loadError.isNotEmpty() ? loadError : "could not create plugin instance";
        return false;
    }

    if (! configureInstance (*instance, error))
        return false;

    auto slot = std::make_unique<Slot>();
    slot->desc = desc;
    slot->instance = std::move (instance);
    prepareInstance (*slot->instance);
    watchInstance (*slot);

    {
        const juce::SpinLock::ScopedLockType lock (slotLock);
        slots.push_back (std::move (slot));
        rebuildActiveChain();
    }

    updateLatency();
    notifyChange();
    return true;
}

void ChainEngine::removePlugin (int index)
{
    {
        const juce::SpinLock::ScopedLockType lock (slotLock);

        if (! juce::isPositiveAndBelow (index, static_cast<int> (slots.size())))
            return;

        slots.erase (slots.begin() + index);
        rebuildActiveChain();
    }

    updateLatency();
    notifyChange();
}

void ChainEngine::moveSlot (int from, int to)
{
    {
        const juce::SpinLock::ScopedLockType lock (slotLock);

        const auto n = static_cast<int> (slots.size());

        if (! juce::isPositiveAndBelow (from, n) || ! juce::isPositiveAndBelow (to, n))
            return;

        auto moved = std::move (slots[static_cast<size_t> (from)]);
        slots.erase (slots.begin() + from);
        slots.insert (slots.begin() + to, std::move (moved));
        rebuildActiveChain();
    }

    notifyChange();
}

void ChainEngine::setSlotBypassed (int index, bool shouldBeBypassed)
{
    {
        const juce::SpinLock::ScopedLockType lock (slotLock);
        if (! juce::isPositiveAndBelow (index, static_cast<int> (slots.size())))
            return;
        slots[static_cast<size_t> (index)]->bypassed.store (shouldBeBypassed, std::memory_order_relaxed);
    }
    updateLatency();
    notifyChange();
}

void ChainEngine::setSlotSoloed (int index, bool shouldBeSoloed)
{
    {
        const juce::SpinLock::ScopedLockType lock (slotLock);
        if (! juce::isPositiveAndBelow (index, static_cast<int> (slots.size())))
            return;
        slots[static_cast<size_t> (index)]->soloed.store (shouldBeSoloed, std::memory_order_relaxed);
    }
    updateLatency();
    notifyChange();
}

int ChainEngine::getNumSlots() const
{
    const juce::SpinLock::ScopedLockType lock (slotLock);
    return static_cast<int> (slots.size());
}

juce::String ChainEngine::getSlotName (int index) const
{
    const juce::SpinLock::ScopedLockType lock (slotLock);

    if (! juce::isPositiveAndBelow (index, static_cast<int> (slots.size())))
        return {};

    return slots[static_cast<size_t> (index)]->desc.name;
}

juce::String ChainEngine::getSlotVendor (int index) const
{
    const juce::SpinLock::ScopedLockType lock (slotLock);

    if (! juce::isPositiveAndBelow (index, static_cast<int> (slots.size())))
        return {};

    const auto& slot = slots[static_cast<size_t> (index)];
    auto vendor = slot->desc.manufacturerName.trim();

    if (vendor.isEmpty() && slot->desc.fileOrIdentifier.isNotEmpty())
        vendor = vendorForBundle (juce::File (slot->desc.fileOrIdentifier));

    return vendor.isNotEmpty() ? vendor : "VST3";
}

bool ChainEngine::isSlotBypassed (int index) const
{
    const juce::SpinLock::ScopedLockType lock (slotLock);

    if (! juce::isPositiveAndBelow (index, static_cast<int> (slots.size())))
        return false;

    return slots[static_cast<size_t> (index)]->bypassed.load();
}

bool ChainEngine::isSlotSoloed (int index) const
{
    const juce::SpinLock::ScopedLockType lock (slotLock);

    if (! juce::isPositiveAndBelow (index, static_cast<int> (slots.size())))
        return false;

    return slots[static_cast<size_t> (index)]->soloed.load();
}

bool ChainEngine::isAnySlotSoloed() const
{
    const juce::SpinLock::ScopedLockType lock (slotLock);

    for (const auto& slot : slots)
        if (slot->soloed.load (std::memory_order_relaxed))
            return true;

    return false;
}

juce::AudioPluginInstance* ChainEngine::getSlotInstance (int index) const
{
    const juce::SpinLock::ScopedLockType lock (slotLock);

    if (! juce::isPositiveAndBelow (index, static_cast<int> (slots.size())))
        return nullptr;

    return slots[static_cast<size_t> (index)]->instance.get();
}

void ChainEngine::updateLatency()
{
    int total = 0;

    {
        const juce::SpinLock::ScopedLockType lock (slotLock);

        bool anySoloed = false;

        for (const auto& slot : slots)
            if (slot->soloed.load (std::memory_order_relaxed))
            {
                anySoloed = true;
                break;
            }

        for (auto& slot : slots)
            if (slot->instance != nullptr && ! slot->bypassed.load (std::memory_order_relaxed)
                && (! anySoloed || slot->soloed.load (std::memory_order_relaxed)))
                total += slot->instance->getLatencySamples();
    }

    cachedLatency.store (total, std::memory_order_relaxed);
}

void ChainEngine::notifyChange()
{
    if (onChange != nullptr)
        onChange();
}

juce::ValueTree ChainEngine::saveState() const
{
    juce::ValueTree chain (chainTag);
    const juce::SpinLock::ScopedLockType lock (slotLock);

    for (auto& slot : slots)
    {
        juce::ValueTree node (slotTag);
        node.setProperty (bypassTag, slot->bypassed.load(), nullptr);
        node.setProperty (soloTag, slot->soloed.load(), nullptr);

        if (auto xml = slot->desc.createXml())
            node.setProperty (descTag, xml->toString (juce::XmlElement::TextFormat().singleLine()), nullptr);

        if (slot->instance != nullptr)
        {
            juce::MemoryBlock state;
            slot->instance->getStateInformation (state);
            node.setProperty (dataTag, state.toBase64Encoding(), nullptr);
        }
        else
        {
            node.setProperty (dataTag, slot->savedData, nullptr);
        }

        chain.appendChild (node, nullptr);
    }

    return chain;
}

bool ChainEngine::restoreState (const juce::ValueTree& chain)
{
    if (! chain.hasType (chainTag))
        return false;

    {
        const juce::SpinLock::ScopedLockType lock (slotLock);
        slots.clear();
    }

    for (auto node : chain)
    {
        if (! node.hasType (slotTag))
            continue;

        auto descXml = node.getProperty (descTag).toString();

        if (descXml.isEmpty())
            continue;

        auto xml = juce::XmlDocument::parse (descXml);

        if (xml == nullptr)
            continue;

        juce::PluginDescription desc;

        if (! desc.loadFromXml (*xml))
            continue;

        auto slot = std::make_unique<Slot>();
        slot->desc = desc;
        slot->bypassed.store (static_cast<bool> (node.getProperty (bypassTag, false)));
        slot->soloed.store (static_cast<bool> (node.getProperty (soloTag, false)));
        slot->savedData = node.getProperty (dataTag).toString();

        juce::String error;
        slot->instance = formatManager.createPluginInstance (desc, currentSampleRate, currentBlockSize, error);
        if (slot->instance != nullptr && ! configureInstance (*slot->instance, error))
            slot->instance.reset();
        if (slot->instance == nullptr)
            slot->loadError = error.isNotEmpty() ? error : "Plugin unavailable";

        if (slot->instance != nullptr)
        {
            const auto& encoded = slot->savedData;

            if (encoded.isNotEmpty())
            {
                juce::MemoryBlock state;
                state.fromBase64Encoding (encoded);
                slot->instance->setStateInformation (state.getData(), static_cast<int> (state.getSize()));
            }
            // State may alter bus configuration; re-check before preparing.
            if (! configureInstance (*slot->instance, error))
            {
                slot->instance.reset();
                slot->loadError = error;
            }
            else
            {
                prepareInstance (*slot->instance);
                watchInstance (*slot);
            }
        }

        const juce::SpinLock::ScopedLockType lock (slotLock);
        slots.push_back (std::move (slot));
    }

    {
        const juce::SpinLock::ScopedLockType lock (slotLock);
        rebuildActiveChain();
    }

    updateLatency();
    notifyChange();
    return true;
}

} // namespace zyv
