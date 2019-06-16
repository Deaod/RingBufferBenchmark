#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <array>
#include <limits>
#include "aligned_alloc.hpp"
#include "compile_time_utilities.hpp"
#include "scope_guard.hpp"

template<
    int _buffer_size_log2,
    int _l1d_size_log2 = 15,
    int _align_log2 = 7
>
struct alignas(((size_t)1) << _align_log2) spsc_ring_buffer_chunked {
    using difference_type = ptrdiff_t;
    static const auto size = size_t(1) << _buffer_size_log2;
    static const auto mask = ctu::bit_mask_v<size_t, _buffer_size_log2>;
    static const auto align = size_t(1) << _align_log2;
    static const auto chunk_size = size_t(1) << _l1d_size_log2;
    static const auto chunk_mask = chunk_size - 1;
    static const auto chunk_count = size_t(1) << (_buffer_size_log2 - _l1d_size_log2);
    static const auto content_align_log2 = ctu::log2_v<sizeof(void*)>;

    spsc_ring_buffer_chunked() :
        _chunks{},
        _head(_chunks.data()),
        _tail(_chunks.data())
    {
        for (size_t i = 0; i < chunk_count - 1; i += 1) {
            _chunks[i]._next = &_chunks[i + 1];
        }
        _chunks[chunk_count - 1]._next = _chunks.data();
    }

    struct alignas(align) chunk {
        chunk() :
            _produce_pos(),
            _consume_pos_cache(),
            _consume_pos(),
            _produce_pos_cache()
        {}

        bool is_empty() const noexcept {
            auto produce_pos = _produce_pos.load(std::memory_order_acquire);
            auto consume_pos = _consume_pos.load(std::memory_order_acquire);

            return produce_pos == consume_pos;
        }

        alignas(align) std::byte _buffer[chunk_size];

        alignas(align) std::atomic<size_t> _produce_pos = 0;
        mutable size_t _consume_pos_cache = 0;

        alignas(align) std::atomic<size_t> _consume_pos = 0;
        mutable size_t _produce_pos_cache = 0;

        alignas(align) chunk* _next = 0;
    };

    template<typename cbtype>
    bool produce(size_t length, cbtype callback) noexcept(noexcept(callback(static_cast<void*>(nullptr)))) {
        if (length <= 0 || length >= (chunk_size/2))
            return false;

        auto rounded_length = ctu::round_up_bits(length + sizeof(difference_type), content_align_log2);

        auto head = _head.load(std::memory_order_relaxed);

        auto produce_pos = head->_produce_pos.load(std::memory_order_relaxed);
        auto consume_pos = head->_consume_pos_cache;

        if ((consume_pos != produce_pos) && (((consume_pos - produce_pos) & chunk_mask) <= rounded_length)) {
            consume_pos = head->_consume_pos_cache = head->_consume_pos.load(std::memory_order_acquire);
            if ((consume_pos != produce_pos) && (((consume_pos - produce_pos) & chunk_mask) <= rounded_length)) {
                goto next_chunk;
            }
        }

        if (auto wrap_distance = chunk_size - produce_pos; wrap_distance < rounded_length) {
            if (consume_pos <= rounded_length) {
                consume_pos = head->_consume_pos_cache = head->_consume_pos.load(std::memory_order_acquire);
                if (consume_pos <= rounded_length) {
                    goto next_chunk;
                }
            }

            new (head->_buffer + produce_pos) difference_type(-difference_type(wrap_distance));
            produce_pos = 0;
        }

        new (head->_buffer + produce_pos) difference_type(length);
        if (callback(static_cast<void*>(head->_buffer + produce_pos + sizeof(difference_type)))) {
            head->_produce_pos.store((produce_pos + rounded_length) & chunk_mask, std::memory_order_release);
            return true;
        }

        return false;


    next_chunk:

        head = head->_next;
        auto tail = _tail.load(std::memory_order_acquire);

        if (head == tail)
            return false;

        produce_pos = head->_produce_pos.load(std::memory_order_relaxed);
        head->_consume_pos_cache = head->_consume_pos.load(std::memory_order_acquire);

        if (auto wrap_distance = chunk_size - produce_pos; wrap_distance < rounded_length) {
            new (head->_buffer + produce_pos) difference_type(-difference_type(wrap_distance));
            produce_pos = 0;
        }

        new (head->_buffer + produce_pos) difference_type(length);
        if (callback(static_cast<void*>(head->_buffer + produce_pos + sizeof(difference_type)))) {
            head->_produce_pos.store((produce_pos + rounded_length) & chunk_mask, std::memory_order_release);
            _head.store(head, std::memory_order_release);
            return true;
        }

        return false;
    }

