#include "AllocationProbe.h"
#include <cstdlib>
#include <new>
#if defined(_WIN32)
#include <malloc.h>
#endif
namespace probe {
thread_local bool audio = false;
thread_local std::size_t allocations = 0, deallocations = 0;
} // namespace probe
void *operator new(std::size_t n) {
    if (probe::audio)
        ++probe::allocations;
    if (auto *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void *operator new[](std::size_t n) {
    return ::operator new(n);
}
void operator delete(void *p) noexcept {
    if (p && probe::audio)
        ++probe::deallocations;
    std::free(p);
}
void operator delete[](void *p) noexcept {
    ::operator delete(p);
}
void operator delete(void *p, std::size_t) noexcept {
    ::operator delete(p);
}
void operator delete[](void *p, std::size_t) noexcept {
    ::operator delete(p);
}
void *operator new(std::size_t n, std::align_val_t a) {
    if (probe::audio)
        ++probe::allocations;
#if defined(_WIN32)
    auto *p = _aligned_malloc(n ? n : 1, static_cast<std::size_t>(a));
#else
    void *p = nullptr;
    if (posix_memalign(&p, static_cast<std::size_t>(a), n ? n : 1) != 0)
        p = nullptr;
#endif
    if (p)
        return p;
    throw std::bad_alloc();
}
void operator delete(void *p, std::align_val_t) noexcept {
    if (p && probe::audio)
        ++probe::deallocations;
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void *operator new[](std::size_t n, std::align_val_t a) {
    return ::operator new(n, a);
}
void operator delete[](void *p, std::align_val_t a) noexcept {
    ::operator delete(p, a);
}
void operator delete(void *p, std::size_t, std::align_val_t a) noexcept {
    ::operator delete(p, a);
}
void operator delete[](void *p, std::size_t, std::align_val_t a) noexcept {
    ::operator delete(p, a);
}
