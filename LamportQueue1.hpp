#pragma once

#include <atomic>
#include <array>
#include <cstddef>

template<typename T, std::size_t SIZE>
struct LamportQueue1 {
    using value_type = T;
    volatile int tail;
    volatile int head;
    T buffer[SIZE];

    int Enqueue(T e) {
        size_t t = tail;
        size_t n = (t + 1) % SIZE;
        size_t h = head;
        if (h == n) return 0;
        buffer[t] = e;
        tail = n;
        return 1;
    }

    int Dequeue(T& e) {
        size_t h = head;
        size_t t = tail;
        if (h == t) return 0;
        e = buffer[h];
        head = (h + 1) % SIZE;
        return 1;
    }
};

