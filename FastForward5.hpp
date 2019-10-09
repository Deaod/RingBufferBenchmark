#pragma once

#include <atomic>
#include <array>
#include <cstddef>

template<typename T, std::size_t SIZE>
struct alignas(64) FastForward5 {
    using value_type = T;
    alignas(64) size_t tail;
    alignas(64) size_t head;
    alignas(64) std::array<std::atomic<T*>, SIZE> buffer;

    int Enqueue(T* val) {
        size_t t = tail;
        auto& e = buffer[t];
        if (e.load(std::memory_order_acquire)) return 0;
        e.store(val, std::memory_order_release);
        t += 1;
        if (t == SIZE) t = 0;
        tail = t;
        return 1;
    }

    int Dequeue(T*& out) {
        size_t h = head;
        auto& e = buffer[h];
        out = e.load(std::memory_order_acquire);
        if (!out) return 0;
        e.store(NULL, std::memory_order_release);
        h += 1;
        if (h == SIZE) h = 0;
        head = h;
        return 1;
    }
};
