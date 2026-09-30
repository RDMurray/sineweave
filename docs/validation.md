# Validation record — 30 September 2026

This is a working 0.2 trajectory-playback candidate. The actual screen-reader acceptance gate is
**unverified**, so this record does not certify an accessible public release.

## Build and automated tests

Windows x64, Visual Studio 2022 Community, MSVC 19.44, Windows SDK 10.0.26100.0,
CMake 4.2.0. JUCE 8.0.15 and Loris at the pinned revisions in CMakeLists.txt.
Loris compiled as a static library and passed a synthetic 613.7 Hz analysis
before the JUCE plugin was implemented. No replacement tracker was used.

`cmake --build --preset windows-release` succeeded. `ctest --preset
windows-release` passed **3/3** tests:

* `loris_smoke`: real Loris synthetic-tone detection.
* `core_tests`: inharmonic mixture analysis; frame frequency/amplitude/cosine
  phase equivalence against Loris; phase wrap, endpoints, silence, invalid range;
  deterministic partial selection; truncated/malformed state; exact arbitrary
  pitch ratio and rendered cosine; velocity, attack/decay/sustain/release, pedal,
  bend, quietest-releasing/oldest-active stealing, alias suppression at two
  sample rates; AVX2/SSE2 equivalence; bounded publication/retirement backpressure
  and repeated concurrent replacement.
* `plugin_tests`: worker WAV decoding and defaults; numeric keyboard activation
  for all 20 numeric controls; accessibility handler metadata; sample-offset MIDI and
  stereo duplication; superseded analysis/restoration; state replay after
  deleting the source file; missing-file/invalid-position failure retention;
  repeated replacement, oversized-state budget admission, and empty-state restoration.

Sustain-preview additions passed the same suites: original-frequency audition
with fractional reference pitch and bend; automatic release; repeated replacement
and stop fades without audio-thread allocation; preservation of MIDI voices;
one-shot frame adoption; editor toggle/close; silence during playback, recording
and offline rendering; and no audition on initial state restore. These preview
checks are automated; the actual screen-reader release gate remains unverified.

Trajectory additions pass both the Windows plugin build and the separate core-only
SSE2 build. Tests cover exact forward/reverse chirp phase and amplitude, phase
interpolation against the frame sampler, AVX2/SSE2 chirp equivalence, independent
half speed, 50 ms speed ramp and zero hold, per-note playheads, fractional root and
channel bend, A4/A5 transposition, births/deaths and gaps, deterministic peak/ID
lane selection and reuse, fixed normalization for disjoint peaks, wrap/ping-pong
intro/boundaries, short loops and crossfades, automatic one-shot release, pedal,
stealing, configuration capture, lease pinning after replacement, full return
queue, tiny compilation budgets, source-free trajectory state restore, invalid
range retention/save, repeated partial-limit replacement, and V1 state migration.
Sample-rate suppression is checked at 32/48/96 kHz in trajectory mode. Guarded
trajectory rendering, stealing and queue saturation report zero new/delete calls.

Global C++ new/delete instrumentation, including aligned allocation, reports
zero allocation **and deallocation** in guarded synthesis/processBlock and
audio-side frame adoption. Source inspection covers locks, file operations,
analysis and UI callbacks. The probe does not intercept arbitrary direct calls
to C malloc/free; it is not a general-purpose third-party profiler.

MSVC warns that the deliberately cache-line-aligned SPSC queues contain padding
(C4324). This is expected. A separate core-only Release build using local Loris
source with AVX2 disabled also passed both tests. Debug builds and the hosted GitHub CI run have not
been independently verified in this session.

## Standalone Loris CLI build

The separate `loris-cli` preset compiled all seven pinned upstream drivers as
Windows x64 Release executables with MSVC: analyze, synthesize, dilate, mark,
unmark, spewmarkers, and experimental fastsynth. They link the static Loris core,
with the fast engine in a separate library; Python and FFTW are not required.
Upstream source was not modified. Optional CLI targets avoid upstream Unix
compiler/linker flags and leave the plugin configuration unchanged.

The upstream flute AIFF was analysed at 40 Hz resolution / 80 Hz window and
−80 dB floor into 3,783 SDIF partials. Both synthesizers successfully produced
audible mono 16-bit AIFF at 48 kHz with bandwidth disabled. Marker insertion,
listing, dilation, and removal passed on the generated SDIF. Build output is
`build/loris-cli/bin/Release`; commands are documented in README.md.

## Real REAPER host check

REAPER 7.77 x64 loaded the VST3 from the build directory in an isolated config,
enumerated the stable instrument parameters, accepted a saved analysis, and
saved a project with MIDI A4 and A5 notes. Reopening that project for offline
rendering produced a four-second 48 kHz stereo 24-bit WAV. The analysis source
path was already deleted. The output channels matched exactly; peak amplitude
was 0.017289639, with these measured sinusoidal components:

