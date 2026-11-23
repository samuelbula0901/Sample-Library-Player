# Piano Sampler (VST3, macOS)

Simple sampler plugin: click **Browse Library...**, pick the folder with your extracted
WAV files, and play. Includes volume, release, and an on-screen keyboard.

## Build

1. Install Xcode command line tools: `xcode-select --install`
2. Install CMake: `brew install cmake`
3. In this folder: `chmod +x build.sh && ./build.sh`

The first build downloads JUCE (needs internet) and takes a few minutes.
The plugin is copied to `~/Library/Audio/Plug-Ins/VST3/Piano Sampler.vst3`.
Rescan plugins in your DAW. A Standalone app is also built for quick testing.

## How samples are mapped

The folder is scanned recursively for .wav / .aif / .aiff / .flac.

- **Native format:** `<midiNote>-b<bank>-l<layer>.wav` (e.g. `021-b0-l00.wav` = A0, softest layer;
  `107-b0-l13.wav` = highest note, loudest layer). Layers are spread across velocity 1-127.
  Only the lowest bank (b0) is loaded; other banks are ignored.
- Otherwise, **note** is read from the filename: `C4`, `C#4`, `Db3`, `A0` (e.g. `Piano_C4_v2.wav`)
  or a MIDI number 21-108 (e.g. `piano_060.wav`). C4 = MIDI 60.
  If your files use C3 = middle C, set `kOctaveOffset = 1` in `PluginProcessor.cpp`.
- **Velocity layers**: several files on the same note become layers, ordered by filename
  and split evenly across velocity 1-127 (so name them softest -> loudest).
- Notes between samples are pitch-shifted from the nearest sample.
- If no filename has a note name, files are mapped in order, chromatically, from A0.

## Notes

- Samples are held in RAM as 32-bit float. Large multi-layer pianos can use several GB.
- The chosen folder is saved with your DAW project and reloaded when it opens.
- Sustain pedal (CC64) is supported.
