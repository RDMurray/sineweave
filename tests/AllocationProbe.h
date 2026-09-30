#pragma once
#include <cstddef>
namespace probe {
extern thread_local bool audio;
extern thread_local std::size_t allocations, deallocations;
struct AudioScope {
    AudioScope() {
        allocations = deallocations = 0;
        audio = true;
    }
    ~AudioScope() {
        audio = false;
    }
};
} // namespace probe
