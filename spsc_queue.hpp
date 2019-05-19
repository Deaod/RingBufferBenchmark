#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <type_traits>
#include <utility>

template<typename _element_type, int _queue_size_log2, int _align_log2 = 7>
struct alignas((size_t) 1 << _align_log2) spsc_queue {
    using value_type = _element_type;
    static const auto size = size_t(1) << _queue_size_log2;
    static const auto mask = size - 1;
    static const auto align = size_t(1) << _align_log2;

    spsc_queue() = default;

    spsc_queue(const spsc_queue & other) :
        _produce_pos(other._produce_pos),
        _consume_pos_cache(other._consume_pos_cache),
        _consume_pos(other._consume_pos),
        _produce_pos_cache(other._produce_pos_cache)
    {
        auto dst = (value_type*)_buffer + _consume_pos.load();
        auto end = (value_type*)_buffer + _produce_pos.load();
        auto src = (value_type*)other._buffer + other._consume_pos.load();
        while (dst != end) {
            new(dst) value_type(*src);
            ++dst;
            ++src;
            if (dst == (value_type*)_buffer + size) {
                dst = (value_type*)_buffer;
                src = (value_type*)other._buffer;
            }
        }
    }

    spsc_queue& operator=(const spsc_queue& other) {
        if (this == &other) return *this;

        {
            auto cur = (value_type*)_buffer + _consume_pos.load();
            auto end = (value_type*)_buffer + _produce_pos.load();
            while (cur != end) {
                cur->~value_type();
                ++cur;
                if (cur == (value_type*)_buffer + size) {
                    cur = (value_type*)_buffer;
                }
            }
        }

        _produce_pos = other._produce_pos.load();
        _consume_pos_cache = other._consume_pos_cache;
        _consume_pos = other._consume_pos.load();
        _produce_pos_cache = other._produce_pos_cache;

        {
            auto dst = (value_type*)_buffer + _consume_pos.load();
            auto end = (value_type*)_buffer + _produce_pos.load();
            auto src = (value_type*)other._buffer + other._consume_pos.load();
            while (dst != end) {
                new(dst) value_type(*src);
                ++dst;
                ++src;
                if (dst == (value_type*)_buffer + size) {
                    dst = (value_type*)_buffer;
                    src = (value_type*)other._buffer;
                }
            }
        }

        return *this;
    }

    ~spsc_queue() {
        auto cur = (value_type*)_buffer + _consume_pos.load();
        auto end = (value_type*)_buffer + _produce_pos.load();
        while (cur != end) {
            cur->~value_type();
            ++cur;
            if (cur == (value_type*)_buffer + size) {
                cur = (value_type*)_buffer;
            }
        }
    }

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
    ptrdiff_t consume_all(callable&& callback) noexcept(noexcept(callback(static_cast<value_type*>(nullptr)))) {
        auto consume_pos = _consume_pos.load(std::memory_order_acquire);
        auto produce_pos = _produce_pos_cache = _produce_pos.load(std::memory_order_acquire);

        if (produce_pos == consume_pos)
            return true;

        scope_guard g([this, &consume_pos] {
            _consume_pos.store(consume_pos, std::memory_order_release);
        });

        auto old_consume_pos = consume_pos;

        while (consume_pos != produce_pos) {
            value_type* elem = reinterpret_cast<value_type*>(_buffer + consume_pos * sizeof(value_type));

            if (callback(elem) == false) {
                break;
            }

            elem->~value_type();

            consume_pos += 1;
            if (consume_pos == size) {
                consume_pos = 0;
            }
        }

        return (consume_pos - old_consume_pos) & mask;
    }

    bool is_empty() const {
        auto consume_pos = _consume_pos.load(std::memory_order_acquire);
        auto produce_pos = _produce_pos.load(std::memory_order_acquire);

        return consume_pos == produce_pos;
    }

private:
    alignas(align) std::byte _buffer[size * sizeof(_element_type)];

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

    spsc_queue_cached() = default;

    spsc_queue_cached(const spsc_queue_cached & other) :
        _produce_pos(other._produce_pos),
        _consume_pos_cache(other._consume_pos_cache),
        _consume_pos(other._consume_pos),
        _produce_pos_cache(other._produce_pos_cache)
    {
        auto dst = (value_type*)_buffer + _consume_pos.load();
        auto end = (value_type*)_buffer + _produce_pos.load();
        auto src = (value_type*)other._buffer + other._consume_pos.load();
        while (dst != end) {
            new(dst) value_type(*src);
            ++dst;
            ++src;
            if (dst == (value_type*)_buffer + size) {
                dst = (value_type*)_buffer;
                src = (value_type*)other._buffer;
            }
        }
    }

