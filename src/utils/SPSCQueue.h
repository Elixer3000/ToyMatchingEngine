#pragma once

#include <atomic>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <memory>

template<typename T>
class alignas(64) SPSCQueue {
private:
    struct Node {
        T data;
    };

    const size_t capacity_;
    const size_t mask_;
    Node* buffer_;

    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};

public:
    explicit SPSCQueue(size_t capacity = 1024) 
        : capacity_(capacity), mask_(capacity - 1) {
        
        // Capacity must be a power of 2 for fast masking
        if ((capacity & (capacity - 1)) != 0) {
            throw std::invalid_argument("Capacity must be a power of 2");
        }
        
        buffer_ = new Node[capacity];
    }

    ~SPSCQueue() {
        delete[] buffer_;
    }

    // Called by Producer (Crow REST Threads)
    bool push(const T& item) {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) & mask_;

        if (next_tail == head_.load(std::memory_order_acquire)) {
            return false; // Queue is full
        }

        buffer_[current_tail].data = item;
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    // Called by Consumer (Matching Engine Thread)
    bool pop(T& item) {
        const size_t current_head = head_.load(std::memory_order_relaxed);

        if (current_head == tail_.load(std::memory_order_acquire)) {
            return false; // Queue is empty
        }

        item = buffer_[current_head].data;
        head_.store((current_head + 1) & mask_, std::memory_order_release);
        return true;
    }
};
