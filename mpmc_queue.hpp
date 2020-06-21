#pragma once

#include <array>
#include <atomic>
#include <cstddef>

template<typename _type, std::size_t _size, int _align_log2 = 7>
struct alignas(1 << _align_log2) mpmc_queue {
    using value_type = _type;
    using size_type = std::size_t;
    static const auto size = _size;
    static const auto align = size_type(1) << _align_log2;
    
    alignas(align) std::array<std::size_t, size> _unused{};
    std::atomic_size_t _unused_head{ 0 };

    alignas(align) std::array<std::byte, sizeof(_type) * size> _storage;
    std::atomic_size_t _storage_head{ 1 };

    alignas(align) std::array<std::atomic_size_t, size> _queue{};
    alignas(align) std::atomic_size_t _tail{};
    alignas(align) std::atomic_size_t _head{};

    static const std::size_t TOMBSTONE = ~std::size_t(0);

    mpmc_queue() {}

    std::size_t allocate() {
        auto unused_head = _unused_head.load(std::memory_order_acquire);
        while (true) {
            if (unused_head == 0) {
                // get new storage
                auto storage_head = _storage_head.load(std::memory_order_acquire);
                while (true) {
                    if (storage_head >= size)
                        return 0;
                    if (_storage_head.compare_exchange_weak(storage_head, storage_head + 1, std::memory_order_acq_rel))
                        return storage_head;
                }
            } 
            if (_unused_head.compare_exchange_weak(unused_head, _unused[unused_head], std::memory_order_acq_rel))
                return unused_head;
        }
    }

    void free(std::size_t idx) {
        auto unused_head = _unused_head.load(std::memory_order_acquire);
        while (true) {
            _unused[idx] = unused_head;
            if (_unused_head.compare_exchange_weak(unused_head, idx, std::memory_order_acq_rel))
                break;
        }
    }

    template<typename... Args>
    bool Enqueue(Args&&... args) {
        auto idx = allocate();
        if (idx == 0) return false;

        new(_storage.data() + idx * sizeof(_type)) _type(std::forward<Args>(args)...);

        auto tail = _tail.load(std::memory_order_acq_rel);
        while (true) {
            auto next = tail + 1;
            if (next == size) next = 0;
            if (_tail.compare_exchange_weak(tail, next, std::memory_order_acq_rel))
                break;
        }
        _queue[tail].store(idx, std::memory_order_release);
        return true;
    }

    template<typename Callback>
    bool Dequeue(Callback&& f) {
        auto head = _head.load(std::memory_order_acquire);
        while (true) {
            auto tail = _tail.load(std::memory_order_acquire);
            if (head == tail) return false;
            auto next = head + 1;
            if (next == size) next = 0;
            if (_head.compare_exchange_weak(head, next, std::memory_order_acq_rel))
                break;
        }

        while (_queue[head].load(std::memory_order_acquire) == 0) {}

        return true;
    }
};