    spsc_queue_cached& operator=(const spsc_queue_cached& other) {
        if (this == &other) return *this;

        {
            auto cur = (value_type*)_buffer + _consume_pos.load();
            auto end = (value_type*)_buffer + _produce_pos.load();
            while (cur != end) {
                cur->~value_type();
                ++cur;
                if (cur == (value_type*)_buffer + size) {
                    cur = (value_type*)_buffer;
                }
            }
        }

        _produce_pos = other._produce_pos.load();
        _consume_pos_cache = other._consume_pos_cache;
        _consume_pos = other._consume_pos.load();
        _produce_pos_cache = other._produce_pos_cache;

        {
            auto dst = (value_type*)_buffer + _consume_pos.load();
            auto end = (value_type*)_buffer + _produce_pos.load();
            auto src = (value_type*)other._buffer + other._consume_pos.load();
            while (dst != end) {
                new(dst) value_type(*src);
                ++dst;
                ++src;
                if (dst == (value_type*)_buffer + size) {
                    dst = (value_type*)_buffer;
                    src = (value_type*)other._buffer;
                }
            }
        }

        return *this;
    }

    ~spsc_queue_cached() {
        auto cur = (value_type*)_buffer + _consume_pos.load();
        auto end = (value_type*)_buffer + _produce_pos.load();
        while (cur != end) {
            cur->~value_type();
            ++cur;
            if (cur == (value_type*)_buffer + size) {
                cur = (value_type*)_buffer;
            }
        }
    }

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
    ptrdiff_t consume_all(callable&& callback) noexcept(noexcept(callback(static_cast<value_type*>(nullptr)))) {
        auto consume_pos = _consume_pos.load(std::memory_order_relaxed);
        auto produce_pos = _produce_pos_cache = _produce_pos.load(std::memory_order_acquire);

        if (produce_pos == consume_pos)
            return 0;

        scope_guard g([this, &consume_pos] {
            _consume_pos.store(consume_pos, std::memory_order_release);
        });

        auto old_consume_pos = consume_pos;

        while (consume_pos != produce_pos) {
            value_type* elem = reinterpret_cast<value_type*>(_buffer + consume_pos * sizeof(value_type));

            if (callback(elem) == false) {
                break;
            }

            elem->~value_type();

            consume_pos += 1;
            if (consume_pos == size) {
                consume_pos = 0;
            }
        }

        return (consume_pos - old_consume_pos) & mask;
    }

    bool is_empty() const {
        auto consume_pos = _consume_pos.load(std::memory_order_acquire);
        auto produce_pos = _produce_pos.load(std::memory_order_acquire);

        return consume_pos == produce_pos;
    }

private:
    alignas(align) std::byte _buffer[size * sizeof(value_type)];

    alignas(align) std::atomic<size_t> _produce_pos = 0;
    mutable size_t _consume_pos_cache = 0;

    alignas(align) std::atomic<size_t> _consume_pos = 0;
    mutable size_t _produce_pos_cache = 0;
};

template<typename _element_type, int _queue_size, int _l1d_size_log2 = 15, int _align_log2 = 7>
struct alignas((size_t)1 << _align_log2) spsc_queue_chunked {
    using value_type = _element_type;
    static_assert(sizeof(value_type) <= (size_t(1) << _l1d_size_log2), "Elements must not be larger than L1D size.");

    static const auto size = _queue_size;
    static const auto align = size_t(1) << _align_log2;
    static const auto chunk_size = size_t(1) << (_l1d_size_log2 - ctu::log2_v<ctu::round_up_bits(sizeof(value_type), ctu::log2_v<sizeof(value_type)>)>);
    static const auto chunk_mask = chunk_size - 1;
    static const auto chunk_count = ((size + chunk_mask) / chunk_size);

    spsc_queue_chunked() :
        _chunks{},
        _head(_chunks.data()),
        _tail(_chunks.data())
    {
        for (size_t i = 0; i < chunk_count - 1; i += 1) {
            _chunks[i]._next = &_chunks[i + 1];
        }
        _chunks[chunk_count - 1]._next = _chunks.data();
    }

