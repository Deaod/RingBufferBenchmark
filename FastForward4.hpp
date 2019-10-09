#pragma once

#include <atomic>
#include <array>
#include <cstddef>

template<typename T, std::size_t SIZE>
struct FastForward4 {
    using value_type = T;
    size_t tail;
    size_t head;
    std::array<std::atomic<T*>, SIZE> buffer;

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
        e.store(NULL, std::memory_order_relaxed);
        h += 1;
        if (h == SIZE) h = 0;
        head = h;
        return 1;
    }
};
