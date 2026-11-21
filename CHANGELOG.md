# zyvst v1.2.0

The chain gets the two moves you reach for most: **solo one stage** and **reorder by dragging**. The window also got roomier, and every control now speaks in the same type size.

## Solo, on every slot

A **SOLO** button sits beside **ON** on every stage. Press it and the rest of the chain steps aside so you hear that plug-in on its own — perfect for checking what one compressor, saturation stage, or synth is really doing. Solo several slots to audition them together. **Bypass still wins**, so a soloed-but-bypassed slot stays silent, and the latency zyvst reports always matches the stages you can hear. Solo and bypass are saved with your session.

## Reorder by dragging, and only by dragging

The **▲ / ▼** nudge buttons are gone. Grab any plug-in card and drop it where you want it: the whole card is the handle, with a live insertion line while you drag. Fewer controls, a cleaner row, and the same order you already know.

## More room for plug-in names

The processing chain is **70 px wider**, so long plug-in names and vendor names have room before they truncate. The row actions sit tighter, so the extra width goes to the name and vendor, not the buttons.

## One type size across the window

**SAMPLES**, **ACTIVE / BYPASS**, **+ Add VST3**, **Browse…**, and the **FX / INSTRUMENT / MIDI FX** label now share a single 13-point face. Labels no longer shrink or squeeze from button to button, so the header, the chain, and the footer read as one instrument.

## Everything that was already there

Three Audio Units — **zyvst_fx**, **zyvst_inst**, **zyvst_midi** — with a two-column vendor picker and instant search, embedded plug-in editors, drag-and-drop ordering, per-slot and master bypass, truthful latency reporting, MIDI clock and transport, and full chain and plug-in state recall. Universal builds for Apple Silicon and Intel Macs on macOS 13 or later.

# zyvst v1.1.2

The instrument Audio Unit is now `zyvst_inst` — same identity codes, no space in the bundle name.

- **zyvst_inst** installs as `zyvst_inst.component` and displays as **ZYVS: zyvst_inst**, matching
  `zyvst_fx` and `zyvst_midi`. The previous bundle was `zyvst inst.component`.
- The Audio Unit identity codes are unchanged (`aumu` / `ZyIn` / `Zyvs`), so existing sessions resolve to
  the same instrument slot. The bundle identifier stays `org.zyvst.instrument`.
- Installed bundles report version `1.1.2`.

# zyvst v1.0.0

Your VST3 effects, instruments, and MIDI tools are ready for your Audio Unit sessions. Browse a two-column vendor picker, find plug-ins with instant search, and shape your chain with embedded editors, drag-and-drop, and one-click bypass.

Choose **zyvst_fx** for stereo effects, **zyvst_inst** for software instruments, or **zyvst_midi** for MIDI FX. Available for Apple Silicon and Intel Macs running macOS 13 or later.