    spsc_queue_chunked(const spsc_queue_chunked & other) :
        _chunks(other._chunks),
        _head(_chunks + (other._head - other._chunks)),
        _tail(_chunks + (other._tail - other._chunks))
    {
        for (size_t i = 0; i < chunk_count - 1; i += 1) {
            _chunks[i]._next = &_chunks[i + 1];
        }
        _chunks[chunk_count - 1]._next = _chunks.data();
    }

    spsc_queue_chunked& operator=(const spsc_queue_chunked& other) {
        _chunks = other._chunks;

        for (size_t i = 0; i < chunk_count - 1; i += 1) {
            _chunks[i]._next = &_chunks[i + 1];
        }
        _chunks[chunk_count - 1]._next = _chunks.data();

        _head = &_chunks[other._head - other._chunks];
        _tail = &_chunks[other._tail - other._chunks];

        return *this;
    }

    struct alignas(align) chunk {
        chunk() :
            _produce_pos(0),
            _consume_pos_cache(0),
            _consume_pos(0),
            _produce_pos_cache(0)
        {}

        chunk(const chunk & other) :
            _produce_pos(other._produce_pos),
            _consume_pos_cache(other._consume_pos_cache),
            _consume_pos(other._consume_pos),
            _produce_pos_cache(other._produce_pos_cache)
        {
            auto dst = (value_type*)_buffer + _consume_pos.load();
            auto end = (value_type*)_buffer + _produce_pos.load();
            auto src = (value_type*)other._buffer + other._consume_pos.load();
            while (dst != end) {
                new(dst) value_type(*src);
                ++dst;
                ++src;
                if (dst == (value_type*)_buffer + size) {
                    dst = (value_type*)_buffer;
                    src = (value_type*)other._buffer;
                }
            }
        }

        chunk& operator=(const chunk & other) {
            if (this == &other) return *this;

            {
                auto cur = (value_type*)_buffer + _consume_pos.load();
                auto end = (value_type*)_buffer + _produce_pos.load();
                while (cur != end) {
                    cur->~value_type();
                    ++cur;
                    if (cur == (value_type*)_buffer + size) {
                        cur = (value_type*)_buffer;
                    }
                }
            }

            _produce_pos = other._produce_pos.load();
            _consume_pos_cache = other._consume_pos_cache;
            _consume_pos = other._consume_pos.load();
            _produce_pos_cache = other._produce_pos_cache;

            {
                auto dst = (value_type*)_buffer + _consume_pos.load();
                auto end = (value_type*)_buffer + _produce_pos.load();
                auto src = (value_type*)other._buffer + other._consume_pos.load();
                while (dst != end) {
                    new(dst) value_type(*src);
                    ++dst;
                    ++src;
                    if (dst == (value_type*)_buffer + size) {
                        dst = (value_type*)_buffer;
                        src = (value_type*)other._buffer;
                    }
                }
            }

            return *this;
        }

        ~chunk() {
            auto cur = (value_type*)_buffer + _consume_pos.load();
            auto end = (value_type*)_buffer + _produce_pos.load();
            while (cur != end) {
                cur->~value_type();
                ++cur;
                if (cur == (value_type*)_buffer + size) {
                    cur = (value_type*)_buffer;
                }
            }
        }

        bool is_empty() const {
            auto consume_pos = _consume_pos.load(std::memory_order_acquire);
            auto produce_pos = _produce_pos.load(std::memory_order_acquire);

            return consume_pos == produce_pos;
        }

        std::byte _buffer[chunk_size * sizeof(value_type)];

        alignas(align) std::atomic<size_t> _produce_pos = 0;
        mutable size_t _consume_pos_cache = 0;

        alignas(align) std::atomic<size_t> _consume_pos = 0;
        mutable size_t _produce_pos_cache = 0;

        alignas(align) chunk* _next = nullptr;
    };

