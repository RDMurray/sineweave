// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "Playback.h"
#include <array>
#include <atomic>
#include <memory>
namespace sineweave {
template <class T, std::size_t Capacity> class SpscQueue {
    static_assert(Capacity > 1);
    std::array<T, Capacity> slots{};
    alignas(64) std::atomic<std::size_t> write{0};
    alignas(64) std::atomic<std::size_t> read{0};

  public:
    bool push(T value) noexcept {
        auto w = write.load(std::memory_order_relaxed);
        auto next = (w + 1) % Capacity;
        if (next == read.load(std::memory_order_acquire))
            return false;
        slots[w] = value;
        write.store(next, std::memory_order_release);
        return true;
    }
    bool canPush() const noexcept {
        return (write.load(std::memory_order_relaxed) + 1) % Capacity != read.load(std::memory_order_acquire);
    }
    bool pop(T &value) noexcept {
        auto r = read.load(std::memory_order_relaxed);
        if (r == write.load(std::memory_order_acquire))
            return false;
        value = slots[r];
        read.store((r + 1) % Capacity, std::memory_order_release);
        return true;
    }
};
struct PreparedFrame {
    PreparedFrame() = default;
    PreparedFrame(const PreparedFrame &) = delete;
    PreparedFrame &operator=(const PreparedFrame &) = delete;
    std::uint64_t generation = 0;
    SpectralFrame frame;
    bool previewOnAdopt = false;
    const PlaybackProgram *program = nullptr;
    PlaybackSettings playback;
    bool trajectory = false;
    ~PreparedFrame() {
        if (program)
            program->release();
    }
    void attach(const PlaybackProgram *p) noexcept {
        if (p)
            p->retain();
        if (program)
            program->release();
        program = p;
    }
};
// Worker owns publication/reclamation. Destruction requires audio and worker stopped.
class FrameExchange {
    SpscQueue<PreparedFrame *, 4> incoming, retired;
    PreparedFrame *current = nullptr;

  public:
    ~FrameExchange() {
        clearStopped();
    }
    void clearStopped() noexcept {
        delete current;
        current = nullptr;
        PreparedFrame *p;
        while (incoming.pop(p))
            delete p;
        while (retired.pop(p))
            delete p;
    }
    const PreparedFrame *prepared() const noexcept {
        return current;
    }
    bool publish(std::unique_ptr<PreparedFrame> &p) noexcept {
        if (!incoming.push(p.get()))
            return false;
        p.release();
        return true;
    }
    void reclaim() noexcept {
        PreparedFrame *p;
        while (retired.pop(p))
            delete p;
    }
    const SpectralFrame *adopt(std::uint64_t desiredGeneration, bool *preview = nullptr) noexcept {
        if (preview)
            *preview = false;
        if (!retired.canPush())
            return current ? &current->frame : nullptr;
        PreparedFrame *next;
        if (incoming.pop(next)) {
            if (next->generation != desiredGeneration)
                retired.push(next);
            else {
                if (current)
                    retired.push(current);
                current = next;
                if (preview)
                    *preview = next->previewOnAdopt;
            }
        }
        return current ? &current->frame : nullptr;
    }
};
} // namespace sineweave
