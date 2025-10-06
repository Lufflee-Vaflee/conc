#pragma once

#include <cstdio>
#include <new>
#include <type_traits>
#include <algorithm>

namespace conc {

template <class T> class cache_aligned_alloc {
    static inline constexpr std::size_t CACHELINE_SIZE = std::hardware_destructive_interference_size;
    static inline constexpr auto ALIGN = static_cast<std::align_val_t>(
        std::max(alignof(T), CACHELINE_SIZE)
    );

   public:
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using const_pointer = const T*;

    template <class U> 
    struct rebind {
        using other = cache_aligned_alloc<U>;
    };

   public:
    // Propagation traits for stateless allocator
    using propagate_on_container_copy_assignment = std::true_type;  // Stateless, so always propagate
    using propagate_on_container_move_assignment = std::true_type;  // Stateless, so always propagate
    using propagate_on_container_swap = std::true_type;             // Stateless, so always propagate
    using is_always_equal = std::true_type;                        // All instances are equal (stateless)

    // Constructors
    cache_aligned_alloc() noexcept = default;
    cache_aligned_alloc(const cache_aligned_alloc&) noexcept = default;
    cache_aligned_alloc& operator=(const cache_aligned_alloc&) noexcept = default;
    
    template <class U> 
    cache_aligned_alloc(const cache_aligned_alloc<U>&) noexcept {}

    // For stateless allocators, just return a default-constructed instance
    cache_aligned_alloc select_on_container_copy_construction() const noexcept {
        return cache_aligned_alloc{};
    }

    [[nodiscard]]
    T* allocate(size_type n) {
        return static_cast<T*>(::operator new(n * sizeof(T), ALIGN));
    }

    void deallocate(T* p, size_type n) noexcept {
        ::operator delete(p, ALIGN);
    }
};

template <class T>
constexpr bool operator==(const cache_aligned_alloc<T> &, const cache_aligned_alloc<T> &) noexcept {
    return true;
}

template <class T>
constexpr bool operator!=(const cache_aligned_alloc<T> &, const cache_aligned_alloc<T> &) noexcept {
    return false;
}

} // namespace conc

