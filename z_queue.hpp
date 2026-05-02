#pragma once
#include <bit>
#include <new>
#include <type_traits>
#include "z.hpp"

template <typename T>
requires std::is_trivially_copyable_v<T>
struct z_Queue {
private:
    T *_array = nullptr;
    size_t _head = 0;
    size_t _tail = 0;
    size_t _count = 0;
    const size_t _capacity;
    using DestroyFn = void (*)(T obj) noexcept;
    DestroyFn _destroy_fn;

public:
    z_Queue(size_t capacity = 64, DestroyFn destroy_fn = nullptr) noexcept :
        _capacity{std::bit_ceil(capacity)},
        _destroy_fn{destroy_fn}
        {}

    ~z_Queue() noexcept {
        clear(true);
    }

    // copy,move ctor
    z_Queue(z_Queue &&other) = delete;
    z_Queue(const z_Queue &) = delete;

    // copy,move assign
    z_Queue &operator=(z_Queue &&) = delete;
    z_Queue &operator=(const z_Queue &) = delete;

    // @return ok
    bool push(T obj) noexcept {
        if (!_array) {
            _array = new (std::nothrow) T[_capacity];
            if (!_array) [[unlikely]] return false;
        }
        if (_count >= _capacity) return false;
        _array[_tail] = obj;
        _tail = (_tail + 1) & (_capacity - 1); 
        _count++;
        return true;
    }

    // @return ok
    bool pop(T *obj) noexcept {
        if (_count == 0) return false;
        *obj = _array[_head];
        _head = (_head + 1) & (_capacity - 1);
        _count--;
        return true;
    }

    // peek first obj (or nullptr if empty)
    T *peek() const noexcept {
        return (_count > 0) ? &_array[_head] : nullptr;
    }

    size_t count() const noexcept {
        return _count;
    }

    size_t capacity() const noexcept {
        return _capacity;
    }

    bool is_empty() const noexcept {
        return _count == 0;
    }

    bool is_full() const noexcept {
        return _count >= _capacity;
    }

    void clear(bool free_mem = false) noexcept {
        if (_destroy_fn) {
            for (size_t i = 0; i < _count; ++i) {
                size_t pos = (_head + i) & (_capacity - 1);
                _destroy_fn(_array[pos]);
            }
        }
        _head = _tail;
        _count = 0;
        if (free_mem && _array) {
            delete[] _array;
            _array = nullptr;
        }
    }

    void set_destroy_fn(DestroyFn destroy_fn) noexcept {
        _destroy_fn = destroy_fn;
    }
};
