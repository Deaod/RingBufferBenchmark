#pragma once

#include <atomic>
#include <array>
#include <cstddef>

template<typename T, std::size_t SIZE>
struct ChunkedQueue1 {
    using value_type = T;
    static const auto align = size_t(64);
    static const auto chunk_size_bytes = (size_t(1) << 15) - (3 * align);

    static const auto chunk_max_size = chunk_size_bytes / sizeof(value_type);
    static const auto chunk_count =
        ((SIZE + chunk_max_size - 1) / chunk_max_size);
    static const auto chunk_size =
        (SIZE / chunk_count) + (((SIZE % chunk_count) == 0) ? 0 : 1);

    struct alignas(align) chunk {
        chunk() :
            tail(),
            head()
        {}

        alignas(align) std::atomic_size_t tail;
        alignas(align) std::atomic_size_t head;
        alignas(align) chunk* next;
        alignas(align) std::array<std::byte, sizeof(T) * chunk_size> buffer;
    };

    alignas(align) std::atomic<chunk*> tail_chunk;
    alignas(align) std::atomic<chunk*> head_chunk;
    alignas(align) std::array<chunk, chunk_count> chunks;

    ChunkedQueue1() :
        chunks{},
        head_chunk(chunks.data()),
        tail_chunk(chunks.data())
    {
        for (size_t i = 0; i < chunk_count - 1; i += 1) {
            chunks[i].next = &chunks[i + 1];
        }
        chunks[chunk_count - 1].next = chunks.data();
    }

    template<typename... Args>
    int Enqueue(Args&&... args) {
        chunk* tc = tail_chunk;
        size_t t = tc->tail;
        size_t n = (t + 1);
        if (n == chunk_size) n = 0;

        size_t h = tc->head;
        if (n == h) {
            tc = tc->next;
            if (tc == head_chunk)
                return 0;

            t = tc->tail;
            n = (t + 1);
            if (n == chunk_size) n = 0;

            new(tc->buffer.data() + t * sizeof(T))
                T(std::forward<Args>(args)...);
            tc->tail = n;
            tail_chunk = tc;
            return 1;
        }

        new(tc->buffer.data() + t * sizeof(T)) T(std::forward<Args>(args)...);
        tc->tail = n;

        return 1;
    }

    template<typename Callable>
    int Dequeue(Callable&& f) {
        chunk* hc = head_chunk;
        // this line needs to stay right here, before we load the latest
        // value of hc->tail
        chunk* tc = tail_chunk;

        size_t h = hc->head;
        size_t t = hc->tail;

        if (t == h) {
            if (hc == tc)
                return 0;

            hc = hc->next;
            h = hc->head;
            t = hc->tail;

            if (t == h) return 0;

            T* elem = std::launder(reinterpret_cast<T*>(
                hc->buffer.data() + h * sizeof(T)));
            std::forward<Callable>(f)(std::move(*elem));
            elem->~T();

            h += 1;
            if (h == chunk_size) h = 0;
            hc->head = h;
            head_chunk = hc;
            return 1;
        }

        T* elem = std::launder(reinterpret_cast<T*>(
            hc->buffer.data() + h * sizeof(T)));
        std::forward<Callable>(f)(std::move(*elem));
        elem->~T();

        h += 1;
        if (h == chunk_size) h = 0;
        hc->head = h;
        return 1;
    }
};