| MIDI segment | Frequency | Measured amplitude |
| --- | ---: | ---: |
| A4, 0.5–1.0 s | 613.7 Hz | 0.009866 |
| A4, 0.5–1.0 s | 947.3 Hz | 0.007420 |
| A5, 2.5–3.0 s | 1227.4 Hz | 0.009863 |
| A5, 2.5–3.0 s | 1894.6 Hz | 0.007422 |

The wrong-octave components in those windows were below 0.0001. This verifies
host MIDI, non-harmonic transposition, audible output and project sound restore.
It does not measure live audio-device underruns. The optional harness is
`tests/reaper_host_check.lua`; `tests/verify_reaper_render.py` checks its WAV.

For the trajectory candidate, a second isolated REAPER project used embedded
613.7→813.7 Hz and 947.3→1047.3 Hz chirps, half speed, wrapping-loop settings,
and a nonexistent source path. REAPER exposed all 24 instrument parameters and
saved/reopened the project. Its 48 kHz stereo 24-bit render passed
`tests/verify_reaper_trajectory.py`: matched chirp projections measured 0.019860
and 0.014890 for A4, and 0.019872 and 0.014910 for A5. The half-pitch projection
was below 0.001. This verifies evolving frequency, independent half speed,
octave transposition and embedded-state reconstruction in the actual host.
Loop edge/reflection/overlap coverage is automated DSP coverage; this short host
fixture does not traverse its loop. Live monitored playback and device underruns
have not been measured.
Use a scratch REAPER resource directory/config. Generate its state fixture with
`plugin_tests <absolute directory>`, set the VST scan path in that config, run
the Lua harness, then render the saved acceptance.rpp. REAPER's `vst_chunk`
wrapper is included in the generated state.base64.

## Release DSP benchmark

Intel Core Ultra 7 165U (12 cores, 14 logical processors), runtime AVX2 kernel,
48 kHz, 16 held notes, 200 callbacks per buffer size. This is elapsed callback
time divided by available audio time on a general-purpose Windows desktop;
background scheduling, thermal/power policy and tail activity affect results.
Dense fixtures include partials attenuated by the sample-rate safety fade.

| Partials/voice | Buffer | Mean ms | Max ms | Realtime load | Deadline misses |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 4,096 | 128 | 2.689 | 6.129 | 100.85% | 43/200 |
| 4,096 | 256 | 4.293 | 7.459 | 80.49% | 8/200 |
| 4,096 | 512 | 8.594 | 11.800 | 80.57% | 5/200 |
| 1,024 | 128 | 0.593 | 1.139 | 22.25% | 0/200 |
| 1,024 | 256 | 1.198 | 3.082 | 22.46% | 0/200 |
| 1,024 | 512 | 2.146 | 3.922 | 20.12% | 0/200 |
| 256 | 128 | 0.160 | 0.433 | 6.01% | 0/200 |
| 256 | 256 | 0.295 | 1.052 | 5.54% | 0/200 |
| 256 | 512 | 0.598 | 1.381 | 5.60% | 0/200 |

These are callback **deadline misses**, not observed device underruns. Maximum
detail is not reliably realtime on this machine. Lower the partial/voice limits
or freeze dense tracks. The defaults remain the agreed 16 voices/4,096 partials;
typical small analysed frames render far fewer oscillators. Reproduce with
`build/Release/synth_benchmark.exe`; append any argument to force the SSE2 path.

## Trajectory Release benchmark

Same Intel Core Ultra 7 165U machine, Windows x64 Release AVX2, 48 kHz,
16 voices and 200 timed callbacks per row. The fixture linearly changes both
frequency and amplitude. The loop-overlap rows synchronize all notes at a wrap
boundary in every timed callback; untimed priming/retriggering is excluded.
Its 20 ms loop automatically shortens the requested 20 ms crossfade to 1.25 ms.
These deliberately dense cases include sample-rate-suppressed upper partials.

