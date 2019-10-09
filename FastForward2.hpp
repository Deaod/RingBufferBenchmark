#pragma once

#include <atomic>
#include <array>
#include <cstddef>

template<typename T, std::size_t SIZE>
struct FastForward2 {
    using value_type = T;
    size_t tail;
    size_t head;
    std::array<std::atomic<T*>, SIZE> buffer;

    int Enqueue(T* val) {
        size_t t = tail;
        auto& e = buffer[t];
        if (e.load()) return 0;
        e.store(val);
        t += 1;
        if (t == SIZE) t = 0;
        tail = t;
        return 1;
    }

    int Dequeue(T*& out) {
        size_t h = head;
        auto& e = buffer[h];
        out = e.load();
        if (!out) return 0;
        e.store(NULL);
        h += 1;
        if (h == SIZE) h = 0;
        head = h;
        return 1;
    }
};
