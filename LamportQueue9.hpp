#pragma once

#include <atomic>
#include <array>
#include <cstddef>

template<typename T, std::size_t SIZE>
struct LamportQueue9 {
    using value_type = T;
    std::atomic_size_t tail;
    std::atomic_size_t head;
    std::array<std::byte, sizeof(T) * SIZE> buffer;

    template<typename... Args>
    int Enqueue(Args&&... args) {
        size_t t = tail.load(std::memory_order_relaxed);
        size_t n = (t + 1) % SIZE;
        size_t h = head.load(std::memory_order_acquire);
        if (h == n) return 0;
        new(buffer.data() + t * sizeof(T)) T(std::forward<Args>(args)...);
        tail.store(n, std::memory_order_release);
        return 1;
    }

    template<typename Callable>
    int Dequeue(Callable&& f) {
        size_t h = head.load(std::memory_order_relaxed);
        size_t t = tail.load(std::memory_order_acquire);
        if (h == t) return 0;
        T* elem = std::launder(reinterpret_cast<T*>(
            buffer.data() + h * sizeof(T)));
        std::invoke(std::forward<Callable>(f), std::move(*elem));
        elem->~T();
        h = (h + 1) % SIZE;
        head.store(h, std::memory_order_release);
        return 1;
    }
};

