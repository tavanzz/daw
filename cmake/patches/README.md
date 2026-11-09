# Patches to dependencies

`cmake/Dependencies.cmake` applies these to the downloaded sources through
`cmake/ApplyPatch.cmake`: exactly (no fuzz) or not at all. The configure stops with a
message when a patch no longer applies, and when the sources a build uses lack it (a local
JUCE given with `FETCHCONTENT_SOURCE_DIR_JUCE`, say). Each patch begins with what it changes
and why.

| Patch | Dependency | Platforms | Reported upstream |
|-------|------------|-----------|-------------------|
| `juce-coreaudio-output-only.patch` | JUCE 9.0.4 | macOS | not yet: the report is below |

## At every update of a patched dependency

1. Does the new version fix what the patch is for? Read its changes to the patched file
   and the upstream report. If it does, delete the patch, its lines in `Dependencies.cmake`
   and what CLAUDE.md and `docs/THIRD_PARTY.md` say about it.
2. If not, and the configure stops, make the patch again: extract the new sources twice
   into `a/` and `b/`, make the change in `b/`, then
   `diff -u a/<file> b/<file> | sed -E '1,2s/\t.*$//'` and put the header back on top.
   JUCE's files have CRLF line endings: keep them (`.gitattributes` keeps git from
   converting the patch).
3. Check that it still works. For the JUCE patch: CLAUDE.md, Microphone permission (the
   app plays to an interface with inputs and tccd's log shows no request with
   `preflight=no` until a track needs an input).

On 2026-11-09 JUCE's `develop` branch had the same `juce_CoreAudio_mac.cpp` as 9.0.4, and
no issue about it was open.

## Report to JUCE

For https://github.com/juce-framework/JUCE/issues (Bug report), until it is filed.

**Title:** CoreAudio: playing to an interface that has inputs makes macOS ask for the
microphone

**Detailed steps on how to reproduce the bug**

1. A JUCE 9.0.4 GUI app that may record (`MICROPHONE_PERMISSION_ENABLED TRUE`; hardened
   runtime with `com.apple.security.device.audio-input`), whose microphone permission is
   undecided (`tccutil reset Microphone <bundle id>`).
2. Open an audio interface that also has inputs (ours: Native Instruments Komplete Audio 6)
   for output only: `AudioDeviceSetup` with that `outputDeviceName` and an empty
   `inputDeviceName`.

macOS asks "<app> would like to access the microphone" as the device starts, and the start
waits for the answer. tccd's log (`log show --predicate 'process == "tccd"'`) shows a
`kTCCServiceMicrophone` request with `preflight=no`.

macOS makes this request whenever an IOProc is created on a device that has input
streams, used or not (the device itself or AUHAL with input disabled alike). JUCE opens
the output through a private aggregate device, which carries the interface's input
streams. Making that aggregate stacked when it holds one device used for its output only
(as a Multi-Output Device is) leaves it with output streams only, and macOS asks nothing:

```cpp
// AggregateDeviceDescription::rebuild(), after the two addDevice() calls
outputOnly = ! ioDevices[toUnderlyingType (PlaybackDirection::input)].isValid()
             && devices.getArray().size() == 1;

// AggregateDeviceDescription::toDictionary()
description.setInt (kAudioAggregateDeviceIsStackedKey, outputOnly);

// AggregateDeviceDescription's members
bool outputOnly = false;
```

**What is the expected behaviour?** No microphone request while no input is opened: an
app that only plays never asks, and one that records asks when it opens an input.

**Operating systems:** macOS. **Versions:** macOS 15.6.1. **Architectures:** Arm64.
**`develop`:** not tested; its `juce_CoreAudio_mac.cpp` is the same as 9.0.4's.
