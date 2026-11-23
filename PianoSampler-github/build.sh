#!/bin/bash
# Builds Piano Sampler (VST3 + Standalone) on macOS and installs the VST3.
# Requires: Xcode command line tools (xcode-select --install) and CMake (brew install cmake)
set -e
cd "$(dirname "$0")"

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j

# Ad-hoc sign so macOS / your DAW will load it
VST3="$HOME/Library/Audio/Plug-Ins/VST3/Piano Sampler.vst3"
if [ -d "$VST3" ]; then
    codesign --force --deep -s - "$VST3"
    echo "Installed and signed: $VST3"
fi

echo "Standalone app (for quick testing): build/PianoSampler_artefacts/Release/Standalone/"
