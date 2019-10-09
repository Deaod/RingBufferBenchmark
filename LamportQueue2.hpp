#pragma once

#include <atomic>
#include <array>
#include <cstddef>

template<typename T, std::size_t SIZE>
struct LamportQueue2 {
    using value_type = T;
    std::atomic_size_t tail;
    std::atomic_size_t head;
    alignas(alignof(T)) std::array<std::byte, sizeof(T) * SIZE> buffer;

    template<typename... Args>
    int Enqueue(Args&&... args) {
        size_t t = tail;
        size_t n = (t + 1) % SIZE;
        size_t h = head;
        if (h == n) return 0;
        new(buffer.data() + t * sizeof(T)) T(std::forward<Args>(args)...);
        tail = n;
        return 1;
    }

    template<typename Callable>
    int Dequeue(Callable&& f) {
        size_t h = head;
        size_t t = tail;
        if (h == t) return 0;
        T* elem = std::launder(reinterpret_cast<T*>(
            buffer.data() + h * sizeof(T)));
        std::forward<Callable>(f)(std::move(*elem));
        elem->~T();
        head = (h + 1) % SIZE;
        return 1;
    }
};