| Partials/voice | Fixture | Buffer | Mean ms | Max ms | Realtime load | Deadline misses |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 4096 | changing trajectories | 128 | 14.836 | 24.016 | 556.37% | 200/200 |
| 4096 | changing trajectories | 256 | 29.412 | 59.154 | 551.48% | 200/200 |
| 4096 | changing trajectories | 512 | 59.014 | 109.588 | 553.26% | 200/200 |
| 4096 | loop overlap | 128 | 28.648 | 37.952 | 1074.31% | 200/200 |
| 4096 | loop overlap | 256 | 45.974 | 60.405 | 862.01% | 200/200 |
| 4096 | loop overlap | 512 | 77.886 | 113.318 | 730.18% | 200/200 |
| 1024 | changing trajectories | 128 | 3.744 | 5.003 | 140.41% | 200/200 |
| 1024 | changing trajectories | 256 | 7.970 | 15.645 | 149.44% | 200/200 |
| 1024 | changing trajectories | 512 | 16.600 | 32.376 | 155.63% | 200/200 |
| 1024 | loop overlap | 128 | 8.033 | 11.992 | 301.24% | 200/200 |
| 1024 | loop overlap | 256 | 11.433 | 20.633 | 214.37% | 200/200 |
| 1024 | loop overlap | 512 | 20.359 | 32.996 | 190.86% | 200/200 |
| 256 | changing trajectories | 128 | 1.046 | 1.529 | 39.24% | 0/200 |
| 256 | changing trajectories | 256 | 2.158 | 4.327 | 40.45% | 0/200 |
| 256 | changing trajectories | 512 | 4.401 | 9.402 | 41.26% | 0/200 |
| 256 | loop overlap | 128 | 1.983 | 3.427 | 74.36% | 3/200 |
| 256 | loop overlap | 256 | 2.943 | 5.531 | 55.18% | 1/200 |
| 256 | loop overlap | 512 | 4.904 | 6.547 | 45.98% | 0/200 |

The 16 × 4,096 maximum does not fit this machine in Trajectory mode. Neither
does 16 × 1,024. At 16 × 256, ordinary changing trajectories met all measured
deadlines; synchronized overlap missed 3/200 at 128 samples and 1/200 at 256.
Reduce partial/voice limits or increase the buffer, and measure on the actual
host/device. These are CPU callback deadline misses, not measured audio-device
underruns. The agreed maximum/default limits remain available. Reproduce with
`build/Release/trajectory_benchmark.exe`; any argument forces the SSE2 kernel.
Raw output is in `docs/trajectory-benchmark.txt`. The preceding frozen-frame
table records the prior candidate; its DSP path is retained.

## Keyboard-only NVDA/OSARA release gate

The prior frozen-frame build's actual Windows accessibility tree exposed labelled load/analyse
buttons, channel choice, all 14 numeric sliders and the readable status document.
Programmatic tests confirm Enter/F2 numeric editing activation. NVDA and OSARA
are installed on the test machine, but a complete spoken, keyboard-only
walkthrough **has not been performed**. The computer-use helper could inspect
the floating editor but failed to address its owned-window controls for input.
Accessibility metadata inspection cannot substitute for listening to NVDA.

Before release, a person using the actual screen reader must record results for:

1. Opening the plugin and reaching controls in logical Tab/Shift+Tab order.
2. Activating the native chooser and selecting a WAV without a mouse.
3. Selecting mono channel and entering original-file start/end values.
4. Running analysis and reading busy/completed/error status.
5. Entering sustain time and hearing the newly prepared sound on new notes.
   Verify the automatic short preview while stopped and the checkbox's Space activation.
6. Reading and setting fractional reference pitch with MIDI number/note name.
7. Adjusting gain, ADSR, voice/partial limits and bend range from the keyboard.
8. Reading excluded-partial and invalid-range messages in the status field.
9. Saving/reopening a REAPER project and playing after moving the source file.
10. Locating the original file and explicitly re-analysing after reopening.
11. Selecting Trajectory/Frozen frame, Forward/Reverse, and Off/Wrap/Ping-pong
    with keyboard combo controls, including scroll/focus visibility.
12. Setting independent speed (including zero), playback and loop bounds, and
    crossfade numerically; reading invalid-range errors without automatic speech.
13. Hearing forward/reverse evolution, loops and speed changes on held notes;
    verifying direction/loop changes apply to new notes and note-off/pedal work.
14. Saving/reopening trajectory settings and playing without the original source.

## Resource and lifecycle limits

Selected mono PCM is capped at 32 MiB. The worker applies a 256 MiB per-instance
admission budget including retained model/vector capacities, cached state,
queued restoration bytes, new model/serialization copies, all retained trajectory
program capacities, and a 32 MiB fixed reserve for synth/overlap banks, bounded
queues and validation overhead. Analysis reserves
four times the selected PCM bytes for input/Loris work and gives conversion a
remaining output allowance with serialization headroom. State/frame preparation
rejects requests whose estimated working set exceeds the available budget.
Tests also exercise a deliberately tiny Loris output allowance. No trajectories
are silently dropped to satisfy limits. Loris internal temporary allocations
and host-owned state buffers are not subject to an OS-enforced process cap;
headroom estimates are conservative policy rather than exact allocator tracking.
Large-analysis peak memory remains a limitation requiring further profiling.
Restore/frame preparation is asynchronous; wait for Ready before first playback
or an offline render. Live-device underrun measurement and the screen-reader
walkthrough remain open acceptance work.
