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
