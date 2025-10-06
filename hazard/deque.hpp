#pragma once

#include <new>
#include <atomic>
#include <cstddef>
#include <iterator>

namespace conc {


template<typename cell, std::size_t chunk_size>
requires(
    std::alignment_of_v<cell> == std::hardware_destructive_interference_size &&
    chunk_size >= 2
)
class deque {
   private:
    inline static constexpr std::size_t chunks = chunk_size - 1;
    struct chunk {
        cell arr[chunks];

       alignas(std::hardware_destructive_interference_size)
        std::atomic<chunk*> next = nullptr;
    };

   public:
    class amortize_forward {
       private:
        amortize_forward(chunk* ch, std::size_t cl) noexcept :
            m_current_chunk(ch),
            m_current_cell(cl) {}

       public:
        using difference_type = std::ptrdiff_t;
        using value_type = cell;

        amortize_forward(amortize_forward const& other) noexcept {
            m_current_chunk = other.m_current_chunk;
            m_current_cell = other.m_current_cell;
        }

        amortize_forward& operator=(amortize_forward const& other) noexcept {
            m_current_chunk = other.m_current_chunk;
            m_current_cell = other.m_current_cell;
        }

        cell& operator*() const noexcept {
            return m_current_chunk[m_current_cell];
        }

        amortize_forward& operator++() {
            m_current_cell++;
            if(m_current_cell != chunks) {
                return *this;
            }

            
            do {
                auto next = m_current_chunk->next.load(std::memory_order_acquire);

            } while()
        }

        amortize_forward operator++(int)
        {
            auto tmp = *this;
            ++*this;
            return tmp;
        }

        bool operator==(const amortize_forward&) const;

       private:
        chunk* m_current_chunk = nullptr;
        std::size_t m_current_cell = 0;
    };

    static_assert(std::forward_iterator<amortize_forward>);
};

}
