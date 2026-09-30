#include <Analyzer.h>
#include <Partial.h>
#include <cmath>
#include <iostream>
#include <vector>
int main() {
    constexpr double rate = 48000.0, pi = 3.14159265358979323846;
    std::vector<double> samples(48000);
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = 0.5 * std::cos(2 * pi * 613.7 * i / rate + 0.3);
    Loris::Analyzer analyzer(40, 80);
    analyzer.setAmpFloor(-80);
    auto partials = analyzer.analyze(samples, rate);
    for (const auto &p : partials)
        if (p.startTime() < .5 && p.endTime() > .5 && std::abs(p.frequencyAt(.5) - 613.7) < 1 &&
            p.amplitudeAt(.5) > .4) {
            std::cout << "Loris tone analysis passed: " << p.frequencyAt(.5) << " Hz\n";
            return 0;
        }
    std::cerr << "Loris did not track the synthetic tone\n";
    return 1;
}