    template<typename... Args>
    bool produce(Args&&... args) noexcept(std::is_nothrow_constructible_v<value_type, Args...>) {
        static_assert(
            std::is_constructible_v<value_type, Args...>,
            "value_type must be constructible from Args..."
        );

        auto head = _head.load(std::memory_order_relaxed);

        auto produce_pos = head->_produce_pos.load(std::memory_order_relaxed);
        auto next_index = (produce_pos + 1) & chunk_mask;

        auto consume_pos = head->_consume_pos_cache;
        if (next_index == consume_pos) {
            consume_pos = head->_consume_pos_cache = head->_consume_pos.load(std::memory_order_acquire);
            if (next_index == consume_pos) {
                head = head->_next;

                auto tail = _tail.load(std::memory_order_acquire);
                if (head == tail) {
                    return false;
                }

                produce_pos = head->_produce_pos.load(std::memory_order_relaxed);
                next_index = (produce_pos + 1) & chunk_mask;

                // this next line is exactly where it needs to be, unless you like deadlocks.
                head->_consume_pos_cache = head->_consume_pos.load(std::memory_order_acquire);

                new(head->_buffer + (produce_pos * sizeof(value_type))) value_type(std::forward<Args>(args)...);
                head->_produce_pos.store(next_index, std::memory_order_release);
                _head.store(head, std::memory_order_release);
                return true;
            }
        }

        new(head->_buffer + (produce_pos * sizeof(value_type))) value_type(std::forward<Args>(args)...);
        head->_produce_pos.store(next_index, std::memory_order_release);

        return true;
    }

    template<typename callable>
    bool consume(callable&& callback) noexcept(noexcept(callback(static_cast<value_type*>(nullptr)))) {
        auto tail = _tail.load(std::memory_order_relaxed);

        auto consume_pos = tail->_consume_pos.load(std::memory_order_relaxed);
        auto produce_pos = tail->_produce_pos_cache;

        if (produce_pos == consume_pos) {
            // this next line is exactly where it needs to be, unless you like deadlocks.
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

                value_type* elem = reinterpret_cast<value_type*>(tail->_buffer + (consume_pos * sizeof(value_type)));
                auto result = callback(elem);
                if (result) {
                    elem->~value_type();

                    tail->_consume_pos.store((consume_pos + 1) & chunk_mask, std::memory_order_release);
                }

                _tail.store(tail, std::memory_order_release);

                return result;
            }
        }

        value_type* elem = reinterpret_cast<value_type*>(tail->_buffer + (consume_pos * sizeof(value_type)));
        if (callback(elem)) {
            elem->~value_type();
            tail->_consume_pos.store((consume_pos + 1) & chunk_mask, std::memory_order_release);
            return true;
        }

        return false;
    }

    // returns true if buffer is empty after this call
    template<typename callable>
    ptrdiff_t consume_all(callable callback) noexcept(noexcept(callback(static_cast<value_type*>(nullptr)))) {
        auto tail = _tail.load(std::memory_order_relaxed);
        auto head = _head.load(std::memory_order_acquire);

        scope_guard g([this, &tail] {
            this->_tail.store(tail, std::memory_order_release);
        });

        ptrdiff_t sum_consumed = 0;

        while (tail != head) {
            auto consume_pos = tail->_consume_pos.load(std::memory_order_relaxed);
            auto produce_pos = tail->_produce_pos.load(std::memory_order_acquire);

            scope_guard cg([tail, &consume_pos] {
                tail->_consume_pos.store(consume_pos, std::memory_order_release);
            });

            auto old_consume_pos = consume_pos;

            while (consume_pos != produce_pos) {
                void* addr = tail->_buffer + (consume_pos * sizeof(value_type));
                value_type* elem = reinterpret_cast<value_type*>(addr);

                if (callback(elem) == false) {
                    return sum_consumed + ((consume_pos - old_consume_pos) & chunk_mask);
                }

                elem->~value_type();

                consume_pos = (consume_pos + 1) & chunk_mask;
            }

            sum_consumed += (consume_pos - old_consume_pos) & chunk_mask;
            tail = tail->_next;
        }

        auto consume_pos = tail->_consume_pos.load(std::memory_order_relaxed);
        auto produce_pos = tail->_produce_pos.load(std::memory_order_acquire);

        if (produce_pos == consume_pos)
            return sum_consumed;

        scope_guard cg([&tail, &consume_pos] {
            tail->_consume_pos.store(consume_pos, std::memory_order_release);
        });

        auto old_consume_pos = consume_pos;

        while (consume_pos != produce_pos) {
            void* addr = tail->_buffer + (consume_pos * sizeof(value_type));
            value_type* elem = reinterpret_cast<value_type*>(addr);

            if (callback(elem) == false) {
                break;
            }

            elem->~value_type();

            consume_pos = (consume_pos + 1) & chunk_mask;
        }

        return sum_consumed + ((consume_pos - old_consume_pos) & chunk_mask);
    }

    bool is_empty() const {
        return _head.load(std::memory_order_acquire)->is_empty();
    }