    template<typename cbtype>
    bool consume(cbtype callback) noexcept(noexcept(callback(static_cast<const void*>(nullptr), difference_type(0)))) {
        auto tail = _tail.load(std::memory_order_relaxed);
        
        auto produce_pos = tail->_produce_pos_cache;
        auto consume_pos = tail->_consume_pos.load(std::memory_order_relaxed);

        if (produce_pos == consume_pos) {
            auto head = _head.load(std::memory_order_acquire);
            produce_pos = tail->_produce_pos_cache = tail->_produce_pos.load(std::memory_order_acquire);
            if (produce_pos == consume_pos) {
                if (tail == head) {
                    return false;
                }

                tail = tail->_next;

                consume_pos = tail->_consume_pos.load(std::memory_order_relaxed);
                produce_pos = tail->_produce_pos_cache = tail->_produce_pos.load(std::memory_order_acquire);

                if (produce_pos == consume_pos) {
                    return false;
                }

                difference_type length;
                memcpy(&length, tail->_buffer + consume_pos, sizeof(length));

                if (length < 0) {
                    consume_pos = 0;
                    memcpy(&length, tail->_buffer, sizeof(length));
                }

                if (callback(static_cast<const void*>(tail->_buffer + consume_pos + sizeof(difference_type)), length)) {
                    auto rounded_length = ctu::round_up_bits(length + sizeof(difference_type), content_align_log2);
                    tail->_consume_pos.store((consume_pos + rounded_length) & chunk_mask, std::memory_order_release);
                    _tail.store(tail, std::memory_order_release);
                    return true;
                }
            }
        }

        difference_type length;
        memcpy(&length, tail->_buffer + consume_pos, sizeof(length));

        if (length < 0) {
            consume_pos = 0;
            memcpy(&length, tail->_buffer, sizeof(length));
        }

        if (callback(static_cast<const void*>(tail->_buffer + consume_pos + sizeof(difference_type)), length)) {
            auto rounded_length = ctu::round_up_bits(length + sizeof(difference_type), content_align_log2);
            tail->_consume_pos.store((consume_pos + rounded_length) & chunk_mask, std::memory_order_release);
            return true;
        }

        return false;
    }

    // returns true if buffer is empty after this call
    template<typename cbtype>
    bool consume_all(cbtype callback) noexcept(noexcept(callback(static_cast<const void*>(nullptr), difference_type(0)))) {
        //auto consume_pos = _consume_pos.load(std::memory_order_relaxed);
        //auto produce_pos = _produce_pos.load(std::memory_order_acquire);

        //if (produce_pos == consume_pos)
        //    return true;

        //scope_guard g([this, &consume_pos]() {
        //    _consume_pos.store(consume_pos, std::memory_order_release);
        //});

        //while (consume_pos != produce_pos) {
        //    while (consume_pos != produce_pos) {
        //        difference_type length;
        //        memcpy(&length, _buffer + consume_pos, sizeof(length));

        //        if (length < 0) {
        //            consume_pos = 0;
        //            memcpy(&length, _buffer + consume_pos, sizeof(length));
        //        }

        //        if (callback(static_cast<const void*>(_buffer + consume_pos + sizeof(difference_type)), length) == false) {
        //            return false;
        //        }

        //        auto rounded_length = ctu::round_up_bits(length + sizeof(difference_type), content_align_log2);
        //        consume_pos = (consume_pos + rounded_length) & chunk_mask;
        //    }

        //    produce_pos = _produce_pos.load(std::memory_order_acquire);
        //}

        return true;
    }

    bool is_empty() const noexcept {
        return _head.load(std::memory_order_acquire)->is_empty();
    }

private:
    alignas(align) std::array<chunk, chunk_count> _chunks{};
    alignas(align) std::atomic<chunk*> _head = nullptr;
    alignas(align) std::atomic<chunk*> _tail = nullptr;
};
