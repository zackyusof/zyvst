# Source

The matching source archive accompanies the binaries on the v1.0.0 release.
It includes the exact JUCE revision used to build the Audio Units, JUCE's
third-party notices, and the build workflow. The repository also records
the JUCE revision as a Git submodule.

Build with CMake 3.22 or newer and the macOS Xcode toolchain.

```sh
# For a Git checkout, populate the dependency first.
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --parallel 2 --target HybridFxA_AU HybridInstA_AU HybridMidiA_AU
```

See LICENSE and NOTICE for licensing and attribution.
