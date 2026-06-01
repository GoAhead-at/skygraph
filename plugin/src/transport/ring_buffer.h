#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <string>
#include <utility>

namespace skygraph::transport {

// Bounded single-producer / single-consumer ring of strings. Game-thread
// samplers push serialized NDJSON records; the writer thread drains them.
//
// On overflow we drop the NEW record (returning false from TryPush) rather
// than the oldest, which preserves the strict SPSC invariant that only the
// consumer writes _tail. A drop counter is incremented so we can report
// total drops periodically. For telemetry data this means: if the pipe
// stalls, we keep the older context-rich records around and lose the most
// recent ones. The writer thread periodically logs the drop count so the
// user can tell when overflow happened.
//
// Capacity is rounded up to the next power of two for cheap modulo via mask.
//
// _head, _tail and _dropped are each alignas(64) so they land on separate
// cache lines -- this is the whole point (it stops the producer's _head write
// from invalidating the consumer's _tail line and vice versa). MSVC's C4324
// just reports the padding that alignment necessarily introduces, so we
// silence it here; the padding is intentional, not a mistake.
#if defined(_MSC_VER)
#    pragma warning(push)
#    pragma warning(disable : 4324)  // structure padded due to alignas
#endif
class StringSpscRing {
public:
    explicit StringSpscRing(std::size_t a_capacityHint);

    StringSpscRing(const StringSpscRing&) = delete;
    StringSpscRing& operator=(const StringSpscRing&) = delete;

    // Producer side. Move-friendly.
    bool TryPush(std::string a_record) noexcept;

    // Consumer side. Returns false if empty.
    bool TryPop(std::string& a_out) noexcept;

    std::size_t DroppedCount() const noexcept {
        return _dropped.load(std::memory_order_relaxed);
    }

    std::size_t Capacity() const noexcept { return _capacity; }

private:
    static constexpr std::size_t kCacheLine = 64;

    static std::size_t RoundUpPow2(std::size_t a_v) {
        std::size_t c = 1;
        while (c < a_v) c <<= 1;
        return c;
    }

    const std::size_t _capacity;
    const std::size_t _mask;
    std::unique_ptr<std::string[]> _slots;

    alignas(kCacheLine) std::atomic<std::size_t> _head{ 0 };  // producer-only
    alignas(kCacheLine) std::atomic<std::size_t> _tail{ 0 };  // consumer-only
    alignas(kCacheLine) std::atomic<std::size_t> _dropped{ 0 };
};
#if defined(_MSC_VER)
#    pragma warning(pop)
#endif

inline StringSpscRing::StringSpscRing(std::size_t a_capacityHint)
    : _capacity{ RoundUpPow2(a_capacityHint == 0 ? 1 : a_capacityHint) },
      _mask{ _capacity - 1 },
      _slots{ std::make_unique<std::string[]>(_capacity) } {}

inline bool StringSpscRing::TryPush(std::string a_record) noexcept {
    const auto head = _head.load(std::memory_order_relaxed);
    const auto tail = _tail.load(std::memory_order_acquire);

    if ((head - tail) >= _capacity) {
        _dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    _slots[head & _mask] = std::move(a_record);
    _head.store(head + 1, std::memory_order_release);
    return true;
}

inline bool StringSpscRing::TryPop(std::string& a_out) noexcept {
    const auto tail = _tail.load(std::memory_order_relaxed);
    const auto head = _head.load(std::memory_order_acquire);
    if (tail == head) return false;
    a_out = std::move(_slots[tail & _mask]);
    _tail.store(tail + 1, std::memory_order_release);
    return true;
}

}  // namespace skygraph::transport
