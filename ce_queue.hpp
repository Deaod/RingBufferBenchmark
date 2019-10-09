#pragma once

#include <array>
#include <atomic>
#include <cstddef>

template<typename T, size_t _queue_size, int _align_log2 = 7>
struct ce_queue {
    using value_type = T;
    static const auto size = _queue_size;
    static const auto align = size_t(1) << _align_log2;

    struct alignas(alignof(value_type)) node {
        std::array<std::byte, sizeof(value_type)> storage;
        std::atomic_bool occupied{ false };
    };
    
    ce_queue() = default;

    template<typename... Args>
    bool emplace(Args&& ... args) {
        auto produce_pos = _produce_pos;
        node& elem = _buffer[produce_pos];
        if (elem.occupied.load(std::memory_order_acquire)) {
            return false;
        }

        new(elem.storage.data()) T(std::forward<Args>(args)...);
        elem.occupied.store(true, std::memory_order_release);
        produce_pos += 1;
        if (produce_pos == size) produce_pos = 0;
        _produce_pos = produce_pos;

        return true;
    }

    template<typename Callable>
    bool consume(Callable&& f) {
        auto consume_pos = _consume_pos;

        auto& elem = _buffer[consume_pos];
        if (elem.occupied.load(std::memory_order_acquire) == false)
            return false;

        auto ptr = std::launder(reinterpret_cast<T*>(elem.storage.data()));
        if (std::forward<Callable>(f)(ptr)) {
            ptr->~value_type();
            elem.occupied.store(false, std::memory_order_release);

            consume_pos += 1;
            if (consume_pos == size) consume_pos = 0;
            _consume_pos = consume_pos;
            return true;
        }
        return false;
    }

    bool is_empty() const {
        return _buffer[_consume_pos].occupied.load(std::memory_order_acquire) == false;
    }

private:
    alignas(align) std::array<node, size> _buffer;
    alignas(align) size_t _produce_pos = 0;
    alignas(align) size_t _consume_pos = 0;
};

template<typename T, size_t _queue_size, int _align_log2 = 7>
struct ce_queue2 {
    using value_type = T;
    static const auto size = _queue_size;
    static const auto align = size_t(1) << _align_log2;

    ce_queue2() = default;

    template<typename... Args>
    bool emplace(Args&& ... args) {
        auto produce_pos = _produce_pos;
        auto& info = _info[produce_pos];
        if (info.load(std::memory_order_acquire)) {
            return false;
        }

        new(_buffer.data() + produce_pos * sizeof(T))
            T(std::forward<Args>(args)...);
        info.store(true, std::memory_order_release);
        produce_pos += 1;
        if (produce_pos == size) produce_pos = 0;
        _produce_pos = produce_pos;

        return true;
    }

    template<typename Callable>
    bool consume(Callable&& f) {
        auto consume_pos = _consume_pos;

        auto& info = _info[consume_pos];
        if (info.load(std::memory_order_acquire) == false) {
            return false;
        }

        auto ptr = std::launder(reinterpret_cast<T*>(
                _buffer.data() + consume_pos * sizeof(T)));
        if (std::forward<Callable>(f)(ptr)) {
            ptr->~value_type();
            info.store(false, std::memory_order_release);

            consume_pos += 1;
            if (consume_pos == size) consume_pos = 0;
            _consume_pos = consume_pos;
            return true;
        }

        return false;
    }

    bool is_empty() const {
        return _info[_consume_pos].load(std::memory_order_acquire) == false;
    }

private:
    alignas(align) std::array<std::atomic_bool, size> _info;
    alignas(align) std::array<std::byte, size * sizeof(T)> _buffer;
    alignas(align) size_t _produce_pos = 0;
    alignas(align) size_t _consume_pos = 0;
};
