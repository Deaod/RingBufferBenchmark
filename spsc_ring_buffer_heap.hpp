#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <array>
#include <limits>
#include <memory>
#include "aligned_alloc.hpp"
#include "compile_time_utilities.hpp"
#include "scope_guard.hpp"

template<
    int _buffer_size_log2,
    int _content_align_log2 = ctu::log2_v<sizeof(void*)>,
    typename _difference_type = ptrdiff_t,
    int _align_log2 = 7
>
struct alignas(((size_t)1) << _align_log2) spsc_ring_buffer_heap {
    using difference_type = _difference_type;
    static const auto size = size_t(1) << _buffer_size_log2;
    static const auto mask = ctu::bit_mask_v<size_t, _buffer_size_log2>;
    static const auto align = size_t(1) << _align_log2;
    static const auto content_align_log2 = _content_align_log2;

    static_assert(std::is_signed_v<difference_type>);
    static_assert(content_align_log2 >= ctu::log2(sizeof(difference_type)));

    spsc_ring_buffer_heap() :
        _buffer(static_cast<std::byte*>(aligned_alloc(align, size))) {}

    spsc_ring_buffer_heap(const spsc_ring_buffer_heap& other) :
        _buffer(static_cast<std::byte*>(aligned_alloc(align, size))),
        _produce_pos(other._produce_pos.load(std::memory_order_acquire)),
        _consume_pos_cache(other._consume_pos_cache),
        _consume_pos(other._consume_pos.load(std::memory_order_acquire)),
        _produce_pos_cache(other._produce_pos_cache)
    {
        memcpy(_buffer.get(), other._buffer.get(), size);
    }

    spsc_ring_buffer_heap& operator=(const spsc_ring_buffer_heap& other) {
        memcpy(_buffer.get(), other._buffer.get(), size);
        _produce_pos_cache = other._produce_pos_cache;
        _consume_pos_cache = other._consume_pos_cache;
        _produce_pos = other._produce_pos.load(std::memory_order_acquire);
        _consume_pos = other._consume_pos.load(std::memory_order_acquire);

        return *this;
    }

    spsc_ring_buffer_heap(spsc_ring_buffer_heap&& other) :
        _buffer(std::move(other._buffer)),
        _produce_pos(other._produce_pos.load(std::memory_order_acquire)),
        _consume_pos_cache(other._consume_pos_cache),
        _consume_pos(other._consume_pos.load(std::memory_order_acquire)),
        _produce_pos_cache(other._produce_pos_cache) {}

    spsc_ring_buffer_heap& operator=(spsc_ring_buffer_heap&& other) {
        _buffer = std::move(other._buffer);
        _produce_pos_cache = other._produce_pos_cache;
        _consume_pos_cache = other._consume_pos_cache;
        _produce_pos = other._produce_pos.load(std::memory_order_acquire);
        _consume_pos = other._consume_pos.load(std::memory_order_acquire);

        return *this;
    }

    template<typename cbtype>
    bool produce(size_t length, cbtype callback) noexcept(noexcept(callback(static_cast<void*>(nullptr)))) {
        if (length <= 0 || length >= size)
            return false;

        auto rounded_length = ctu::round_up_bits(length + sizeof(difference_type), content_align_log2);

        if constexpr (size >= size_t(std::numeric_limits<difference_type>::max())) {
            if (rounded_length > size_t(std::numeric_limits<difference_type>::max()))
                return false;
        }

        auto consume_pos = _consume_pos_cache;
        auto produce_pos = _produce_pos.load(std::memory_order_relaxed);

        if ((produce_pos - consume_pos) > (size - rounded_length)) {
            consume_pos = _consume_pos_cache = _consume_pos.load(std::memory_order_acquire);
            if ((produce_pos - consume_pos) > (size - rounded_length))
                return false;
        }

        auto wrap_distance = size - (produce_pos & mask);
        if (wrap_distance < rounded_length) {
            if ((produce_pos + wrap_distance - consume_pos) > (size - rounded_length)) {
                consume_pos = _consume_pos_cache = _consume_pos.load(std::memory_order_acquire);
                if ((produce_pos + wrap_distance - consume_pos) > (size - rounded_length))
                    return false;
            }

            new (_buffer.get() + (produce_pos & mask)) difference_type(-difference_type(wrap_distance));
            produce_pos += wrap_distance;
        }

        new (_buffer.get() + (produce_pos & mask)) difference_type(difference_type(length));
        if (callback(static_cast<void*>(_buffer.get() + (produce_pos & mask) + sizeof(difference_type)))) {
            _produce_pos.store(produce_pos + rounded_length, std::memory_order_release);
            return true;
        }

        return false;
    }

    template<typename cbtype>
    bool consume(cbtype callback) noexcept(noexcept(callback(static_cast<const void*>(nullptr), difference_type(0)))) {
        auto consume_pos = _consume_pos.load(std::memory_order_relaxed);
        auto produce_pos = _produce_pos_cache;

        if (produce_pos == consume_pos) {
            produce_pos = _produce_pos_cache = _produce_pos.load(std::memory_order_acquire);
            if (produce_pos == consume_pos)
                return false;
        }

        difference_type length;
        memcpy(&length, _buffer.get() + (consume_pos & mask), sizeof(length));

        if (length < 0) {
            consume_pos += -length;
            memcpy(&length, _buffer.get() + (consume_pos & mask), sizeof(length));
        }

        if (callback(static_cast<const void*>(_buffer.get() + (consume_pos & mask) + sizeof(difference_type)), length)) {
            auto rounded_length = ctu::round_up_bits(length + sizeof(difference_type), content_align_log2);
            _consume_pos.store(consume_pos + rounded_length, std::memory_order_release);
            return true;
        }

        return false;
    }

    // returns true if buffer is empty after this call
    template<typename cbtype>
    bool consume_all(cbtype callback) noexcept(noexcept(callback(static_cast<const void*>(nullptr), difference_type(0)))) {
        auto consume_pos = _consume_pos.load(std::memory_order_relaxed);
        auto produce_pos = _produce_pos.load(std::memory_order_acquire);

        if (produce_pos == consume_pos)
            return true;

        scope_guard g([this, &consume_pos]() {
            _consume_pos.store(consume_pos, std::memory_order_release);
        });

        while (consume_pos != produce_pos) {
            while (consume_pos != produce_pos) {
                difference_type length;
                memcpy(&length, _buffer.get() + (consume_pos & mask), sizeof(length));

                if (length < 0) {
                    consume_pos += -length;
                    memcpy(&length, _buffer.get() + (consume_pos & mask), sizeof(length));
                }

                if (callback(static_cast<const void*>(_buffer.get() + (consume_pos & mask) + sizeof(difference_type)), length) == false) {
                    return false;
                }

                auto rounded_length = ctu::round_up_bits(length + sizeof(difference_type), content_align_log2);
                consume_pos += rounded_length;
            }

            produce_pos = _produce_pos.load(std::memory_order_acquire);
        }

        return (consume_pos == produce_pos);
    }

    bool is_empty() const noexcept {
        auto produce_pos = _produce_pos.load(std::memory_order_acquire);
        auto consume_pos = _consume_pos.load(std::memory_order_acquire);

        return produce_pos == consume_pos;
    }

private:
    alignas(align) std::unique_ptr<std::byte, aligned_free_deleter> _buffer;

    alignas(align) std::atomic<size_t> _produce_pos = 0;
    mutable size_t _consume_pos_cache = 0;

    alignas(align) std::atomic<size_t> _consume_pos = 0;
    mutable size_t _produce_pos_cache = 0;
};
