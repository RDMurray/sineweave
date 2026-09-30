# Sineweave technical plan

Sineweave is a Windows x64 MIDI additive instrument. V1 extracts one sustained
spectral frame from arbitrary partial trajectories; it does not impose harmonics.
Version 0.2 also follows their evolution with independent speed, direction and loops.

## Analysis choice and licences

Reviewed on 29 September 2026:

* [Loris upstream](https://github.com/kellyfitz/loris), pinned at
  `eccd42ac847b08769643b0d0c36442ef0fd27f38`: C++17, CMake, reassigned
  bandwidth-enhanced partial analysis, frequency/amplitude/phase/bandwidth
  trajectories. Upstream CI covers Linux/macOS, not MSVC. Best native fit.
* [SiMPL](https://github.com/johnglover/simpl): GPLv3 C++/Python research API;
  FFTW and GSL dependencies complicate Windows integration.
* [SMS Tools](https://github.com/MTG/sms-tools): AGPLv3 Python/Cython package;
  embedding Python is unnecessary for this C++ plugin.

Use Loris under its GPLv2-or-later grant, exercising GPLv3 compatibility with
AGPLv3. JUCE 8.0.15 uses its AGPLv3 open-source route. Project code is AGPLv3.
Dependency notices and original licences accompany source/binary distributions.
No commercial JUCE licence, external FFTW, or Python runtime is required.

Before plugin implementation, compile a static Loris target and analyse a
synthetic tone under MSVC. The local target uses the pinned upstream core source
list, omits procedural/fast-synth utilities, GNU warning flags, and Unix `m`
linkage on Windows. This changes integration/build settings, not tracking DSP.

## Boundaries and phase

The core model and synth have no JUCE/Loris dependency. An analyser adapter
converts Loris tracks to contiguous breakpoint vectors with stable IDs and
absolute source times. Frame sampling linearly interpolates frequency,
amplitude, and bandwidth and integrates linear frequency from the preceding
breakpoint to advance phase. Phase is cosine phase in radians, wrapped to
[-pi, pi). Frames exclude times outside a partial's lifetime. This convention
matches Loris and is the foundation for later transient handover.

Optional source audio/residual sections are reserved. Future editing creates
new model/frame/program versions. Sample/additive handover can use the same
absolute time, partial IDs, and phase convention; residuals and handover remain
unimplemented.

## Trajectory program and phase/time contract (0.2)

The worker compiles an immutable program with indexed track data and bounded
oscillator-lane intervals. Birth/death events update a peak-amplitude-ranked
active set; stable IDs break ties. Surviving tracks keep their lane; vacant lanes
are reused. No sampling vector, sorting, or trajectory copying occurs in render.
All original trajectories remain in the embedded model. A sweep of the selected
piecewise-linear amplitude slope/jump events finds the exact peak sum before
birth/death fades; one fixed attenuation of 1/max(1, peak) applies to the program.

Each note copies validated region/direction/loop/crossfade settings and leases
its immutable program. The playhead moves by speed/output sample rate, while
oscillator phase advances by signed frequency times MIDI/bend ratio/output
sample rate. These clocks are independent: half-speed evolution keeps the same
pitch, and zero freezes timbre but continues oscillation. Reverse has negative
phase advance. Analysis cosine phase initializes newly entering tracks; live
gain, root, bend, and speed edits do not reset the ongoing bank's phases.
Speed ramps linearly over 50 ms; pitch retains the existing smoothing.

Rendering splits into tiles up to 64 samples, shortened to 16 during pitch/speed
smoothing and split at trajectory/lane/playback boundaries. Within a tile,
frequency and amplitude are linear; double-precision quadratic integrated phase
seeds AVX2/SSE2 complex chirp recurrences. Average speed and pitch during a short
tile approximate their smoothed curves. Source boundary overshoot is bounded
to one output sample, with the playhead clamped/wrapped/reflected immediately.
Births and lane replacement use 5 ms fades; loop-entry banks rely on their overlap
crossfade, so repeated tiny loops do not repeatedly restart a 5 ms birth fade.
Lifetime exits fade when they occur
inside the program; a playback-end hold retains the last bank for ADSR release.

Every voice and stealing tail has a preallocated overlap bank. On a loop edge,
the old bank continues oscillating at its boundary timbre while the incoming
bank enters at analysis phase. Linear crossfade coefficients sum to one. With
zero crossfade, tracks that remain assigned keep phase through wrap/reflection.
The duration is min(requested duration, loop length / (4 * 4)) seconds: a
conservative quarter traversal at the maximum supported speed ensures a later
speed edit cannot exhaust the two-bank overlap capacity. Sub-sample durations
become zero. Ping-pong direction changes and wrapping jumps affect source time;
neither multiplies audible pitch. Noise/bandwidth synthesis is absent.

## Realtime ownership

One serial worker owns decoding, analysis, state restoration, trajectory
sampling, sorting, normalisation, and immutable prepared frames. A bounded
single-producer/single-consumer pointer queue publishes frames. The audio thread
adopts at most one per block, and sends the previous frame through a retirement
queue. If retirement has no space, publication is deferred. Only the worker
deletes retired frames. Each voice copies a prepared frame into preallocated
structure-of-arrays storage on note-on; existing voices retain their sound.

Trajectory notes and their stealing tails instead hold an atomic program lease.
Audio sends lease-return tokens through a bounded 64-slot SPSC queue, then drops
the reference without deleting. A worker-owned registry, reserved/bounded to 64
programs, checks reference counts and deletes only unused programs. If token
capacity is exhausted by a dense MIDI burst, registry scanning on the next worker
cycle safely completes retirement. Tokens are never dereferenced after return.
Prepared frames and the worker's current program also hold leases. Frame-queue
backpressure defers adoption; program/budget admission failure keeps the old
playable configuration. Shutdown stops the worker, releases audio/frame leases,
and finally destroys the program registry. Render-path allocation/reclamation
instrumentation covers replacement, overlap, stealing, and full return queues.

The render path uses an interpolated cosine lookup table to seed SIMD oscillator
recurrences, sample-offset MIDI, per-channel bend
and pedal, envelopes, and smoothed pitch/gain. It performs no allocation,
deallocation, locks, I/O, analysis, or UI callbacks. Stealing uses a bounded pool
of preallocated 5 ms fade tails. Timbre parameter changes are coalesced and
prepared asynchronously; expensive re-analysis requires an explicit command.

Sustain-position preparation can mark a frame for one-shot audition on audio
adoption. Preview uses two additional preallocated banks: one short-lived voice
and one 5 ms replacement tail. It plays at ratio one independently of fractional
reference pitch, MIDI bend, pedal and the MIDI voice limit, with fixed 10 ms
attack/250 ms gate (including attack)/50 ms release and normal master gain. It is gated by the editor
toggle and suppressed while the host plays, records or renders offline. Closing
the editor fades it out. Its request marker is transient and is not serialized;
restore/initial analysis never starts audition. Position changes superseded
during preparation are discarded; audio also checks the selected position before
starting a queued preview. Publication and reclamation keep the existing bounded
ownership contract.

The bank uses double phase in cycles and caches rotation constants in preallocated
arrays. Recurrences are re-seeded every 256 samples, or every 16 during pitch
smoothing, bounding float drift. MSVC x64 includes an AVX2 translation unit;
CPUID, OSXSAVE and XGETBV checks select it at construction. The rest of the core
uses baseline SSE2, so unsupported CPUs never execute AVX2. Mean pitch over each
short smoothing tile advances phase continuously; pitch modulation within that
tile is approximate. The algorithm never resets phase for a gain/root/bend edit.

Worker results are prepared and validated before a generation-checked commit.
Commit and request generation changes share the non-realtime worker mutex, so
superseded results cannot become the model used by a later frame request. The
last successful model/frame survives failures. Restores use the embedded prepared
frame even if saved controls describe an invalid or still-pending frame request.

## State, resource limits, accessibility

Versioned binary model state stores all tracks, metadata, analysis settings, and
the prepared frame; JUCE parameter XML is stored alongside it. Paths are metadata,
not required for replay. Validate finite values, ordering, counts and a 256 MiB
analysis/state budget before publication; report failures without replacing the
last working timbre. Per-instance admission accounts for retained vector
capacities, cached/queued state, candidate data/serialization and fixed render
storage. Temporary internal Loris allocation uses conservative PCM headroom and
a remaining conversion allowance; this is not an OS-enforced process memory cap.
The fixed reserve is now 32 MiB for oscillator/overlap banks, bounded queues and
validation overhead. Every retained program's track/breakpoint/interval capacities
count against admission, including programs pinned by old voices. Candidate lane
and normalization working storage is checked before publication. Budgets reject
replacement rather than truncating saved trajectories.

The binary model format remains SWV1/version 1; the outer SWP1 stream contains
parameter XML plus this complete model. Old 15-parameter states are migrated to
24 parameters with Frozen frame/1×/Forward/Off/20 ms defaults and regions from
embedded analysis. Playback programs are reconstructed on the worker. Invalid
playback edits are replaced by the last valid prepared configuration in saved
XML, preserving the sound on reopening. Parameter IDs remain stable; appended
IDs are speed, playStart, playEnd, loopStart, loopEnd, crossfade, playMode,
direction, loopMode. No automatic speech or live-region announcement is added.

Host parameters cover numeric settings; accessible standard JUCE controls cover
file selection and analysis commands. Numeric time entry avoids a waveform
requirement. A native Windows chooser and readable status complete the workflow.
Actual REAPER + NVDA/OSARA keyboard acceptance is a release gate separate from
automated DSP/build tests.
