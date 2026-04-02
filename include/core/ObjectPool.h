#pragma once

#include <vector>
#include <cstddef>

namespace exchange {

// ============================================================
// OBJECT POOL — Pre-allocate objects to avoid heap allocation
// ============================================================
//
// THE PROBLEM:
// Every time you write "new Order()" or create a std::vector that
// grows, C++ asks the operating system for memory (malloc/new).
// This is SLOW — typically 50-200 nanoseconds per allocation.
// When you're processing millions of orders per second, those
// nanoseconds add up to seconds.
//
// THE SOLUTION:
// Allocate a big block of memory ONCE at startup, then hand out
// chunks of it when needed. When an object is "deleted", we don't
// actually free the memory — we just put it back in our free list.
// Next time someone needs an object, we give them the recycled one.
//
// ANALOGY:
// Imagine a restaurant. Without a pool: every customer gets a brand
// new plate from the factory (slow). With a pool: you wash and reuse
// plates (fast). The plates are pre-made and sitting on a shelf.
//
// WHY THIS MATTERS FOR YOUR RESUME:
// "Eliminated critical-path heap allocations via object pooling"
// tells an interviewer you understand memory allocation costs —
// something most college students never think about.
// ============================================================

template <typename T>
class ObjectPool {
public:
    // Reserve space for 'initial_capacity' objects upfront.
    // No memory allocation happens after this until we run out.
    explicit ObjectPool(size_t initial_capacity = 10000) {
        // Allocate the storage block — one big chunk of memory
        // capable of holding initial_capacity objects of type T.
        storage_.reserve(initial_capacity);

        // Pre-fill the free list with indices 0, 1, 2, ...
        // These are "slots" available for use.
        free_indices_.reserve(initial_capacity);
        for (size_t i = 0; i < initial_capacity; ++i) {
            storage_.emplace_back();             // construct default T
            free_indices_.push_back(i);          // mark slot as available
        }
    }

    // Acquire an object from the pool.
    // Returns a pointer to a recycled (or new) object.
    // The caller can then fill in the object's fields.
    //
    // This is O(1) — just pops from the free list.
    // Compare to "new T()" which is O(???) — depends on the allocator,
    // may need to search free lists, may trigger a system call.
    T* acquire() {
        if (free_indices_.empty()) {
            // Pool exhausted — grow it. This DOES allocate,
            // but it happens rarely if you sized the pool well.
            size_t idx = storage_.size();
            storage_.emplace_back();
            return &storage_[idx];
        }

        size_t idx = free_indices_.back();  // grab last free slot
        free_indices_.pop_back();           // remove from free list
        return &storage_[idx];              // return pointer to that slot
    }

    // Return an object to the pool for reuse.
    // The object isn't destroyed — its memory is just marked as available.
    // The caller should NOT use the pointer after calling release().
    //
    // IMPORTANT: We calculate the index from the pointer using pointer
    // arithmetic. This only works because storage_ is contiguous in memory
    // (std::vector guarantee). ptr - &storage_[0] gives the index.
    void release(T* ptr) {
        size_t idx = static_cast<size_t>(ptr - &storage_[0]);
        free_indices_.push_back(idx);
    }

    // How many objects are currently in use?
    size_t active_count() const {
        return storage_.size() - free_indices_.size();
    }

    // How many objects total does the pool hold?
    size_t capacity() const {
        return storage_.size();
    }

private:
    std::vector<T> storage_;           // The actual objects (contiguous memory)
    std::vector<size_t> free_indices_; // Which slots are available
};

} // namespace exchange
