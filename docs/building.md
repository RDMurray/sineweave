# Building Sineweave

## Build on Windows

Install Visual Studio 2022 with **Desktop development with C++**, a Windows SDK,
CMake 3.24+, and Git. Dependencies are pinned and fetched on first configure;
subsequent builds can use the downloaded sources offline.

```powershell
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
.\build\Release\synth_benchmark.exe
.\build\Release\trajectory_benchmark.exe
```

The bundle is `build/Sineweave_artefacts/Release/VST3/Sineweave.vst3`.
Builds do not install into system directories. Add its parent directory to
REAPER's VST scan paths, or manually copy the **whole bundle** into your VST3
directory, then rescan. The Microsoft VC++ runtime may be required on another
machine. CI produces a Windows x64 bundle with licences and notices.
Install Inno Setup 6, then run `./scripts/package.ps1` to produce the installer
and package the bundle, documentation, licences,
and corresponding source (including the pinned dependency source). From the
archive's `Source` directory, use local paths `third_party/JUCE` and
`third_party/LORIS` with the CMake options below to build without fetching.

Local sources can replace fetching:

```powershell
cmake --preset windows-release -DSINEWEAVE_JUCE_PATH=C:/deps/JUCE -DSINEWEAVE_LORIS_PATH=C:/deps/loris
```

Use the exact pinned revisions in CMakeLists.txt. `core-only` builds the model,
Loris adapter, DSP tests and benchmark without JUCE. AVX2 is detected at runtime
on Windows x64; older CPUs use SSE2. Configure with
`-DSINEWEAVE_ENABLE_AVX2=OFF` to exclude that kernel entirely.

## Standalone Loris command-line tools

Build the pinned upstream CLI drivers separately with Windows x64 Release:

```powershell
cmake --preset loris-cli
cmake --build --preset loris-cli
```

To reuse the source already fetched for the plugin, add
`-DSINEWEAVE_LORIS_PATH=C:/Users/robbie/src/sineweave/build/_deps/loris-src`
to the configure command (adjust the absolute path for your checkout).
The executables are in `build/loris-cli/bin/Release`: `loris-analyze`,
`loris-synthesize`, `loris-dilate`, `loris-mark`, `loris-unmark`,
`loris-spewmarkers`, and upstream's experimental `loris-fastsynth`.
They link Loris statically, use the bundled FFT, and require no Python/FFTW.
The experimental engine is separate from the plugin's Loris library.

The upstream audio format is AIFF, and partials use SDIF (some tools also read
SPC). These tools do not read Sineweave plugin states. For example:

```powershell
.\build\loris-cli\bin\Release\loris-analyze.exe 40 80 input.aiff -ampfloor -80 -o partials.sdif
.\build\loris-cli\bin\Release\loris-synthesize.exe partials.sdif -rate 48000 -bw 0 -o resynth.aiff
```

Running an executable without arguments prints its usage (and normally exits
with status 1). `-bw 0` disables bandwidth/noise during synthesis.

