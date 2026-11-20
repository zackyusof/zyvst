/*
  The AU entry point. Each AU target compiles the same engine; the wrapper
  identity (type, subtype, manufacturer) comes from the target's JucePlugin_*
  defines, so one entry point serves all three targets.
*/

#include "HybridProcessor.h"

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new zyv::HybridProcessor();
}
