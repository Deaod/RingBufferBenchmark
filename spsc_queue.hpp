#pragma once

#include <atomic>
#include <cstddef>
#include <utility>
#include <type_traits>

template<typename _element_type, int _queue_size_log2, int _align_log2 = 7>
struct alignas((size_t) 1 << _align_log2) spsc_queue {
    using value_type = _element_type;
    static const auto size = size_t(1) << _queue_size_log2;
    static const auto mask = size - 1;
    static const auto align = size_t(1) << _align_log2;

    template<typename... Args>
    bool produce(Args&&... args) noexcept(std::is_nothrow_constructible_v<value_type, Args...>) {
        static_assert(
            std::is_constructible_v<value_type, Args...>,
            "value_type must be constructible from Args..."
        );

        auto produce_pos = _produce_pos.load(std::memory_order_relaxed);
        auto next_index = produce_pos + 1;
        if (next_index == size) {
            next_index = 0;
        }

        auto consume_pos = _consume_pos.load(std::memory_order_acquire);
        if (next_index == consume_pos) {
            return false;
        }

        new(_buffer + produce_pos * sizeof(value_type)) value_type(std::forward<Args>(args)...);

        _produce_pos.store(next_index, std::memory_order_release);
        return true;
    }

    template<typename callable>
    bool consume(callable&& callback) noexcept(noexcept(callback(static_cast<value_type*>(nullptr)))) {
        auto consume_pos = _consume_pos.load(std::memory_order_relaxed);
        auto produce_pos = _produce_pos.load(std::memory_order_acquire);

        if (produce_pos == consume_pos) {
            return false;
        }

        value_type* elem = reinterpret_cast<value_type*>(_buffer + consume_pos * sizeof(value_type));
        if (callback(elem)) {
            elem->~value_type();

            auto next_index = consume_pos + 1;
            if (next_index == size) {
                next_index = 0;
            }

            _consume_pos.store(next_index, std::memory_order_release);

            return true;
        }

        return false;
    }

    // returns true if buffer is empty after this call
    template<typename callable>
    bool consume_all(callable&& callback) noexcept(noexcept(callback(static_cast<value_type*>(nullptr)))) {
        auto consume_pos = _consume_pos.load(std::memory_order_acquire);
        auto produce_pos = _produce_pos.load(std::memory_order_acquire);

        if (produce_pos == consume_pos)
            return true;

        scope_guard g([this, &consume_pos] {
            _consume_pos.store(consume_pos, std::memory_order_release);
        });

        while (consume_pos != produce_pos) {
            while (consume_pos != produce_pos) {
                value_type* elem = reinterpret_cast<value_type*>(_buffer + consume_pos * sizeof(value_type));

                if (callback(elem) == false) {
                    return false;
                }

                elem->~value_type();

                consume_pos += 1;
                if (consume_pos == size) {
                    consume_pos = 0;
                }
            }

            produce_pos = _produce_pos.load(std::memory_order_acquire);
        }

        return (consume_pos == produce_pos);
    }

    bool is_empty() const {
        auto consume_pos = _consume_pos.load(std::memory_order_acquire);
        auto produce_pos = _produce_pos.load(std::memory_order_acquire);

        return consume_pos == produce_pos;
    }

private:
    alignas(align) std::byte _buffer[size * sizeof(_element_type)]{};

    alignas(align) std::atomic<size_t> _produce_pos = 0;
    mutable size_t _consume_pos_cache = 0;

    alignas(align) std::atomic<size_t> _consume_pos = 0;
    mutable size_t _produce_pos_cache = 0;
};

template<typename _element_type, int _queue_size_log2, int _align_log2 = 7>
struct alignas((size_t) 1 << _align_log2) spsc_queue_cached {
    using value_type = _element_type;
    static const auto size = size_t(1) << _queue_size_log2;
    static const auto mask = size - 1;
    static const auto align = size_t(1) << _align_log2;

    template<typename... Args>
    bool produce(Args&&... args) noexcept(std::is_nothrow_constructible_v<value_type, Args...>) {
        static_assert(
            std::is_constructible_v<value_type, Args...>,
            "value_type must be constructible from Args..."
        );

        auto produce_pos = _produce_pos.load(std::memory_order_relaxed);
        auto next_index = produce_pos + 1;
        if (next_index == size) {
            next_index = 0;
        }

        auto consume_pos = _consume_pos_cache;
        if (next_index == consume_pos) {
            consume_pos = _consume_pos_cache = _consume_pos.load(std::memory_order_acquire);
            if (next_index == consume_pos) {
                return false;
            }
        }

        new(_buffer + produce_pos * sizeof(value_type)) value_type(std::forward<Args>(args)...);

        _produce_pos.store(next_index, std::memory_order_release);
        return true;
    }

    template<typename callable>
    bool consume(callable&& callback) noexcept(noexcept(callback(static_cast<value_type*>(nullptr)))) {
        auto consume_pos = _consume_pos.load(std::memory_order_relaxed);
        auto produce_pos = _produce_pos_cache;

        if (produce_pos == consume_pos) {
            produce_pos = _produce_pos_cache = _produce_pos.load(std::memory_order_acquire);
            if (produce_pos == consume_pos) {
                return false;
            }
        }

        value_type* elem = reinterpret_cast<value_type*>(_buffer + consume_pos * sizeof(value_type));
        if (callback(elem)) {
            elem->~value_type();

            auto next_index = consume_pos + 1;
            if (next_index == size) {
                next_index = 0;
            }

            _consume_pos.store(next_index, std::memory_order_release);
            
            return true;
        }

        return false;
    }

    // returns true if buffer is empty after this call
    template<typename callable>
    bool consume_all(callable&& callback) noexcept(noexcept(callback(static_cast<value_type*>(nullptr)))) {
        auto consume_pos = _consume_pos.load(std::memory_order_acquire);
        auto produce_pos = _produce_pos.load(std::memory_order_acquire);

        if (produce_pos == consume_pos)
            return true;

        scope_guard g([this, &consume_pos] {
            _consume_pos.store(consume_pos, std::memory_order_release);
        });

        while (consume_pos != produce_pos) {
            while (consume_pos != produce_pos) {
                value_type* elem = reinterpret_cast<value_type*>(_buffer + consume_pos * sizeof(value_type));

                if (callback(elem) == false) {
                    return false;
                }

                elem->~value_type();

                consume_pos += 1;
                if (consume_pos == size) {
                    consume_pos = 0;
                }
            }

            produce_pos = _produce_pos.load(std::memory_order_acquire);
        }

        return (consume_pos == produce_pos);
    }

    bool is_empty() const {
        auto consume_pos = _consume_pos.load(std::memory_order_acquire);
        auto produce_pos = _produce_pos.load(std::memory_order_acquire);

        return consume_pos == produce_pos;
    }

private:
    alignas(align) std::byte _buffer[size * sizeof(value_type)]{};

    alignas(align) std::atomic<size_t> _produce_pos = 0;
    mutable size_t _consume_pos_cache = 0;

    alignas(align) std::atomic<size_t> _consume_pos = 0;
    mutable size_t _produce_pos_cache = 0;
};
