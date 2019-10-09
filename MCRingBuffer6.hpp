#pragma once

#include <atomic>
#include <array>
#include <cstddef>

template<typename T, std::size_t SIZE>
struct MCRingBuffer6 {
    using value_type = T;
    alignas(128) std::array<std::byte, sizeof(T) * SIZE> buffer;
    alignas(128) std::atomic<T*> tail;
    mutable T* head_cache;
    alignas(128) std::atomic<T*> head;
    mutable T* tail_cache;

    MCRingBuffer6() :
        buffer(),
        tail(reinterpret_cast<T*>(buffer.data())),
        head_cache(reinterpret_cast<T*>(buffer.data())),
        head(reinterpret_cast<T*>(buffer.data())),
        tail_cache(reinterpret_cast<T*>(buffer.data())) {}

    template<typename... Args>
    int Enqueue(Args&&... args) {
        T* t = tail.load(std::memory_order_relaxed);
        T* n = (t + 1);
        if (n == reinterpret_cast<T*>(buffer.data() + buffer.max_size()))
            n = reinterpret_cast<T*>(buffer.data());
        T* h = head_cache;
        if (n == h && n == (head_cache = head.load(std::memory_order_acquire)))
            return 0;
        new(t) T(std::forward<Args>(args)...);
        tail.store(n, std::memory_order_release);
        return 1;
    }

    template<typename Callable>
    int Dequeue(Callable&& f) {
        T* h = head.load(std::memory_order_relaxed);
        T* t = tail_cache;
        if (h == t && h == (tail_cache = tail.load(std::memory_order_acquire)))
            return 0;
        T* elem = std::launder(h);
        std::forward<Callable>(f)(std::move(*elem));
        elem->~T();
        h += 1;
        if (h == reinterpret_cast<T*>(buffer.data() + buffer.max_size()))
            h = reinterpret_cast<T*>(buffer.data());
        head.store(h, std::memory_order_release);
        return 1;
    }
};