private:
    alignas(align) std::array<chunk, chunk_count> _chunks{};
    alignas(align) std::atomic<chunk*> _head = nullptr;
    alignas(align) std::atomic<chunk*> _tail = nullptr;
};

template<typename _element_type, int _queue_size_log2, int _align_log2 = 7>
struct alignas((size_t)1 << _align_log2) spsc_queue_cached_ptr {
    using value_type = _element_type;
    static const auto size = size_t(1) << _queue_size_log2;
    static const auto mask = size - 1;
    static const auto align = size_t(1) << _align_log2;

    spsc_queue_cached_ptr() :
        _produce_pos((value_type*)_buffer),
        _consume_pos_cache((value_type*)_buffer),
        _consume_pos((value_type*)_buffer),
        _produce_pos_cache((value_type*)_buffer)
    {}

    spsc_queue_cached_ptr(const spsc_queue_cached_ptr& other) :
        _produce_pos((value_type*)_buffer + (other._produce_pos - (value_type*)other._buffer)),
        _consume_pos_cache((value_type*)_buffer + (other._consume_pos_cache - (value_type*)other._buffer)),
        _consume_pos((value_type*)_buffer + (other._consume_pos - (value_type*)other._buffer)),
        _produce_pos_cache((value_type*)_buffer + (other._produce_pos_cache - (value_type*)other._buffer))
    {
        auto dst = _consume_pos.load();
        auto end = _produce_pos.load();
        auto src = other._consume_pos.load();
        while (dst != end) {
            new(dst) value_type(*src);
            ++dst;
            ++src;
            if (dst == (value_type*)_buffer + size) {
                dst = (value_type*)_buffer;
                src = (value_type*)other._buffer;
            }
        }
    }

    spsc_queue_cached_ptr& operator=(const spsc_queue_cached_ptr& other) {
        if (this == &other) return *this;

        {
            auto cur = _consume_pos.load();
            auto end = _produce_pos.load();
            while (cur != end) {
                cur->~value_type();
                ++cur;
                if (cur == (value_type*)_buffer + size) {
                    cur = (value_type*)_buffer;
                }
            }
        }

        _produce_pos = (value_type*)_buffer + (other._produce_pos.load() - (value_type*)other._buffer);
        _consume_pos_cache = (value_type*)_buffer + (other._consume_pos_cache - (value_type*)other._buffer);
        _consume_pos = (value_type*)_buffer + (other._consume_pos.load() - (value_type*)other._buffer);
        _produce_pos_cache = (value_type*)_buffer + (other._produce_pos_cache - (value_type*)other._buffer);

        {
            auto dst = _consume_pos.load();
            auto end = _produce_pos.load();
            auto src = other._consume_pos.load();
            while (dst != end) {
                new(dst) value_type(*src);
                ++dst;
                ++src;
                if (dst == (value_type*)_buffer + size) {
                    dst = (value_type*)_buffer;
                    src = (value_type*)other._buffer;
                }
            }
        }

        return *this;
    }

    ~spsc_queue_cached_ptr() {
        auto cur = _consume_pos.load();
        auto end = _produce_pos.load();
        while (cur != end) {
            cur->~value_type();
            ++cur;
            if (cur == (value_type*)_buffer + size) {
                cur = (value_type*)_buffer;
            }
        }
    }

    template<typename... Args>
    bool produce(Args && ... args) noexcept(std::is_nothrow_constructible_v<value_type, Args...>) {
        static_assert(
            std::is_constructible_v<value_type, Args...>,
            "value_type must be constructible from Args..."
        );

        auto produce_pos = _produce_pos.load(std::memory_order_relaxed);
        auto next_index = (produce_pos + 1);
        if (next_index == (value_type*)_buffer + size) {
            next_index = (value_type*)_buffer;
        }

        auto consume_pos = _consume_pos_cache;
        if (next_index == consume_pos) {
            consume_pos = _consume_pos.load(std::memory_order_acquire);
            if (next_index == consume_pos) {
                return false;
            }
            _consume_pos_cache = consume_pos;
        }

        new(produce_pos) value_type(std::forward<Args>(args)...);

        _produce_pos.store(next_index, std::memory_order_release);
        return true;
    }

