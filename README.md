# Sineweave

An open-source Windows VST3 MIDI instrument that analyses an audio sample into
arbitrary sinusoidal partial trajectories and plays a selected sustain frame or
follows the changing trajectories with independent speed and MIDI pitch.
Inharmonic frequencies are preserved: a 613.7 Hz partial at reference A4 becomes
1227.4 Hz at A5. Project code is **AGPLv3**.

## Install on Windows

[Download the latest Windows x64 installer](https://github.com/RDMurray/sineweave/releases/latest/download/Sineweave-Windows-x64-Setup.exe)

Run the installer with administrator permission, then rescan plugins in your
audio host. It installs the complete bundle into
`C:\Program Files\Common Files\VST3\Sineweave.vst3` and documentation/licences into
`C:\Program Files\Sineweave`. Close your audio host before installing an update.
Run the newer installer to upgrade; remove Sineweave through Windows Installed
Apps to uninstall. The shared Microsoft VC++ runtime is retained.

The installer includes the Microsoft VC++ x64 runtime for offline installation.
Sineweave's installer is unsigned; Windows may display an unknown-publisher or
SmartScreen prompt. Windows 10/11 x64 is the supported target.

Every passing push to `main` publishes a separate
[GitHub release](https://github.com/RDMurray/sineweave/releases) with the installer,
a portable ZIP including corresponding project/dependency source, and SHA-256
checksums. The download above follows the newest successfully released commit.

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

## Keyboard and screen-reader workflow

1. Insert Sineweave as an instrument on a MIDI track. Open its plugin editor.
2. Tab to **Load audio file...**, activate it with Space, and choose a file in
   the native Windows dialog. WAV, AIFF, FLAC and Ogg are supported by the basic
   JUCE readers; Windows system codecs may provide additional formats. The
   chooser also accepts MP3, but codec availability is not guaranteed.
3. Loading sets analysis start to zero, end to the file duration, and sustain
   position to the midpoint. Select **Mono mix**, **Left**, or **Right**.
   Stereo/multichannel files use the first two channels; mono files use their
   sole channel for all selections.
4. Edit numeric **Analysis start/end** and **Sustain position** in seconds from
   the original file start. An end value of zero means the file end. Keep the
   sustain position inside the selected range. No waveform is needed.
5. Activate **Analyse / Re-analyse**. The focusable, read-only **Analysis status**
   field shows busy, completion, errors, and partial counts for on-demand reading.
   Analysis resolution and amplitude-floor changes take effect on re-analysis.
6. Set **Reference pitch** (default MIDI 69, A4) to the pitch corresponding to
   the sample. Fractional MIDI note values allow tuning. Play MIDI notes.
7. Adjust gain, ADSR, polyphony, partial limit, or pitch-bend range through
   the editor or REAPER's host parameter/automation interface.
8. Choose **Trajectory** in **Playback mode** to play through the tracked sound.
   Set **Playback speed** (0–4×), **Initial direction**, and **Loop mode** (Off,
   Wrap, Ping-pong). Playback and loop start/end are original-file seconds;
   successful analysis initializes both regions to the analysed range.

Controls are standard accessibility-aware JUCE sliders, editable value fields,
buttons, and combo boxes. Use Tab/Shift+Tab and arrow keys; the controls scroll
into view as focus moves, and status remains visible. On a numeric slider,
press Enter or F2, type its value, and press Enter to commit (Escape cancels).
Root pitch text includes the MIDI number and note name; JUCE's octave convention
is used (REAPER's octave display offset may differ). Sustain is shown as percent.
If REAPER intercepts a key, enable its option to send keyboard input to the plugin.
File selection/analysis are explicit editor commands; numeric settings are host
parameters. There is no host parameter capable of holding an arbitrary file path.

**Preview sustain changes** is enabled when the editor opens. Changing sustain
position plays the newly prepared resynthesis frame for about 300 ms at its
original frequencies (the exact reference pitch), through the master gain. The
preview has a 10 ms attack and 50 ms release and uses a dedicated voice, so it
does not steal MIDI notes. Rapid changes replace the preview with a short fade.
Uncheck the control with Space to disable it. Preview is suppressed during host
playback, recording and offline rendering, and stops when the editor closes.
Opening the editor, restoring state, or running analysis does not trigger it.
The host must process/monitor the plugin while stopped for previews to be audible;
in REAPER enable **Run FX when stopped** if needed. This editor toggle is not a
saved host parameter.

## Behaviour and limits

**Trajectory playback:** every MIDI note has its own playhead. Forward starts
at playback start; reverse starts at playback end. At 0.5×, the timbre evolves
twice as slowly with the same audible pitch. Zero holds the current timbre while
oscillators continue sounding. Speed edits affect held notes with a 50 ms ramp.
Mode, direction, regions, loop mode, and crossfade are prepared asynchronously
and captured by new notes; held notes retain their configuration and program.

Forward playback traverses the introduction before entering a loop; reverse
traverses the section above the loop. Wrap jumps to the opposite boundary;
Ping-pong reverses direction. Loop crossfade defaults to 20 ms (0–100 ms),
shortened conservatively to a quarter traversal at the maximum 4× speed to keep
overlap bounded. An outgoing bank holds the boundary timbre while a new bank
enters the next leg. With crossfade zero, continuing partials retain their phase.
Loop ranges must be inside the playback range and at least 1 ms. Invalid edits
retain the last prepared configuration, which is also the configuration saved
with the sound. Read status after editing. Note-off releases while the playhead
continues; sustain pedal delays that release. With looping Off, reaching the
playback boundary freezes the last timbre and starts release automatically.

* Default maximum detail is **16 voices and 4,096 strongest active partials per
  voice**. This is a rendering limit, not a harmonic model or analysis truncation.
  All tracked trajectories are retained in saved state. Rendering counts and
  excluded weaker partials are shown in the status field.
* Changing the sustain position or partial limit prepares a frame on the worker,
  typically on its 20 ms polling cycle. Existing notes keep their copied timbre;
  new notes use the prepared frame. Timbre automation is asynchronous and is not
  sample-accurate. Gains/pitch are smoothed; MIDI events retain block sample offsets.
  In Trajectory mode, a partial-limit edit also prepares a new lane schedule.
  Lanes prioritize active trajectories by their peak amplitude with stable-ID
  tie-breaking, and are reused across non-overlapping lifetimes. All trajectories
  remain saved. Rendering-limit warnings are readable status text, without speech.
* The cosine oscillator bank keeps double-precision phase, uses SIMD recurrence
  periodically re-seeded from an interpolated lookup table, and fades partials
  between 0.45 and 0.49 times the output sample rate. Above that band they are muted.
* Each frame is scaled down only if the sum of partial amplitudes exceeds one.
  Trajectory programs use one fixed scale from peak summed rendered amplitudes,
  preserving amplitude evolution without frame-by-frame gain pumping.
  Velocity is linear, master gain defaults to −24 dB, and there is no limiter.
  Higher gain or coincident stealing tails can overload the output.
* Pitch bend is per MIDI channel; sustain pedal, all-notes-off, all-sound-off,
  and reset-controllers are handled. MPE and MIDI 2.0 remain future features.
* Save/restore embeds the trajectories, metadata and prepared sustain frame.
  The original file is unnecessary for playback, but needed for re-analysis.
  Restore is asynchronous; wait for **Ready** before playing/rendering. Failed
  loading, analysis or state validation retains the last successful timbre.
* A loaded replacement file does not replace the old timbre until analysis
  succeeds. Saving while a sustain request is invalid preserves the last prepared
  frame. Reopening reproduces that frame; changing sustain position then resamples.
* Source files may be up to 24 hours; selected decoded mono PCM is limited to
  **32 MiB** (about 87 seconds at 48 kHz) to reserve analysis headroom. Model/state
  size is bounded at **256 MiB**. Per-instance admission also accounts for old/new
  models, cached state, serialization and conservative Loris working headroom.
  This is not an OS-enforced process memory cap. Oversized requests are rejected;
  select a shorter range.
* Resynthesis is mono duplicated to stereo. Bandwidth/noise metadata is saved
  but its noise contribution, partial editing, morphing
  and transient/sample handover are not synthesised yet.

## Validation and performance

See [docs/validation.md](docs/validation.md) for actual checks, measured benchmark
results and the REAPER + NVDA/OSARA acceptance status. Dense 16 × 4,096-partial
patches exceed real time on some machines; lower polyphony/partial limit or freeze
the track when necessary. The benchmark reports callback deadline misses; it does
not pretend to measure audio-device underruns.

Tests cover Loris inharmonic analysis, frame phase equivalence, transposition,
envelopes, pedal/bend/stealing, alias suppression, state validation/replay, stale
publication/backpressure, and allocation/deallocation instrumentation on the
render path. An optional isolated REAPER host harness is in
`tests/reaper_host_check.lua`; fixture generation uses
`plugin_tests <absolute fixture directory>`.
Create `trajectory-mode.txt` in that fixture directory to select the trajectory
chirp fixture; verify its reopened render with `tests/verify_reaper_trajectory.py`.
Legacy V1 states load with the new controls defaulted to Frozen frame. New states
embed the analysis and reconstruct playback programs without the original file.

Architecture and future phase/time contracts are documented in
[docs/technical-plan.md](docs/technical-plan.md). Licences and dependency notices
are in LICENSE, THIRD_PARTY_NOTICES.md, and licenses/.
