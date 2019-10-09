#pragma once

#include <atomic>
#include <array>
#include <cstddef>

template<typename T, std::size_t SIZE>
struct MCRingBuffer2 {
    using value_type = T;
    alignas(64) std::atomic_size_t tail;
    mutable size_t head_cache;
    alignas(64) std::atomic_size_t head;
    mutable size_t tail_cache;
    alignas(64) std::array<std::byte, sizeof(T) * SIZE> buffer;

    template<typename... Args>
    int Enqueue(Args&&... args) {
        size_t t = tail;
        size_t n = (t + 1);
        if (n == SIZE) n = 0;
        size_t h = head_cache;
        if (n == h && n == (head_cache = head)) return 0;
        new(buffer.data() + t * sizeof(T)) T(std::forward<Args>(args)...);
        tail = n;
        return 1;
    }

    template<typename Callable>
    int Dequeue(Callable&& f) {
        size_t h = head;
        size_t t = tail_cache;
        if (h == t && h == (tail_cache = tail)) return 0;
        T* elem = std::launder(reinterpret_cast<T*>(
            buffer.data() + h * sizeof(T)));
        std::forward<Callable>(f)(std::move(*elem));
        elem->~T();
        h += 1;
        if (h == SIZE) h = 0;
        head = h;
        return 1;
    }
};

