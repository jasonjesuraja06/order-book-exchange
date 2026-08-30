#pragma once

#include <cstddef>
#include <deque>
#include <vector>

namespace exchange {

// ============================================================
// OBJECT POOL: pre-allocated storage for hot-path objects
// ============================================================
//
// The matching path creates and destroys an Order for every message.
// Going to the allocator for each one costs tens to hundreds of
// nanoseconds and, worse, is variable: an allocation that happens to
// trigger an arena refill shows up in the latency tail. The pool
// allocates the objects up front and recycles them through a free
// list, so acquire() and release() are both O(1) with no allocator
// call in the steady state.
//
// STORAGE STABILITY:
// The backing store is a std::deque, not a std::vector, and the free
// list holds pointers rather than indices. Both choices exist for the
// same reason: the book hands out raw Order* that live in its price
// levels and its order-ID index, so a pointer must stay valid for as
// long as the order rests. std::deque guarantees that references to
// existing elements survive insertion at either end, so growing the
// pool past its initial capacity does not invalidate orders already
// on the book. A std::vector would reallocate on growth and leave
// every resting order dangling.
// ============================================================

template <typename T>
class ObjectPool {
public:
    // Construct 'initial_capacity' objects up front. Steady-state
    // traffic below that watermark performs no allocation at all.
    explicit ObjectPool(size_t initial_capacity = 10000) {
        free_list_.reserve(initial_capacity);
        for (size_t i = 0; i < initial_capacity; ++i) {
            storage_.emplace_back();
            free_list_.push_back(&storage_.back());
        }
    }

    // Non-copyable: the free list holds pointers into storage_.
    ObjectPool(const ObjectPool&)            = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    // Take an object from the free list, growing the pool if it is empty.
    // Growth appends to the deque, which leaves existing objects in place.
    T* acquire() {
        if (free_list_.empty()) {
            storage_.emplace_back();
            return &storage_.back();
        }

        T* obj = free_list_.back();
        free_list_.pop_back();
        return obj;
    }

    // Return an object for reuse. The object is not destroyed; the caller
    // must not dereference the pointer afterwards.
    void release(T* ptr) {
        free_list_.push_back(ptr);
    }

    size_t active_count() const {
        return storage_.size() - free_list_.size();
    }

    size_t capacity() const {
        return storage_.size();
    }

private:
    std::deque<T>   storage_;    // stable addresses across growth
    std::vector<T*> free_list_;  // available slots
};

} // namespace exchange