    template<typename callable>
    bool consume(callable && callback) noexcept(noexcept(callback(static_cast<value_type*>(nullptr)))) {
        auto consume_pos = _consume_pos.load(std::memory_order_relaxed);
        auto produce_pos = _produce_pos_cache;

        if (produce_pos == consume_pos) {
            produce_pos = _produce_pos_cache = _produce_pos.load(std::memory_order_acquire);
            if (produce_pos == consume_pos) {
                return false;
            }
        }

        if (callback(consume_pos)) {
            consume_pos->~value_type();

            auto next_index = (consume_pos + 1);
            if (next_index == (value_type*)_buffer + size) {
                next_index = (value_type*)_buffer;
            }

            _consume_pos.store(next_index, std::memory_order_release);

            return true;
        }

        return false;
    }

    // returns true if buffer is empty after this call
    template<typename callable>
    ptrdiff_t consume_all(callable && callback) noexcept(noexcept(callback(static_cast<value_type*>(nullptr)))) {
        auto consume_pos = _consume_pos.load(std::memory_order_acquire);
        auto produce_pos = _produce_pos_cache = _produce_pos.load(std::memory_order_acquire);

        if (produce_pos == consume_pos)
            return 0;

        scope_guard g([this, &consume_pos] {
            _consume_pos.store(consume_pos, std::memory_order_release);
        });

        auto old_consume_pos = consume_pos;

        while (consume_pos != produce_pos) {
            if (callback(consume_pos) == false) {
                break;
            }

            consume_pos->~value_type();

            consume_pos = (consume_pos + 1);
            if (consume_pos == (value_type*)_buffer + size) {
                consume_pos = (value_type*)_buffer;
            }
        }
        
        ptrdiff_t count = consume_pos - old_consume_pos;
        if (count < 0) count += ptrdiff_t(size);
        return count;
    }

    bool is_empty() const {
        auto consume_pos = _consume_pos.load(std::memory_order_acquire);
        auto produce_pos = _produce_pos.load(std::memory_order_acquire);

        return consume_pos == produce_pos;
    }

private:
    alignas(align) std::byte _buffer[size * sizeof(value_type)]{};

    alignas(align) std::atomic<value_type*> _produce_pos = nullptr;
    mutable value_type* _consume_pos_cache = nullptr;

    alignas(align) std::atomic<value_type*> _consume_pos = nullptr;
    mutable value_type* _produce_pos_cache = nullptr;
};

template<typename _element_type, int _queue_size, int _l1d_size_log2 = 15, int _align_log2 = 7>
struct alignas((size_t)1 << _align_log2) spsc_queue_chunked_ptr {
    using value_type = _element_type;
    static_assert(sizeof(value_type) <= (size_t(1) << _l1d_size_log2), "Elements must not be larger than L1D size.");

    static const auto size = _queue_size;
    static const auto align = size_t(1) << _align_log2;
    static const auto chunk_size = size_t(1) << (_l1d_size_log2 - ctu::log2_v<ctu::round_up_bits(sizeof(value_type), ctu::log2_v<sizeof(value_type)>)>);
    static const auto chunk_mask = chunk_size - 1;
    static const auto chunk_count = ((size + chunk_mask) / chunk_size);

    spsc_queue_chunked_ptr() : 
        _chunks{},
        _head(_chunks.data()),
        _tail(_chunks.data())
    {
        for (size_t i = 0; i < chunk_count - 1; i += 1) {
            _chunks[i]._next = &_chunks[i + 1];
        }
        _chunks[chunk_count - 1]._next = _chunks.data();
    }

    spsc_queue_chunked_ptr(const spsc_queue_chunked_ptr& other) :
        _chunks(other._chunks),
        _head(_chunks + (other._head - other._chunks)),
        _tail(_chunks + (other._tail - other._chunks))
    {
        for (size_t i = 0; i < chunk_count - 1; i += 1) {
            _chunks[i]._next = &_chunks[i + 1];
        }
        _chunks[chunk_count - 1]._next = _chunks.data();
    }

    spsc_queue_chunked_ptr& operator=(const spsc_queue_chunked_ptr& other) {
        _chunks = other._chunks;

        for (size_t i = 0; i < chunk_count - 1; i += 1) {
            _chunks[i]._next = &_chunks[i + 1];
        }
        _chunks[chunk_count - 1]._next = _chunks.data();

        _head = &_chunks[other._head - other._chunks];
        _tail = &_chunks[other._tail - other._chunks];

        return *this;
    }

