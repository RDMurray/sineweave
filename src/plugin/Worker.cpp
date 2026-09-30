// SPDX-License-Identifier: AGPL-3.0-only
#include "Worker.h"
#include "analysis/Analyser.h"
#include <chrono>
#include <stdexcept>
namespace sineweave {
namespace {
std::size_t stateBytes(const SoundState &s) {
    std::size_t bytes = sizeof(s) + s.sound.sourcePath.capacity() +
                        s.sound.partials.capacity() * sizeof(PartialTrajectory) +
                        s.frame.partials.capacity() * sizeof(SpectralPartial);
    for (const auto &t : s.sound.partials)
        bytes += t.points.capacity() * sizeof(PartialBreakpoint);
    return bytes;
}
PlaybackSettings settings(const std::array<float, 8> &input, const AnalysedSound &sound) {
    return validatePlayback({input[0], input[1], input[2], input[3], input[4], input[6] == 0 ? 1 : -1,
                             static_cast<LoopMode>(static_cast<int>(input[7]))},
                            sound.startSeconds, sound.endSeconds);
}
std::array<float, 8> inputs(const juce::ValueTree &tree) {
    std::array<float, 8> values{};
    const char *ids[] = {"playStart", "playEnd",  "loopStart", "loopEnd",
                         "crossfade", "playMode", "direction", "loopMode"};
    for (int i = 0; i < 8; ++i)
        values[i] = static_cast<float>(tree.getChildWithProperty("id", ids[i])["value"]);
    return values;
}
void setRange(juce::ValueTree &tree, double begin, double end) {
    const char *ids[] = {"playStart", "playEnd", "loopStart", "loopEnd"};
    for (int i = 0; i < 4; ++i)
        tree.getChildWithProperty("id", ids[i])
            .setProperty("value", static_cast<float>(i % 2 ? end : begin), nullptr);
}
juce::String playbackStatus(const PlaybackProgram *program, const std::array<float, 8> &v) {
    const char *loops[] = {"Off", "Wrap", "Ping-pong"};
    auto text = juce::String(v[5] == 0 ? " Frozen frame." : " Trajectory.") +
                (v[6] == 0 ? " Forward." : " Reverse.") + " Loop " + loops[static_cast<int>(v[7])] +
                ". Playback " + juce::String(v[0], 3) + " to " + juce::String(v[1], 3) + " seconds; loop " +
                juce::String(v[2], 3) + " to " + juce::String(v[3], 3) + " seconds.";
    if (v[5] == 1 && program) {
        text += " " + juce::String(static_cast<int>(program->lanes.size())) + " trajectory oscillator lanes.";
        if (program->limited)
            text += " Rendering limit excludes weaker active trajectories; all trajectories are saved.";
    }
    return text;
}
} // namespace
Worker::Worker(Processor &p) : processor(p) {
    programs.reserve(64); // More than voices, stealing tails, and bounded frame queues combined.
    thread = std::thread([this] { run(); });
}
Worker::~Worker() {
    stop();
    pendingFrame.reset();
    if (currentProgram)
        currentProgram->release();
    currentProgram = nullptr;
}
void Worker::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
    }
    wake.notify_one();
    if (thread.joinable())
        thread.join();
}
void Worker::report(juce::String s, std::uint64_t requestGeneration) {
    std::lock_guard<std::mutex> lock(mutex);
    if (requestGeneration != 0 && requestGeneration != processor.generation.load())
        return;
    statusText = std::move(s);
}
juce::String Worker::status() const {
    std::lock_guard<std::mutex> lock(mutex);
    return statusText;
}
juce::MemoryBlock Worker::modelSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex);
    return cachedModel;
}
std::array<float, 8> Worker::configurationSnapshot() const {
    std::lock_guard<std::mutex> lock(mutex);
    return validInputs;
}
Worker::Snapshot Worker::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex);
    return {cachedModel, validInputs};
}
void Worker::reclaimPrograms() {
    const PlaybackProgram *returned;
    while (processor.programReturns.pop(returned)) {
    } // Tokens only; lifetime is checked in the registry.
    std::lock_guard<std::mutex> lock(mutex);
    programs.erase(
        std::remove_if(programs.begin(), programs.end(),
                       [](const auto &p) { return p->references.load(std::memory_order_acquire) == 0; }),
        programs.end());
}
void Worker::commitProgram(std::unique_ptr<PlaybackProgram> p) {
    // Caller holds mutex; frames and voices independently lease programs.
    auto *next = p.get();
    if (programs.size() >= 64)
        throw std::runtime_error("Playback program retirement capacity unavailable");
    programs.push_back(std::move(p));
    if (currentProgram)
        currentProgram->release();
    currentProgram = next;
    currentProgram->retain();
}
std::size_t Worker::availableBudget() const {
    std::lock_guard<std::mutex> lock(mutex);
    // Reserve synth/queued frames, validation structures and allocator overhead.
    auto used = 32u * 1024u * 1024u + cachedModel.getSize() + (state ? stateBytes(*state) : 0) +
                (job ? job->model.getSize() : 0);
    for (const auto &p : programs)
        used += p->bytes();
    return used < memoryBudget ? memoryBudget - used : 0;
}
void Worker::requireBudget(std::size_t bytes) const {
    if (bytes > availableBudget())
        throw std::runtime_error(
            "Per-instance 256 MiB analysis/state budget exceeded; choose a shorter range");
}
std::size_t Worker::budgetAfter(std::size_t reserve) const {
    const auto available = availableBudget();
    if (reserve > available)
        throw std::runtime_error("Per-instance 256 MiB analysis/state budget exceeded");
    return available - reserve;
}
void Worker::enqueue(Job j) {
    std::lock_guard<std::mutex> lock(mutex);
    j.generation = processor.generation.fetch_add(1) + 1;
    job = std::move(j);
    statusText = "Request queued. Previous timbre remains playable.";
    wake.notify_one();
}
void Worker::load(const juce::File &file) {
    Job j{};
    j.kind = Kind::load;
    j.file = file;
    enqueue(std::move(j));
}
void Worker::analyse() {
    Job j{};
    j.kind = Kind::analyse;
    j.start = processor.value(0);
    j.end = processor.value(1);
    j.channel = processor.inputChannel();
    j.settings = {processor.value(3), 2 * processor.value(3), processor.value(4)};
    enqueue(std::move(j));
}
void Worker::restore(juce::MemoryBlock model, juce::ValueTree params, bool legacy) {
    Job j{};
    j.kind = Kind::restore;
    j.model = std::move(model);
    j.params = std::move(params);
    j.legacy = legacy;
    enqueue(std::move(j));
}
void Worker::prepareFrame(std::uint64_t gen) {
    const bool positionChanged = processor.value(2) != state->frame.sourceSeconds;
    const bool limitChanged = static_cast<int>(processor.value(13)) != frameLimit;
    lastTime = processor.value(2);
    lastLimit = static_cast<int>(processor.value(13));
    lastInputs = processor.playbackInputs();
    const auto playback = settings(lastInputs, state->sound);
    const auto reserve = (positionChanged || limitChanged ? 3 * cachedModel.getSize() : 0) +
                         2 * state->sound.partials.size() * sizeof(SpectralPartial);
    requireBudget(reserve);
    auto frame =
        positionChanged || limitChanged
            ? sampleFrame(state->sound, positionChanged ? lastTime : state->frame.sourceSeconds, lastLimit)
            : state->frame;
    std::unique_ptr<PlaybackProgram> program;
    if (limitChanged)
        program = compilePlayback(state->sound, lastLimit, budgetAfter(reserve));
    auto prepared = std::make_unique<PreparedFrame>();
    prepared->generation = gen;
    prepared->frame = frame;
    prepared->previewOnAdopt = positionChanged && processor.previewEnabled.load(std::memory_order_relaxed);
    prepared->playback = playback;
    prepared->trajectory = lastInputs[5] == 1;
    prepared->attach(program ? program.get() : currentProgram);
    juce::MemoryBlock cached;
    if (positionChanged || limitChanged) {
        auto bytes = encodeState(state->sound, frame);
        cached.replaceAll(bytes.data(), bytes.size());
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping || gen != processor.generation.load() || processor.value(2) != lastTime ||
            static_cast<int>(processor.value(13)) != lastLimit || processor.playbackInputs() != lastInputs)
            return;
        if (program)
            commitProgram(std::move(program));
        state->frame = std::move(frame);
        frameLimit = lastLimit;
        if (cached.getSize())
            cachedModel = std::move(cached);
        validInputs = lastInputs;
        pendingFrame = std::move(prepared);
    }
    const auto rendered = state->frame.partials.size(), active = state->frame.activeCount;
    report("Ready. " + juce::String(static_cast<juce::int64>(state->sound.partials.size())) +
               " tracked partials. Sustain at " + juce::String(lastTime, 3) + " seconds. " +
               juce::String(static_cast<int>(rendered)) + " of " +
               juce::String(static_cast<juce::int64>(active)) + " active partials rendered." +
               (active > rendered ? " Rendering limit excludes weaker partials; all trajectories are saved."
                                  : "") +
               (rendered == 0 ? " This position is silent." : "") +
               playbackStatus(currentProgram, validInputs),
           gen);
}
void Worker::execute(Job &j) {
    if (j.kind == Kind::restore) {
        report("Restoring saved analysis.", j.generation);
        requireBudget(4 * j.model.getSize() + 1024u * 1024u);
        std::unique_ptr<SoundState> restored;
        if (j.model.getSize())
            restored = std::make_unique<SoundState>(decodeState(j.model.getData(), j.model.getSize()));
        if (j.legacy && restored)
            setRange(j.params, restored->sound.startSeconds, restored->sound.endSeconds);
        const auto restoredInputs = inputs(j.params);
        PlaybackSettings playback;
        std::unique_ptr<PlaybackProgram> program;
        if (restored) {
            playback = settings(restoredInputs, restored->sound);
            const auto reserve = stateBytes(*restored) + j.model.getSize();
            requireBudget(reserve);
            const int limit = static_cast<int>(j.params.getChildWithProperty("id", "partials")["value"]);
            program = compilePlayback(restored->sound, limit, budgetAfter(reserve));
        }
        auto prepared = std::make_unique<PreparedFrame>();
        prepared->generation = j.generation;
        if (restored)
            prepared->frame = restored->frame;
        prepared->playback = playback;
        prepared->trajectory = restoredInputs[5] == 1;
        prepared->attach(program.get());
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopping || j.generation != processor.generation.load())
                return;
            if (program)
                commitProgram(std::move(program));
            else if (currentProgram) {
                currentProgram->release();
                currentProgram = nullptr;
            }
            state = std::move(restored);
            validInputs = restoredInputs;
            samplingEnabled = state != nullptr;
            pendingFrame = std::move(prepared);
            cachedModel = std::move(j.model);
            source =
                state ? juce::File(juce::String::fromUTF8(state->sound.sourcePath.c_str())) : juce::File{};
        }
        // Host parameter callbacks stay outside the worker mutex.
        processor.parameters.replaceState(j.params);
        lastTime = processor.value(2);
        lastLimit = static_cast<int>(processor.value(13));
        frameLimit = lastLimit;
        lastInputs = processor.playbackInputs();
        if (state) {
            report("Ready. Saved analysis and sustain frame restored at " +
                       juce::String(state->frame.sourceSeconds, 3) +
                       " seconds. Original file is not needed for playback." +
                       playbackStatus(currentProgram, validInputs),
                   j.generation);
        } else {
            lastTime = -1;
            lastLimit = -1;
            report("State restored. No analysed sound saved; load a file to begin.", j.generation);
        }
        return;
    }
    report(j.kind == Kind::load ? "Loading audio file metadata."
                                : "Analysing audio. Previous timbre remains playable.",
           j.generation);
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const auto file = j.kind == Kind::load ? j.file : source;
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader)
        throw std::runtime_error("Cannot read source audio. Load/locate the original file for analysis.");
    const double duration = reader->lengthInSamples / reader->sampleRate;
    if (!std::isfinite(duration) || duration <= 0 || duration > 86400 || reader->sampleRate <= 0 ||
        reader->numChannels == 0)
        throw std::runtime_error("Invalid or unsupported audio length (maximum 24 hours).");
    if (j.kind == Kind::load) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopping || j.generation != processor.generation.load())
                return;
            source = file;
            samplingEnabled = false;
        }
        processor.fileDefaults(duration, j.generation);
        report("Loaded " + file.getFileName() + ", " + juce::String(duration, 3) +
                   " seconds. Set range and choose Analyse. Previous timbre remains playable.",
               j.generation);
        return;
    }
    double start = j.start, end = j.end == 0 ? duration : j.end;
    if (start < 0 || end > duration + .00051 || start >= end)
        throw std::runtime_error("Analysis range must have start < end within the source file.");
    end = std::min(end, duration);
    const auto first = static_cast<juce::int64>(std::floor(start * reader->sampleRate));
    const auto last =
        std::min(reader->lengthInSamples, static_cast<juce::int64>(std::floor(end * reader->sampleRate)));
    const auto count = last - first;
    if (count <= 0 || static_cast<std::uint64_t>(count) > memoryBudget / (sizeof(double) * 8))
        throw std::runtime_error("Selected audio exceeds 32 MiB mono PCM budget. Choose a shorter range.");
    const auto pcmBytes = static_cast<std::size_t>(count) * sizeof(double);
    // Account for PCM plus conservative Loris working headroom alongside the
    // retained sound/cache. Output/cached serialization reserve is checked too.
    requireBudget(4 * pcmBytes);
    const auto available = availableBudget();
    if (available < 4 * pcmBytes)
        throw std::runtime_error("Per-instance analysis budget unavailable while another state is queued");
    const auto outputAllowance = (available - 4 * pcmBytes) / 4;
    std::vector<double> mono(static_cast<std::size_t>(count));
    juce::AudioBuffer<float> block(2, 8192);
    for (juce::int64 offset = 0; offset < count; offset += 8192) {
        if (j.generation != processor.generation.load())
            return;
        const int n = static_cast<int>(std::min<juce::int64>(8192, count - offset));
        block.clear();
        if (!reader->read(&block, 0, n, first + offset, true, true))
            throw std::runtime_error("Audio decoding failed");
        const auto *l = block.getReadPointer(0);
        const auto *r = block.getReadPointer(1);
        for (int i = 0; i < n; ++i) {
            const double right = reader->numChannels > 1 ? r[i] : l[i];
            mono[static_cast<std::size_t>(offset + i)] = j.channel == InputChannel::left ? l[i]
                                                         : j.channel == InputChannel::right
                                                             ? right
                                                             : .5 * (l[i] + right);
        }
    }
    AnalysedSound metadata;
    metadata.sourcePath = file.getFullPathName().toStdString();
    metadata.sourceSampleRate = reader->sampleRate;
    metadata.sourceDuration = duration;
    metadata.startSeconds = first / reader->sampleRate;
    metadata.endSeconds = last / reader->sampleRate;
    metadata.channel = j.channel;
    metadata.settings = j.settings;
    auto result = LorisAnalyser{outputAllowance}.analyse(mono, std::move(metadata));
    if (j.generation != processor.generation.load())
        return;
    auto next = std::make_unique<SoundState>();
    next->sound = std::move(result);
    // Prepare before committing: invalid sustain positions retain the old sound.
    const double time = processor.value(2);
    const int limit = static_cast<int>(processor.value(13));
    next->frame = sampleFrame(next->sound, time, limit);
    const auto reserve = pcmBytes + 4 * (stateBytes(*next) + 512);
    requireBudget(reserve);
    auto program = compilePlayback(next->sound, limit, budgetAfter(reserve));
    auto newInputs = processor.playbackInputs();
    newInputs[0] = newInputs[2] = static_cast<float>(next->sound.startSeconds);
    newInputs[1] = newInputs[3] = static_cast<float>(next->sound.endSeconds);
    const auto playback = settings(newInputs, next->sound);
    auto bytes = encodeState(*next);
    juce::MemoryBlock cached(bytes.data(), bytes.size());
    auto prepared = std::make_unique<PreparedFrame>();
    prepared->generation = j.generation;
    prepared->frame = next->frame;
    prepared->attach(program.get());
    prepared->playback = playback;
    prepared->trajectory = newInputs[5] == 1;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping || j.generation != processor.generation.load())
            return;
        commitProgram(std::move(program));
        state = std::move(next);
        validInputs = newInputs;
        lastInputs = processor.playbackInputs();
        samplingEnabled = true;
        lastTime = time;
        lastLimit = limit;
        frameLimit = limit;
        cachedModel = std::move(cached);
        pendingFrame = std::move(prepared);
    }
    processor.rangeDefaults(state->sound.startSeconds, state->sound.endSeconds, j.generation);
    const auto rendered = state->frame.partials.size(), active = state->frame.activeCount;
    report("Ready. " + juce::String(static_cast<juce::int64>(state->sound.partials.size())) +
               " tracked partials. Sustain at " + juce::String(time, 3) + " seconds. " +
               juce::String(static_cast<int>(rendered)) + " of " +
               juce::String(static_cast<juce::int64>(active)) + " active partials rendered." +
               (active > rendered ? " Rendering limit excludes weaker partials; all trajectories are saved."
                                  : "") +
               (rendered == 0 ? " This position is silent." : "") +
               playbackStatus(currentProgram, validInputs),
           j.generation);
}
void Worker::run() {
    for (;;) {
        std::optional<Job> request;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait_for(lock, std::chrono::milliseconds(20),
                          [this] { return stopping || job.has_value(); });
            if (stopping)
                break;
            if (job) {
                request = std::move(job);
                job.reset();
            }
        }
        processor.frames.reclaim();
        reclaimPrograms();
        try {
            if (request)
                execute(*request);
            const auto gen = processor.generation.load();
            if (pendingFrame && pendingFrame->generation != gen)
                pendingFrame.reset();
            if (!request && samplingEnabled && state &&
                (processor.value(2) != lastTime || static_cast<int>(processor.value(13)) != lastLimit ||
                 processor.playbackInputs() != lastInputs))
                prepareFrame(gen);
            if (pendingFrame)
                processor.frames.publish(pendingFrame);
        } catch (const std::exception &e) {
            if (!request || request->generation == processor.generation.load())
                report("Error: " + juce::String(e.what()) + ". Previous timbre remains playable.",
                       request ? request->generation : 0);
        }
    }
    processor.frames.reclaim();
}
} // namespace sineweave
