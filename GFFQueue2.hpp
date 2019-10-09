#pragma once

#include <atomic>
#include <array>
#include <cstddef>

template<typename T, std::size_t SIZE>
struct GFFQueue2 {
    using value_type = T;

    struct node {
        std::atomic_bool occupied;
        alignas(alignof(T)) std::array<std::byte, sizeof(T)> storage;
    };

    std::size_t tail;
    std::size_t head;
    std::array<node, SIZE> buffer;

    template<typename... Args>
    int Enqueue(Args&&... args) {
        size_t t = tail;
        node& n = buffer[t];
        if (n.occupied) return 0;
        new(n.storage.data()) T(std::forward<Args>(args)...);
        n.occupied = true;
        t += 1;
        if (t == SIZE) t = 0;
        tail = t;
        return 1;
    }

    template<typename Callable>
    int Dequeue(Callable&& f) {
        size_t h = head;
        node& n = buffer[h];
        if (n.occupied == false) return 0;
        T* elem = std::launder(reinterpret_cast<T*>(n.storage.data()));
        std::forward<Callable>(f)(std::move(*elem));
        elem->~T();
        n.occupied = false;
        h += 1;
        if (h == SIZE) h = 0;
        head = h;
        return 1;
    }
};

