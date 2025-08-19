#pragma once

#include <cstddef>
#include <limits>
#include <new>
#include <type_traits>

namespace hazard {

template <class T>
    struct allocator {
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using void_pointer = void*;
    using const_void_pointer = const void*;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;

    using propagate_on_container_copy_assignment = std::false_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;
    using is_always_equal = std::true_type;

    template <class U>
    struct rebind { using other = allocator<U>; };

    allocator() noexcept = default;

    template <class U>
    allocator(const allocator<U>&) noexcept {}

    [[nodiscard]] pointer allocate(size_type n) {
        if (n > max_size()) 
            throw std::bad_alloc();

        return static_cast<pointer>(::operator new(n * sizeof(T)));
    }

    void deallocate(pointer p, size_type) noexcept { 
        ::operator delete(p); 
    }

    constexpr size_type max_size() const noexcept {
        return std::numeric_limits<size_type>::max() / (sizeof(T) ? sizeof(T) : 1);
    }

    allocator select_on_container_copy_construction() const { 
        return *this;
    }
};

template <class T, class U>
constexpr bool operator==(const allocator<T>&, const allocator<U>&) noexcept { return true; }

template <class T, class U>
constexpr bool operator!=(const allocator<T>&, const allocator<U>&) noexcept { return false; }

} // namespace hazard