    struct alignas(align) chunk {
        chunk() :
            _produce_pos((value_type*)_buffer),
            _consume_pos_cache((value_type*)_buffer),
            _consume_pos((value_type*)_buffer),
            _produce_pos_cache((value_type*)_buffer)
        {}

        chunk(const chunk& other) :
            _produce_pos((value_type*)_buffer + (other._produce_pos.load() - (value_type*)other._buffer)),
            _consume_pos_cache((value_type*)_buffer + (other._consume_pos_cache - (value_type*)other._buffer)),
            _consume_pos((value_type*)_buffer + (other._consume_pos.load() - (value_type*)other._buffer)),
            _produce_pos_cache((value_type*)_buffer + (other._produce_pos_cache - (value_type*)other._buffer))
        {
            auto dst = _consume_pos.load();
            auto end = _produce_pos.load();
            auto src = other._consume_pos.load();
            while (dst != end) {
                new(dst) value_type(*src);
                ++dst;
                ++src;
                if (dst == (value_type*)_buffer + size) {
                    dst = (value_type*)_buffer;
                    src = (value_type*)other._buffer;
                }
            }
        }

        chunk& operator=(const chunk& other) {
            if (this == &other) return *this;

            {
                auto cur = _consume_pos.load();
                auto end = _produce_pos.load();
                while (cur != end) {
                    cur->~value_type();
                    ++cur;
                    if (cur == (value_type*)_buffer + size) {
                        cur = (value_type*)_buffer;
                    }
                }
            }

            _produce_pos = (value_type*)_buffer + (other._produce_pos.load() - (value_type*)other._buffer);
            _consume_pos_cache = (value_type*)_buffer + (other._consume_pos_cache - (value_type*)other._buffer);
            _consume_pos = (value_type*)_buffer + (other._consume_pos.load() - (value_type*)other._buffer);
            _produce_pos_cache = (value_type*)_buffer + (other._produce_pos_cache - (value_type*)other._buffer);

            {
                auto dst = _consume_pos.load();
                auto end = _produce_pos.load();
                auto src = other._consume_pos.load();
                while (dst != end) {
                    new(dst) value_type(*src);
                    ++dst;
                    ++src;
                    if (dst == (value_type*)_buffer + size) {
                        dst = (value_type*)_buffer;
                        src = (value_type*)other._buffer;
                    }
                }
            }

            return *this;
        }

        ~chunk() {
            auto cur = _consume_pos.load();
            auto end = _produce_pos.load();
            while (cur != end) {
                cur->~value_type();
                ++cur;
                if (cur == (value_type*)_buffer + size) {
                    cur = (value_type*)_buffer;
                }
            }
        }

        bool is_empty() const {
            auto consume_pos = _consume_pos.load(std::memory_order_acquire);
            auto produce_pos = _produce_pos.load(std::memory_order_acquire);

            return consume_pos == produce_pos;
        }

        std::byte _buffer[chunk_size * sizeof(value_type)];

        alignas(align) std::atomic<value_type*> _produce_pos = nullptr;
        mutable value_type* _consume_pos_cache = nullptr;

        alignas(align) std::atomic<value_type*> _consume_pos = nullptr;
        mutable value_type* _produce_pos_cache = nullptr;

        alignas(align) chunk* _next = nullptr;
    };

    template<typename... Args>
    bool produce(Args&&... args) noexcept(std::is_nothrow_constructible_v<value_type, Args...>) {
        static_assert(
            std::is_constructible_v<value_type, Args...>,
            "value_type must be constructible from Args..."
        );

        auto head = _head.load(std::memory_order_relaxed);

        auto produce_pos = head->_produce_pos.load(std::memory_order_relaxed);
        auto next_index = (produce_pos + 1);
        if (next_index == (value_type*)head->_buffer + chunk_size) {
            next_index = (value_type*)head->_buffer;
        }

        auto consume_pos = head->_consume_pos_cache;
        if (next_index == consume_pos) {
            consume_pos = head->_consume_pos_cache = head->_consume_pos.load(std::memory_order_acquire);
            if (next_index == consume_pos) {
                head = head->_next;

                auto tail = _tail.load(std::memory_order_acquire);
                if (head == tail) {
                    return false;
                }

                produce_pos = head->_produce_pos.load(std::memory_order_relaxed);
                next_index = (produce_pos + 1);
                if (next_index == (value_type*)head->_buffer + chunk_size) {
                    next_index = (value_type*)head->_buffer;
                }

                // this next line is exactly where it needs to be, unless you like deadlocks.
                head->_consume_pos_cache = head->_consume_pos.load(std::memory_order_acquire);

                new(produce_pos) value_type(std::forward<Args>(args)...);
                head->_produce_pos.store(next_index, std::memory_order_release);
                _head.store(head, std::memory_order_release);
                return true;
            }
        }

        new(produce_pos) value_type(std::forward<Args>(args)...);
        head->_produce_pos.store(next_index, std::memory_order_release);

        return true;
    }

