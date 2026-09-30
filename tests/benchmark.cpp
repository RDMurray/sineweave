#include "core/Synth.h"
#include <chrono>
#include <iostream>
#include <memory>
#include <vector>
using namespace sineweave;
int main(int argc, char **) {
    auto synth = std::make_unique<Synth>();
    if (argc > 1)
        synth->disableAvx2();
    std::cout << (synth->usingAvx2() ? "AVX2" : "SSE2/scalar") << " kernel" << std::endl;
    std::vector<float> left(512), right(512);
    for (int partialCount : {4096, 1024, 256}) {
        SpectralFrame frame;
        for (int i = 0; i < partialCount; ++i)
            frame.partials.push_back(
                {static_cast<std::uint64_t>(i), 40 + 4.7 * i, 1. / partialCount, wrapPhase(.7 * i), {}});
        frame.activeCount = frame.partials.size();
        std::cout << "16 voices x " << partialCount << " partials, 48 kHz" << std::endl;
        for (int block : {128, 256, 512}) {
            synth->prepare(48000);
            for (int i = 0; i < 16; ++i)
                synth->noteOn(0, 60 + i, 1, &frame);
            synth->render(left.data(), right.data(), 512);
            double maximum = 0, total = 0;
            int misses = 0;
            constexpr int iterations = 200;
            for (int i = 0; i < iterations; ++i) {
                auto start = std::chrono::steady_clock::now();
                synth->render(left.data(), right.data(), block);
                const double seconds =
                    std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                total += seconds;
                maximum = std::max(maximum, seconds);
                if (seconds > block / 48000.)
                    ++misses;
            }
            std::cout << block << " samples: mean " << 1000 * total / iterations << " ms, max "
                      << maximum * 1000 << " ms, realtime load "
                      << 100 * total / (iterations * block / 48000.) << "%, deadline misses " << misses << "/"
                      << iterations << std::endl;
        }
    }
}
