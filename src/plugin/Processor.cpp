// SPDX-License-Identifier: AGPL-3.0-only
#include "Processor.h"
#include "Editor.h"
#include "Worker.h"
#include <charconv>
namespace sineweave {
namespace {
bool savedNumber(const juce::ValueTree &child, double &value) {
    const auto text = child["value"].toString().trim().toStdString();
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && std::isfinite(value);
}
} // namespace
juce::AudioProcessorValueTreeState::ParameterLayout Processor::makeParameters() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const auto &s : specs) {
        auto attributes =
            juce::AudioParameterFloatAttributes()
                .withLabel(s.unit)
                .withStringFromValueFunction([s](float v, int) {
                    if (juce::String(s.id) == "root")
                        return juce::String(v, 2) + " (" +
                               juce::MidiMessage::getMidiNoteName(juce::roundToInt(v), true, true, 4) + ")";
                    if (juce::String(s.id) == "sustain")
                        return juce::String(v * 100, 1) + " %";
                    return juce::String(v, s.interval >= 1 ? 0 : 3) + " " + s.unit;
                })
                .withValueFromStringFunction([s](const juce::String &text) {
                    auto v = text.getFloatValue();
                    return juce::String(s.id) == "sustain" && text.contains("%") ? v / 100 : v;
                });
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{s.id, 1}, s.name,
            juce::NormalisableRange<float>{s.minimum, s.maximum, s.interval, s.skew}, s.initial, attributes));
    }
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"channel", 1}, "Analysis input channel",
        juce::StringArray{"Mono mix", "Left", "Right"}, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{"playMode", 1}, "Playback mode",
                                                            juce::StringArray{"Frozen frame", "Trajectory"},
                                                            0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"direction", 1}, "Playback direction", juce::StringArray{"Forward", "Reverse"}, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"loopMode", 1}, "Loop mode", juce::StringArray{"Off", "Wrap", "Ping-pong"}, 0));
    return layout;
}
Processor::Processor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      synth(std::make_unique<Synth>(&programReturns)),
      parameters(*this, nullptr, "SineweaveParameters", makeParameters()) {
    for (std::size_t i = 0; i < specs.size(); ++i)
        values[i] = parameters.getRawParameterValue(specs[i].id);
    channelValue = parameters.getRawParameterValue("channel");
    modeValue = parameters.getRawParameterValue("playMode");
    directionValue = parameters.getRawParameterValue("direction");
    loopValue = parameters.getRawParameterValue("loopMode");
    worker = std::make_unique<Worker>(*this);
}
Processor::~Processor() {
    worker->stop();
    synth->reset();
    frames.clearStopped();
    worker.reset();
    cancelPendingUpdate();
}
void Processor::loadFile(const juce::File &f) {
    worker->load(f);
}
void Processor::analyse() {
    worker->analyse();
}
juce::String Processor::status() const {
    return worker->status();
}
void Processor::fileDefaults(double duration, std::uint64_t gen) {
    {
        std::lock_guard<std::mutex> lock(defaultsMutex);
        pendingDuration = duration;
        pendingFileDefaults = true;
        pendingRangeDefaults = true;
        pendingRangeStart = 0;
        pendingRangeEnd = duration;
        defaultsGeneration = gen;
    }
    triggerAsyncUpdate();
}
void Processor::rangeDefaults(double start, double end, std::uint64_t gen) {
    {
        std::lock_guard<std::mutex> lock(defaultsMutex);
        pendingRangeStart = start;
        pendingRangeEnd = end;
        pendingRangeDefaults = true;
        defaultsGeneration = gen;
    }
    triggerAsyncUpdate();
}
void Processor::handleAsyncUpdate() {
    double duration;
    std::uint64_t gen;
    double start, end;
    bool file, range;
    {
        std::lock_guard<std::mutex> lock(defaultsMutex);
        duration = pendingDuration;
        gen = defaultsGeneration;
        start = pendingRangeStart;
        end = pendingRangeEnd;
        file = pendingFileDefaults;
        range = pendingRangeDefaults;
        pendingFileDefaults = pendingRangeDefaults = false;
    }
    if (gen != generation.load())
        return;
    const float defaults[] = {0, static_cast<float>(duration), static_cast<float>(duration * .5)};
    auto update = [&](int index, float value) {
        auto *param = parameters.getParameter(specs[index].id);
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1(value));
        param->endChangeGesture();
    };
    if (file)
        for (int i = 0; i < 3; ++i)
            update(i, defaults[i]);
    if (range)
        for (int i = 15; i <= 18; ++i)
            update(i, static_cast<float>(i % 2 ? start : end));
}
void Processor::prepareToPlay(double rate, int) {
    synth->prepare(rate);
}
void Processor::releaseResources() {
    synth->reset();
}
bool Processor::isBusesLayoutSupported(const BusesLayout &l) const {
    return l.getMainInputChannelSet().isDisabled() &&
           (l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() ||
            l.getMainOutputChannelSet() == juce::AudioChannelSet::mono());
}
void Processor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi) {
    juce::ScopedNoDenormals guard;
    buffer.clear();
    if (buffer.getNumChannels() < 1)
        return;
    synth->setControls({value(6), value(5), value(7), value(8), value(9), value(10), value(11),
                        static_cast<int>(value(12)), value(14)});
    bool preview = false;
    const auto *frame = frames.adopt(generation.load(std::memory_order_acquire), &preview);
    const auto *prepared = frames.prepared();
    const bool auditionEnabled = previewEnabled.load(std::memory_order_relaxed);
    bool transportActive = isNonRealtime();
    if (auditionEnabled)
        if (auto *hostPlayHead = getPlayHead())
            if (auto position = hostPlayHead->getPosition())
                transportActive = transportActive || position->getIsPlaying() || position->getIsRecording();
    if (!auditionEnabled || transportActive)
        synth->stopPreview(transportActive);
    else if (preview && frame && std::abs(frame->sourceSeconds - value(2)) <= .00051)
        synth->startPreview(frame);
    auto *left = buffer.getWritePointer(0);
    auto *right = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    int cursor = 0;
    // Raw metadata avoids MidiMessage copies (SysEx may allocate).
    for (const auto metadata : midi) {
        const int position = std::clamp(metadata.samplePosition, 0, buffer.getNumSamples());
        if (position > cursor) {
            synth->render(left + cursor, right ? right + cursor : nullptr, position - cursor);
            cursor = position;
        }
        const auto *d = metadata.data;
        const int length = metadata.numBytes;
        if (length < 1)
            continue;
        const int ch = d[0] & 15, kind = d[0] & 240;
        if (length >= 3) {
            if (kind == 0x90)
                synth->noteOn(ch, d[1], d[2] / 127.f, frame,
                              prepared && prepared->trajectory ? prepared->program : nullptr,
                              prepared && prepared->trajectory ? &prepared->playback : nullptr);
            else if (kind == 0x80)
                synth->noteOff(ch, d[1]);
            else if (kind == 0xe0) {
                const int b = d[1] + 128 * d[2];
                synth->pitchBend(ch, (b - 8192) / (b >= 8192 ? 8191.f : 8192.f));
            } else if (kind == 0xb0) {
                if (d[1] == 64)
                    synth->sustainPedal(ch, d[2] >= 64);
                else if (d[1] == 120 || d[1] == 123) {
                    synth->allNotesOff(ch, d[1] == 120);
                    if (d[1] == 120)
                        synth->stopPreview(true);
                } else if (d[1] == 121) {
                    synth->pitchBend(ch, 0);
                    synth->sustainPedal(ch, false);
                }
            }
        }
    }
    if (cursor < buffer.getNumSamples())
        synth->render(left + cursor, right ? right + cursor : nullptr, buffer.getNumSamples() - cursor);
    midi.clear();
}
juce::AudioProcessorEditor *Processor::createEditor() {
    return new Editor(*this);
}
void Processor::getStateInformation(juce::MemoryBlock &dest) {
    auto snapshot = worker->snapshot();
    auto model = std::move(snapshot.model);
    auto parameterState = parameters.copyState();
    const auto configuration = snapshot.configuration;
    const char *ids[] = {"playStart", "playEnd",  "loopStart", "loopEnd",
                         "crossfade", "playMode", "direction", "loopMode"};
    if (model.getSize())
        for (int i = 0; i < 8; ++i)
            parameterState.getChildWithProperty("id", ids[i]).setProperty("value", configuration[i], nullptr);
    auto xml = parameterState.createXml();
    juce::MemoryBlock paramData;
    copyXmlToBinary(*xml, paramData);
    if (model.getSize() + paramData.getSize() + 12 > memoryBudget) {
        worker->error("Error: complete plugin state exceeds 256 MiB.");
        dest.reset();
        return;
    }
    juce::MemoryOutputStream out(dest, false);
    out.writeInt(0x31505753);
    out.writeInt(static_cast<int>(paramData.getSize()));
    out.write(paramData.getData(), paramData.getSize());
    out.writeInt(static_cast<int>(model.getSize()));
    out.write(model.getData(), model.getSize());
}
void Processor::setStateInformation(const void *data, int size) {
    if (!data || size < 12 || static_cast<std::size_t>(size) > memoryBudget) {
        worker->error("Error: invalid or oversized plugin state.");
        return;
    }
    juce::MemoryInputStream in(data, static_cast<std::size_t>(size), false);
    if (in.readInt() != 0x31505753) {
        worker->error("Error: unsupported plugin state.");
        return;
    }
    const int paramSize = in.readInt();
    if (paramSize <= 0 || paramSize > 1024 * 1024 || paramSize > size - 12) {
        worker->error("Error: invalid parameter state.");
        return;
    }
    juce::MemoryBlock paramData;
    in.readIntoMemoryBlock(paramData, static_cast<std::size_t>(paramSize));
    auto xml = getXmlFromBinary(paramData.getData(), paramSize);
    if (!xml || !xml->hasTagName("SineweaveParameters")) {
        worker->error("Error: invalid parameter XML.");
        return;
    }
    const int modelSize = in.readInt();
    if (modelSize < 0 || modelSize != size - 12 - paramSize) {
        worker->error("Error: invalid analysis state length.");
        return;
    }
    auto tree = juce::ValueTree::fromXml(*xml);
    const bool legacy = tree.getNumChildren() == 15;
    if (!legacy && tree.getNumChildren() != static_cast<int>(specs.size() + 4)) {
        worker->error("Error: invalid saved parameter count.");
        return;
    }
    // Accept exactly our known finite, in-range parameter values.
    for (std::size_t index = 0; index < specs.size(); ++index) {
        const auto &s = specs[index];
        bool found = false;
        for (auto child : tree)
            if (child["id"].toString() == s.id) {
                if (found || !child.hasProperty("value")) {
                    worker->error("Error: duplicate or missing saved parameter value.");
                    return;
                }
                double v = 0;
                if (!savedNumber(child, v) || v < s.minimum || v > s.maximum) {
                    worker->error("Error: parameter value outside range.");
                    return;
                }
                found = true;
            }
        if (!found) {
            if (legacy && index >= 14) {
                juce::ValueTree child("PARAM");
                child.setProperty("id", s.id, nullptr);
                child.setProperty("value", s.initial, nullptr);
                tree.addChild(child, -1, nullptr);
                continue;
            }
            worker->error("Error: missing saved parameter.");
            return;
        }
    }
    bool channelFound = false;
    for (auto child : tree)
        if (child["id"].toString() == "channel") {
            double v = 0;
            if (!savedNumber(child, v) || v < 0 || v > 2 || v != std::floor(v)) {
                worker->error("Error: invalid saved channel.");
                return;
            }
            channelFound = true;
        }
    if (!channelFound) {
        worker->error("Error: missing saved channel.");
        return;
    }
    const char *choiceIds[] = {"playMode", "direction", "loopMode"};
    for (int i = 0; i < 3; ++i) {
        auto child = tree.getChildWithProperty("id", choiceIds[i]);
        if (!child.isValid() && legacy) {
            child = juce::ValueTree("PARAM");
            child.setProperty("id", choiceIds[i], nullptr);
            child.setProperty("value", 0, nullptr);
            tree.addChild(child, -1, nullptr);
        }
        double value = 0;
        int matches = 0;
        for (auto item : tree)
            if (item["id"].toString() == choiceIds[i])
                ++matches;
        if (matches != 1 || !savedNumber(child, value) || value < 0 || value > (i == 2 ? 2 : 1) ||
            value != std::floor(value)) {
            worker->error("Error: invalid saved playback choice.");
            return;
        }
    }
    juce::MemoryBlock model;
    in.readIntoMemoryBlock(model, static_cast<std::size_t>(modelSize));
    worker->restore(std::move(model), std::move(tree), legacy);
}
} // namespace sineweave
juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() {
    return new sineweave::Processor();
}
