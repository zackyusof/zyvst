#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>

namespace zyv
{

/** Sample-accurate MIDI Clock and Transport Generator.

    Generates standard MIDI 1.0/2.0 real-time sync messages:
      - 0xF8: Timing Clock (24 PPQ - 24 pulses per quarter note)
      - 0xFA: Start (playback started from beginning / position 0)
      - 0xFB: Continue (playback resumed from non-zero position)
      - 0xFC: Stop (playback stopped)
      - 0xF2: Song Position Pointer (SPP: 14-bit integer, 1 MIDI beat = 6 clocks = 1/16th note)

    Phase-locks sample-accurately to host AudioPlayHead PPQ transport to guarantee
    zero jitter and zero drift, with intelligent fallback to sample counting when
    PPQ is not supplied by the host.
*/
class MidiClock
{
public:
    MidiClock() = default;

    void prepare (double sampleRate) noexcept
    {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        reset();
    }

    void reset() noexcept
    {
        wasPlaying = false;
        lastPpq = -1.0;
        clockPhase = 0.0;
        lastTempo = 120.0;
    }

    /** Generates sample-accurate MIDI clock messages into midiMessages based on playhead state. */
    void process (juce::AudioPlayHead* playHead, int numSamples, juce::MidiBuffer& midiMessages)
    {
        if (numSamples <= 0)
            return;

        bool isPlaying = false;
        double bpm = lastTempo > 10.0 ? lastTempo : 120.0;
        double currentPpq = -1.0;
        bool hasPpq = false;

        if (playHead != nullptr)
        {
            if (auto pos = playHead->getPosition())
            {
                isPlaying = pos->getIsPlaying();
                if (auto b = pos->getBpm())
                {
                    if (*b > 10.0 && *b < 999.0)
                    {
                        bpm = *b;
                        lastTempo = bpm;
                    }
                }
                if (auto p = pos->getPpqPosition())
                {
                    currentPpq = *p;
                    hasPpq = true;
                }
            }
        }

        // 1. Handle transport state changes (Start / Continue / Stop / SPP)
        if (isPlaying && ! wasPlaying)
        {
            // Transition from stopped to playing
            if (hasPpq && currentPpq > 0.001)
            {
                // Resuming from non-zero position: send Song Position Pointer then Continue
                const int sppBeats = juce::jmax (0, static_cast<int> (std::floor (currentPpq * 4.0)));
                sendSongPositionPointer (sppBeats, 0, midiMessages);
                midiMessages.addEvent (juce::MidiMessage::midiContinue(), 0);
            }
            else
            {
                // Starting from the beginning: send Start
                midiMessages.addEvent (juce::MidiMessage::midiStart(), 0);
            }

            wasPlaying = true;
            clockPhase = hasPpq ? (currentPpq * 24.0) : 0.0;
            lastPpq = currentPpq;
        }
        else if (! isPlaying && wasPlaying)
        {
            // Transition from playing to stopped
            midiMessages.addEvent (juce::MidiMessage::midiStop(), 0);
            wasPlaying = false;
            lastPpq = -1.0;
            return;
        }
        else if (isPlaying && hasPpq && lastPpq >= 0.0)
        {
            // Detect seek or loop jump during playback
            const double expectedPpq = lastPpq + (static_cast<double> (numSamples) * bpm) / (currentSampleRate * 60.0);
            if (std::abs (currentPpq - expectedPpq) > 0.1 || currentPpq < lastPpq)
            {
                // Transport jumped or looped -> send SPP
                const int sppBeats = juce::jmax (0, static_cast<int> (std::floor (currentPpq * 4.0)));
                sendSongPositionPointer (sppBeats, 0, midiMessages);
                clockPhase = currentPpq * 24.0;
            }
        }

        if (! isPlaying)
            return;

        // 2. Sample-accurate MIDI Timing Clock pulses (24 PPQ)
        const double ppqPerSample = bpm / (60.0 * currentSampleRate);
        const double clocksPerSample = ppqPerSample * 24.0;

        if (hasPpq)
        {
            const double startClock = currentPpq * 24.0;
            int nextClockInt = static_cast<int> (std::floor (startClock)) + 1;

            for (int s = 0; s < numSamples; ++s)
            {
                const double clockNow = startClock + static_cast<double> (s) * clocksPerSample;
                if (clockNow >= nextClockInt)
                {
                    midiMessages.addEvent (juce::MidiMessage::midiClock(), s);
                    nextClockInt = static_cast<int> (std::floor (clockNow)) + 1;
                }
            }
            lastPpq = currentPpq + static_cast<double> (numSamples) * ppqPerSample;
        }
        else
        {
            // Fallback when host does not supply PPQ: advance internal clock phase
            for (int s = 0; s < numSamples; ++s)
            {
                const int prevInt = static_cast<int> (std::floor (clockPhase));
                clockPhase += clocksPerSample;
                const int currInt = static_cast<int> (std::floor (clockPhase));

                if (currInt > prevInt)
                    midiMessages.addEvent (juce::MidiMessage::midiClock(), s);
            }
        }
    }

private:
    static void sendSongPositionPointer (int midiBeats, int sampleOffset, juce::MidiBuffer& midi)
    {
        // Song Position Pointer: 0xF2 <LSB> <MSB>, 14-bit integer
        const int value = juce::jlimit (0, 16383, midiBeats);
        const auto lsb = static_cast<juce::uint8> (value & 0x7F);
        const auto msb = static_cast<juce::uint8> ((value >> 7) & 0x7F);
        midi.addEvent (juce::MidiMessage (0xF2, lsb, msb), sampleOffset);
    }

    double currentSampleRate = 44100.0;
    bool wasPlaying = false;
    double lastPpq = -1.0;
    double clockPhase = 0.0;
    double lastTempo = 120.0;
};

} // namespace zyv