    template<typename callable>
    bool consume(callable && callback) noexcept(noexcept(callback(static_cast<value_type*>(nullptr)))) {
        auto tail = _tail.load(std::memory_order_relaxed);

        auto consume_pos = tail->_consume_pos.load(std::memory_order_relaxed);
        auto produce_pos = tail->_produce_pos_cache;

        if (produce_pos == consume_pos) {
            // this next line is exactly where it needs to be, unless you like deadlocks.
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

                auto result = callback(consume_pos);
                if (result) {
                    consume_pos->~value_type();

                    consume_pos = (consume_pos + 1);
                    if (consume_pos == (value_type*)tail->_buffer + chunk_size) {
                        consume_pos = (value_type*)tail->_buffer;
                    }
                    tail->_consume_pos.store(consume_pos, std::memory_order_release);
                }

                _tail.store(tail, std::memory_order_release);

                return result;
            }
        }

        if (callback(consume_pos)) {
            consume_pos->~value_type();

            consume_pos = (consume_pos + 1);
            if (consume_pos == (value_type*)tail->_buffer + chunk_size) {
                consume_pos = (value_type*)tail->_buffer;
            }
            tail->_consume_pos.store(consume_pos, std::memory_order_release);
            return true;
        }

        return false;
    }

    // returns true if buffer is empty after this call
    template<typename callable>
    ptrdiff_t consume_all(callable callback) noexcept(noexcept(callback(static_cast<value_type*>(nullptr)))) {
        auto tail = _tail.load(std::memory_order_relaxed);
        auto head = _head.load(std::memory_order_acquire);

        scope_guard g([this, &tail] {
            this->_tail.store(tail, std::memory_order_release);
        });

        ptrdiff_t sum_consumed = 0;

        while (tail != head) {
            auto consume_pos = tail->_consume_pos.load(std::memory_order_relaxed);
            auto produce_pos = tail->_produce_pos.load(std::memory_order_acquire);

            scope_guard cg([tail, &consume_pos] {
                tail->_consume_pos.store(consume_pos, std::memory_order_release);
            });

            auto old_consume_pos = consume_pos;

            while (consume_pos != produce_pos) {
                if (callback(consume_pos) == false) {
                    return sum_consumed + ((consume_pos - old_consume_pos) & chunk_mask);
                }

                consume_pos->~value_type();

                consume_pos = (consume_pos + 1);
                if (consume_pos == (value_type*)tail->_buffer + chunk_size) {
                    consume_pos = (value_type*)tail->_buffer;
                }
            }

            sum_consumed += (consume_pos - old_consume_pos) & chunk_mask;
            tail = tail->_next;
        }

        auto consume_pos = tail->_consume_pos.load(std::memory_order_relaxed);
        auto produce_pos = tail->_produce_pos.load(std::memory_order_acquire);

        if (produce_pos == consume_pos)
            return sum_consumed;

        scope_guard cg([&tail, &consume_pos] {
            tail->_consume_pos.store(consume_pos, std::memory_order_release);
        });

        auto old_consume_pos = consume_pos;

        while (consume_pos != produce_pos) {
            if (callback(consume_pos) == false) {
                break;
            }

            consume_pos->~value_type();

            consume_pos = (consume_pos + 1);
            if (consume_pos == (value_type*)tail->_buffer + chunk_size) {
                consume_pos = (value_type*)tail->_buffer;
            }
        }

        return sum_consumed + ((consume_pos - old_consume_pos) & chunk_mask);
    }

    bool is_empty() const {
        return _head.load(std::memory_order_acquire)->is_empty();
    }

private:
    alignas(align) std::array<chunk, chunk_count> _chunks{};
    alignas(align) std::atomic<chunk*> _head = nullptr;
    alignas(align) std::atomic<chunk*> _tail = nullptr;
};

