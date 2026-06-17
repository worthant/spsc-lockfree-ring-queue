#pragma once

#include <atomic>
#include <cstddef>
#include <new>
namespace spsc {
    template <typename T, std::size_t CAPACITY>
    class RingQueue {
        static_assert(CAPACITY > 0, "CAPACITY must be greater than 0");

    private:
        // static, T-aligned raw byte storage
        alignas(T) std::byte m_buffer[CAPACITY * sizeof(T)];
        // 64 is hardcoded cache line size - to avoid "False Sharing"
        // (keep producer/consumer idx on separate cache lines)
        alignas(64) std::atomic<std::size_t> m_head{};
        alignas(64) std::atomic<std::size_t> m_tail{};

        // Addr of `idx % CAPACITY` slot as a raw T*
        // later will use a "placement new" addr
        const T* get_ptr(std::size_t idx) const noexcept {
            return reinterpret_cast<const T*>(
                &m_buffer[(idx % CAPACITY) * sizeof(T)]);
        }
        T* get_ptr(std::size_t idx) noexcept {
            return reinterpret_cast<T*>(
                &m_buffer[(idx % CAPACITY) * sizeof(T)]);
        }

        /* helpers */
        void destroy() noexcept {
            auto curr_head = m_head.load(std::memory_order_relaxed);
            auto curr_tail = m_tail.load(std::memory_order_relaxed);
            for (auto i = curr_head; i < curr_tail; ++i) {
                std::launder(get_ptr(i))->~T();
            }
        }

        // precondition: *this should be freed
        void copy_from(const RingQueue& other) {
            auto curr_head = other.m_head.load(std::memory_order_relaxed);
            auto curr_tail = other.m_tail.load(std::memory_order_relaxed);
            m_head.store(curr_head, std::memory_order_relaxed);
            m_tail.store(curr_tail, std::memory_order_relaxed);
            for (auto i = curr_head; i < curr_tail; ++i) {
                const T* src_ptr = other.get_ptr(i);
                void* dst_ptr    = static_cast<void*>(get_ptr(i));
                ::new (dst_ptr) T(*std::launder(src_ptr));
            }
        }

    public:
        using value_type = T;

        RingQueue() = default;
        ~RingQueue() { destroy(); }

        RingQueue(const RingQueue& other) { copy_from(other); }

        RingQueue& operator=(const RingQueue& other) {
            if (this == &other)
                return *this;
            destroy();
            copy_from(other);
            return *this;
        }

        static constexpr std::size_t capacity() noexcept { return CAPACITY; }

        bool push(const T& value) {
            auto curr_head = m_head.load(std::memory_order_acquire); // foreign
            auto curr_tail = m_tail.load(std::memory_order_relaxed); // our

            // should not be full
            if ((curr_tail - curr_head) == capacity())
                return false;

            // find raw addr to call T constructor to
            void* dst_ptr = static_cast<void*>(get_ptr(curr_tail));
            ::new (dst_ptr) T(value);

            m_tail.store(curr_tail + 1, std::memory_order_release);
            return true;
        }

        bool pop(T& out) {
            auto curr_head = m_head.load(std::memory_order_relaxed); // our
            auto curr_tail = m_tail.load(std::memory_order_acquire); // foreign
            // should not be empty
            if (curr_tail == curr_head)
                return false;

            // copy T obj to out
            T* ptr = std::launder(get_ptr(curr_head));
            out    = *ptr;
            // cleanup memory
            ptr->~T();

            m_head.store(curr_head + 1, std::memory_order_release);
            return true;
        }

        /* public API for informational purposes only */

        bool empty() const noexcept {
            auto tail = m_tail.load(std::memory_order_acquire);
            auto head = m_head.load(std::memory_order_acquire);
            return tail == head;
        }

        bool full() const noexcept {
            auto tail = m_tail.load(std::memory_order_acquire);
            auto head = m_head.load(std::memory_order_acquire);
            return (tail - head) == capacity();
        }

        std::size_t size() const noexcept {
            auto tail = m_tail.load(std::memory_order_acquire);
            auto head = m_head.load(std::memory_order_acquire);
            return tail - head;
        }
    };
} // namespace spsc
