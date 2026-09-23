# Project-owned AxeFxControl

Source: tysonlt/AxeFxControl, commit
`2e51d11d4fc6cadd08dd0e437d905f0bad1947f7` (upstream v1.4;
upstream library metadata calls it 1.0.0). Original license is retained.
This directory replaces the downloaded PlatformIO dependency.

Local corrections:

- Copy preset/scene names using literal `%s` format strings.
- Terminate parsed names within the supplied buffer and preserve all 32 characters.
- Named scene enumeration does not replace the active scene; a confirmed scene
  number triggers a request for its current name.
- Current-preset queries reset the incoming record so external changes are accepted.
- Report completion once per request even when the same preset is reloaded unchanged.
- Explicitly request details after every outgoing preset change, even in passive mode.
- Process available MIDI bytes with a bounded, non-blocking streaming parser,
  including running status, interleaved realtime bytes and SysEx overflow recovery.
- Check minimum packet/payload sizes, bound effect dumps and outgoing packets.
- Size the receive buffer for a complete 50-effect dump (158 bytes).
- Use fixed-size arrays rather than variable-length or zero-length stack arrays.
- Search only received effects, so old slots cannot survive a smaller effect dump.

Native tests compile these actual sources against a minimal serial adapter:
`scripts\test_native.cmd`. Re-check these changes when upgrading upstream.
