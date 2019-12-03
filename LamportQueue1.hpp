#pragma once

#include <atomic>
#include <array>
#include <cstddef>

template<typename T, size_t SIZE>
struct LamportQueue1 {
    volatile int tail;
    volatile int head;
    T buffer[SIZE];

    int Enqueue(T element) {
        int t = tail;
        int h = head;
        int n = (t + 1) % SIZE;
        if (h == n) return 0;
        buffer[t] = element;
        tail = n;
        return 1;
    }

    int Dequeue(T& element) {
        int h = head;
        int t = tail;
        if (h == t) return 0;
        element = buffer[h];
        head = (h + 1) % SIZE;
        return 1;
    }
};

