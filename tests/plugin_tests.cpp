#include "AllocationProbe.h"
#include "plugin/Editor.h"
#include "plugin/Processor.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#if JUCE_WINDOWS
#include <windows.h>
#endif
using namespace sineweave;
class TestPlayHead final : public juce::AudioPlayHead {
  public:
    bool playing = false, recording = false;
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo position;
        position.setIsPlaying(playing);
        position.setIsRecording(recording);
        return position;
    }
};
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error("Check failed: " #x);                                                   \
    } while (false)
void pump() {
#if JUCE_WINDOWS
    MSG message;
    while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessage(&message);
    }
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}
template <class F> void waitFor(F done) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!done()) {
        pump();
        if (std::chrono::steady_clock::now() > deadline)
            throw std::runtime_error("Worker timeout");
    }
}
void set(Processor &p, const char *id, float v) {
    auto *parameter = p.parameters.getParameter(id);
    parameter->setValueNotifyingHost(parameter->convertTo0to1(v));
}
double render(Processor &p, bool note = false, int offset = 0) {
    juce::AudioBuffer<float> audio(2, 512);
    juce::MidiBuffer midi;
    if (note)
        midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.f), offset);
    {
        probe::AudioScope guard;
        p.processBlock(audio, midi);
    }
    CHECK(probe::allocations == 0 && probe::deallocations == 0);
    if (offset)
        for (int i = 0; i < offset; ++i)
            CHECK(audio.getSample(0, i) == 0);
    double e = 0;
    for (int i = 0; i < 512; ++i) {
        CHECK(audio.getSample(0, i) == audio.getSample(1, i));
        e += audio.getSample(0, i) * audio.getSample(0, i);
    }
    return e;
}
juce::MemoryBlock editState(const juce::MemoryBlock &saved,
                            const std::function<void(juce::ValueTree &)> &edit,
                            const std::vector<std::uint8_t> *replacementModel = nullptr) {
    juce::MemoryInputStream in(saved, false);
    CHECK(in.readInt() == 0x31505753);
    const int size = in.readInt();
    auto xml = juce::AudioProcessor::getXmlFromBinary(static_cast<const char *>(saved.getData()) + 8, size);
    auto tree = juce::ValueTree::fromXml(*xml);
    edit(tree);
    juce::MemoryBlock params;
    juce::AudioProcessor::copyXmlToBinary(*tree.createXml(), params);
    juce::MemoryBlock result;
    juce::MemoryOutputStream out(result, false);
    out.writeInt(0x31505753);
    out.writeInt(static_cast<int>(params.getSize()));
    out.write(params.getData(), params.getSize());
    if (replacementModel) {
        out.writeInt(static_cast<int>(replacementModel->size()));
        out.write(replacementModel->data(), replacementModel->size());
    } else
        out.write(static_cast<const char *>(saved.getData()) + 8 + size, saved.getSize() - 8 - size);
    out.flush();
    return result;
}
int main(int argc, char **argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    juce::File file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getNonexistentChildFile("sineweave-test", ".wav");
    try {
        juce::WavAudioFormat format;
        std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
        CHECK(stream);
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(stream.release(), 48000, 2, 16, {}, 0));
        CHECK(writer);
        juce::AudioBuffer<float> samples(2, 48000);
        for (int i = 0; i < 48000; ++i) {
            samples.setSample(0, i, .4f * static_cast<float>(std::cos(2 * pi * 613.7 * i / 48000)));
            samples.setSample(1, i, .3f * static_cast<float>(std::cos(2 * pi * 947.3 * i / 48000)));
        }
        CHECK(writer->writeFromAudioSampleBuffer(samples, 0, 48000));
        writer.reset();
        Processor p;
        p.prepareToPlay(48000, 512);
        p.loadFile(file);
        waitFor([&] { return p.status().startsWith("Loaded"); });
        pump();
        CHECK(p.value(1) == 1 && p.value(2) == .5f);
        p.analyse();
        waitFor([&] { return p.status().startsWith("Ready"); });
        render(p);
        CHECK(render(p, true, 120) > 0);
        juce::MemoryBlock saved;
        p.getStateInformation(saved);
        CHECK(saved.getSize() > 1000);
        {
            auto legacy = editState(saved, [](auto &tree) {
                for (int i = tree.getNumChildren(); --i >= 0;) {
                    const auto id = tree.getChild(i)["id"].toString();
                    bool old = id == "channel";
                    for (int j = 0; j < 14; ++j)
                        old |= id == specs[j].id;
                    if (!old)
                        tree.removeChild(i, nullptr);
                }
            });
            Processor old;
            old.prepareToPlay(48000, 512);
            old.setStateInformation(legacy.getData(), static_cast<int>(legacy.getSize()));
            waitFor([&] { return old.status().startsWith("Ready"); });
            CHECK(old.playbackMode() == 0 && old.value(15) == 0 && old.value(16) == 1);
            render(old);
            CHECK(render(old, true) > 0);
        }
        juce::MemoryBlock trajectorySaved;
        {
            Processor moving;
            moving.prepareToPlay(48000, 512);
            moving.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
            waitFor([&] { return moving.status().startsWith("Ready"); });
            set(moving, "playMode", 1);
            set(moving, "loopStart", .2f);
            set(moving, "loopEnd", .6f);
            set(moving, "loopMode", 2);
            set(moving, "direction", 1);
            set(moving, "speed", .5f);
            waitFor([&] { return moving.status().contains("Trajectory. Reverse. Loop Ping-pong."); });
            render(moving);
            render(moving, true, 27);
            waitFor([&] { return render(moving) > 0; }); // Reverse traverses Loris's silent end margin.
            for (int i = 0; i < 120; ++i)
                render(moving);
            CHECK(render(moving) > 0);
            moving.getStateInformation(trajectorySaved);
            set(moving, "playStart", .9f);
            waitFor([&] { return moving.status().startsWith("Error"); });
            CHECK(render(moving, true) > 0);
            juce::MemoryBlock validSaved;
            moving.getStateInformation(validSaved);
            Processor valid;
            valid.prepareToPlay(48000, 512);
            valid.setStateInformation(validSaved.getData(), static_cast<int>(validSaved.getSize()));
            waitFor([&] { return valid.status().startsWith("Ready"); });
            CHECK(valid.value(15) == 0 && valid.playbackMode() == 1);
            set(moving, "playStart", 0);
            waitFor([&] { return moving.status().startsWith("Ready"); });
            for (int i = 0; i < 10; ++i) {
                set(moving, "partials", static_cast<float>(i % 2 + 1));
                for (int j = 0; j < 4; ++j) {
                    pump();
                    render(moving);
                }
                CHECK(render(moving, true) > 0);
            }
        }
        std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
        editor->addToDesktop(juce::ComponentPeer::windowIsTemporary);
        CHECK(editor->getAccessibilityHandler());
        for (int i = 0; i < editor->getNumChildComponents(); ++i)
            CHECK(editor->getChildComponent(i)->getAccessibilityHandler());
        int numericControls = 0;
        std::function<void(juce::Component &)> visit = [&](juce::Component &component) {
            if (auto *slider = dynamic_cast<NumericSlider *>(&component)) {
                CHECK(slider->getAccessibilityHandler());
                CHECK(slider->keyPressed(juce::KeyPress(juce::KeyPress::returnKey)));
                slider->hideTextBox(true);
                ++numericControls;
            }
            for (int i = 0; i < component.getNumChildComponents(); ++i)
                visit(*component.getChildComponent(i));
        };
        visit(*editor);
        CHECK(numericControls == static_cast<int>(specs.size()));
        editor.reset();
        {
            TestPlayHead transport;
            Processor audition;
            audition.setPlayHead(&transport);
            audition.prepareToPlay(48000, 512);
            audition.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
            waitFor([&] { return audition.status().startsWith("Ready"); });
            std::unique_ptr<juce::AudioProcessorEditor> auditionEditor(audition.createEditor());
            CHECK(audition.previewEnabled.load());
            for (int i = 0; i < 4; ++i) {
                CHECK(render(audition) == 0); // Opening/restoring does not audition.
                pump();
            }
            set(audition, "position", .6f);
            waitFor([&] { return audition.status().contains("Sustain at 0.600"); });
            waitFor([&] { return render(audition) > 0; });
            for (int i = 0; i < 32; ++i)
                render(audition);
            CHECK(render(audition) == 0); // Preview releases automatically.
            set(audition, "position", 2);
            waitFor([&] { return audition.status().contains("outside analysed range"); });
            CHECK(render(audition) == 0); // Invalid requests do not audition old timbre.
            set(audition, "position", .62f);
            waitFor([&] { return audition.status().contains("Sustain at 0.620"); });
            waitFor([&] { return render(audition) > 0; });
            transport.recording = true;
            CHECK(render(audition) == 0); // Recording immediately stops an existing preview/tail.
            transport.recording = false;
            transport.playing = true;
            set(audition, "position", .65f);
            waitFor([&] { return audition.status().contains("Sustain at 0.650"); });
            for (int i = 0; i < 4; ++i) {
                CHECK(render(audition) == 0);
                pump();
            }
            transport.playing = false;
            transport.recording = true;
            set(audition, "position", .7f);
            waitFor([&] { return audition.status().contains("Sustain at 0.700"); });
            for (int i = 0; i < 4; ++i) {
                CHECK(render(audition) == 0);
                pump();
            }
            transport.recording = false;
            audition.setNonRealtime(true);
            set(audition, "position", .75f);
            waitFor([&] { return audition.status().contains("Sustain at 0.750"); });
            for (int i = 0; i < 4; ++i) {
                CHECK(render(audition) == 0);
                pump();
            }
            audition.setNonRealtime(false);
            juce::ToggleButton *previewToggle = nullptr;
            for (int i = 0; i < auditionEditor->getNumChildComponents(); ++i)
                if (auto *toggle = dynamic_cast<juce::ToggleButton *>(auditionEditor->getChildComponent(i)))
                    previewToggle = toggle;
            CHECK(previewToggle && previewToggle->getToggleState());
            previewToggle->setToggleState(false, juce::sendNotificationSync);
            CHECK(!audition.previewEnabled.load());
            set(audition, "position", .78f);
            waitFor([&] { return audition.status().contains("Sustain at 0.780"); });
            for (int i = 0; i < 4; ++i) {
                CHECK(render(audition) == 0);
                pump();
            }
            previewToggle->setToggleState(true, juce::sendNotificationSync);
            CHECK(audition.previewEnabled.load());
            auditionEditor.reset();
            CHECK(!audition.previewEnabled.load());
            set(audition, "position", .8f);
            waitFor([&] { return audition.status().contains("Sustain at 0.800"); });
            for (int i = 0; i < 4; ++i) {
                CHECK(render(audition) == 0);
                pump();
            }
        }
        if (argc == 2) {
            juce::File fixtures(argv[1]);
            CHECK(fixtures.createDirectory());
            CHECK(fixtures.getChildFile("state.bin").replaceWithData(saved.getData(), saved.getSize()));
            // REAPER's vst_chunk API wraps VST3 component/controller streams, unlike
            // the processor's raw state. This header is host-harness-only.
            juce::MemoryOutputStream hostChunk;
            hostChunk.writeInt(static_cast<int>(saved.getSize()));
            hostChunk.writeInt(1);
            hostChunk.write(saved.getData(), saved.getSize());
            hostChunk.writeInt(0);
            hostChunk.writeInt(0);
            CHECK(fixtures.getChildFile("state.base64")
                      .replaceWithText(juce::Base64::toBase64(hostChunk.getData(), hostChunk.getDataSize())));
            CHECK(file.copyFileTo(fixtures.getChildFile("sample.wav")));
            AnalysedSound chirp;
            chirp.sourcePath = "missing-trajectory-source.wav";
            chirp.sourceSampleRate = 48000;
            chirp.sourceDuration = chirp.endSeconds = 1;
            chirp.partials = {{1, {{0, 613.7, .4, 0, {}}, {1, 813.7, .4, wrapPhase(2 * pi * 713.7), {}}}},
                              {2, {{0, 947.3, .3, 0, {}}, {1, 1047.3, .3, wrapPhase(2 * pi * 997.3), {}}}}};
            auto model = encodeState(chirp, sampleFrame(chirp, .5));
            auto hostState = editState(
                saved,
                [](juce::ValueTree &tree) {
                    tree.getChildWithProperty("id", "playMode").setProperty("value", 1, nullptr);
                    tree.getChildWithProperty("id", "speed").setProperty("value", .5f, nullptr);
                    tree.getChildWithProperty("id", "loopMode").setProperty("value", 1, nullptr);
                    tree.getChildWithProperty("id", "loopStart").setProperty("value", .2f, nullptr);
                    tree.getChildWithProperty("id", "loopEnd").setProperty("value", .8f, nullptr);
                },
                &model);
            juce::MemoryOutputStream trajectoryChunk;
            trajectoryChunk.writeInt(static_cast<int>(hostState.getSize()));
            trajectoryChunk.writeInt(1);
            trajectoryChunk.write(hostState.getData(), hostState.getSize());
            trajectoryChunk.writeInt(0);
            trajectoryChunk.writeInt(0);
            CHECK(fixtures.getChildFile("trajectory.base64")
                      .replaceWithText(
                          juce::Base64::toBase64(trajectoryChunk.getData(), trajectoryChunk.getDataSize())));
        }
        Processor empty;
        juce::MemoryBlock emptyState;
        empty.getStateInformation(emptyState);
        {
            Processor race;
            race.prepareToPlay(48000, 512);
            race.loadFile(file);
            waitFor([&] { return race.status().startsWith("Loaded"); });
            pump();
            race.analyse();
            waitFor(
                [&] { return race.status().startsWith("Analysing") || race.status().startsWith("Ready"); });
            race.setStateInformation(emptyState.getData(), static_cast<int>(emptyState.getSize()));
            waitFor([&] { return race.status().contains("No analysed sound"); });
            for (int i = 0; i < 8; ++i) {
                render(race);
                pump();
            }
            CHECK(render(race, true) == 0);
        }
        file.deleteFile();
        {
            Processor moving;
            moving.prepareToPlay(48000, 512);
            moving.setStateInformation(trajectorySaved.getData(),
                                       static_cast<int>(trajectorySaved.getSize()));
            waitFor([&] { return moving.status().startsWith("Ready"); });
            CHECK(moving.playbackMode() == 1 && moving.value(14) == .5f);
            render(moving);
            render(moving, true);
            waitFor([&] { return render(moving) > 0; });
            for (int i = 0; i < 120; ++i)
                render(moving);
            CHECK(render(moving) > 0);
        }
        Processor restored;
        restored.prepareToPlay(48000, 512);
        restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        waitFor([&] { return restored.status().startsWith("Ready"); });
        render(restored);
        CHECK(render(restored, true) > 0);
        restored.analyse();
        waitFor([&] { return restored.status().startsWith("Error"); });
        CHECK(render(restored) > 0);
        set(restored, "position", 2);
        waitFor([&] { return restored.status().contains("outside analysed range"); });
        CHECK(render(restored) > 0);
        set(restored, "position", .75f);
        waitFor([&] { return restored.status().startsWith("Ready"); });
        render(restored);
        const unsigned char bad[] = {0, 1, 2};
        restored.setStateInformation(bad, 3);
        CHECK(restored.status().startsWith("Error"));
        CHECK(render(restored) > 0);
        {
            juce::MemoryInputStream original(saved, false);
            original.readInt();
            const int xmlSize = original.readInt();
            auto xml = juce::AudioProcessor::getXmlFromBinary(static_cast<const char *>(saved.getData()) + 8,
                                                              xmlSize);
            CHECK(xml && xml->getFirstChildElement());
            xml->getFirstChildElement()->setAttribute("value", "nan");
            juce::MemoryBlock corruptXml;
            juce::AudioProcessor::copyXmlToBinary(*xml, corruptXml);
            juce::MemoryBlock malformed;
            juce::MemoryOutputStream out(malformed, false);
            out.writeInt(0x31505753);
            out.writeInt(static_cast<int>(corruptXml.getSize()));
            out.write(corruptXml.getData(), corruptXml.getSize());
            const auto remainder = saved.getSize() - static_cast<std::size_t>(8 + xmlSize);
            out.write(static_cast<const char *>(saved.getData()) + 8 + xmlSize, remainder);
            out.flush();
            restored.setStateInformation(malformed.getData(), static_cast<int>(malformed.getSize()));
            CHECK(restored.status().contains("parameter value outside range"));
            CHECK(render(restored) > 0);
        }
        {
            // A large, structurally wrapped input must fail budget admission
            // before decoding/allocating a candidate model, retaining playback.
            juce::MemoryInputStream original(saved, false);
            original.readInt();
            const auto parameterBytes = original.readInt();
            constexpr int hugeModelBytes = 70 * 1024 * 1024;
            juce::MemoryBlock oversized(static_cast<std::size_t>(12 + parameterBytes + hugeModelBytes), true);
            std::memcpy(oversized.getData(), saved.getData(), static_cast<std::size_t>(8 + parameterBytes));
            auto *length = static_cast<unsigned char *>(oversized.getData()) + 8 + parameterBytes;
            for (int i = 0; i < 4; ++i)
                length[i] = static_cast<unsigned char>(hugeModelBytes >> (8 * i));
            restored.setStateInformation(oversized.getData(), static_cast<int>(oversized.getSize()));
            waitFor([&] { return restored.status().contains("budget exceeded"); });
            CHECK(render(restored) > 0);
        }
        restored.setStateInformation(emptyState.getData(), static_cast<int>(emptyState.getSize()));
        waitFor([&] { return restored.status().contains("No analysed sound"); });
        render(restored);
        restored.releaseResources();
        restored.prepareToPlay(48000, 512);
        CHECK(render(restored, true) == 0);
        // Superseded restoration cannot replace a later empty state.
        for (int i = 0; i < 20; ++i) {
            restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
            restored.setStateInformation(emptyState.getData(), static_cast<int>(emptyState.getSize()));
        }
        waitFor([&] { return restored.status().contains("No analysed sound"); });
        for (int i = 0; i < 8; ++i) {
            render(restored);
            pump();
        }
        restored.prepareToPlay(48000, 512);
        CHECK(render(restored, true) == 0);
        std::cout << "Plugin decode/analyse/MIDI/state/accessibility metadata checks passed; NVDA/OSARA "
                     "remains a manual acceptance gate\n";
        return 0;
    } catch (const std::exception &e) {
        file.deleteFile();
        std::cerr << e.what() << '\n';
        return 1;
    }
}
