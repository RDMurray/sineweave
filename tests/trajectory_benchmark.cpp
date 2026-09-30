// SPDX-License-Identifier: AGPL-3.0-only
#include "core/Synth.h"
#include <chrono>
#include <iostream>
#include <memory>
#include <vector>
using namespace sineweave;
int main(int argc, char **) {
    std::vector<float> left(512), right(512);
    std::cout << "16 voices, 48 kHz; 200 callbacks; measured deadline misses, device underruns unmeasured\n";
    for (int partialCount : {4096, 1024, 256}) {
        AnalysedSound sound;
        sound.sourceSampleRate = 48000;
        sound.sourceDuration = sound.endSeconds = 1;
        for (int i = 0; i < partialCount; ++i) {
            const double f = 40 + 4.7 * i, amp = 1. / partialCount;
            sound.partials.push_back({static_cast<std::uint64_t>(i),
                                      {{0, f, amp, wrapPhase(.7 * i), {}},
                                       {.5, f * 1.04, amp * .4, 0, {}},
                                       {1, f * .97, amp, 0, {}}}});
        }
        auto program = compilePlayback(sound, partialCount);
        auto synth = std::make_unique<Synth>();
        if (argc > 1)
            synth->disableAvx2();
        SynthControls c;
        c.speed = 1;
        for (bool overlap : {false, true})
            for (int block : {128, 256, 512}) {
                synth->prepare(48000);
                synth->setControls(c);
                // 20 ms traversal with automatically shortened overlap; synchronized boundaries exercise
                // doubled banks.
                PlaybackSettings cfg{0, 1, 0, overlap ? .02 : .8, 20, 1, LoopMode::wrap};
                for (int i = 0; i < 16; ++i)
                    synth->noteOn(0, 60 + i, 1, nullptr, program.get(), &cfg);
                synth->render(left.data(), right.data(), 512);
                double maximum = 0, total = 0;
                int misses = 0;
                constexpr int iterations = 200;
                for (int i = 0; i < iterations; ++i) {
                    if (overlap) {
                        // Force a boundary/overlap into every timed callback, avoiding diluted peak averages.
                        synth->reset();
                        for (int note = 0; note < 16; ++note)
                            synth->noteOn(0, 60 + note, 1, nullptr, program.get(), &cfg);
                        int prime = 960 - block / 4;
                        while (prime > 0) {
                            const int n = std::min(prime, 512);
                            synth->render(left.data(), right.data(), n);
                            prime -= n;
                        }
                    }
                    const auto start = std::chrono::steady_clock::now();
                    synth->render(left.data(), right.data(), block);
                    const double elapsed =
                        std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                    total += elapsed;
                    maximum = std::max(maximum, elapsed);
                    misses += elapsed > block / 48000.;
                }
                std::cout << (synth->usingAvx2() ? "AVX2" : "SSE2") << ", " << partialCount << ", "
                          << (overlap ? "loop overlap" : "changing trajectories") << ", " << block
                          << ": mean " << total * 1000 / iterations << " ms, max " << maximum * 1000
                          << " ms, load " << 100 * total / (iterations * block / 48000.) << "%, misses "
                          << misses << "/" << iterations << std::endl;
            }
    }
}
