#pragma once

#include <new>
#include <atomic>
#include <cstddef>
#include <iterator>

namespace conc {

// there are few requirements for cell also
template<typename cell, std::size_t chunk_size>
requires(
    std::alignment_of_v<cell> == std::hardware_destructive_interference_size &&
    chunk_size >= 2
)
class deque {
   private:
    inline static constexpr std::size_t cells_size = chunk_size - 1;
    struct chunk {
        cell arr[cells_size];

       alignas(std::hardware_destructive_interference_size)
        std::atomic<chunk*> next = nullptr;
    };
    chunk start_chunk;
    public: 
    class amortize_forward {
        friend deque;
       private:
        amortize_forward(chunk* ch, std::size_t cl) noexcept :
            m_current_chunk(ch),
            m_current_cell(cl) {}

       public:
        using difference_type = std::ptrdiff_t;
        using value_type = cell;

        amortize_forward() noexcept {
            m_current_cell = 0;
            m_current_chunk = nullptr;
        }

        amortize_forward(amortize_forward const& other) noexcept {
            m_current_chunk = other.m_current_chunk;
            m_current_cell = other.m_current_cell;
        }

        amortize_forward& operator=(amortize_forward const& other) noexcept {
            m_current_chunk = other.m_current_chunk;
            m_current_cell = other.m_current_cell;
            return *this;
        }

        cell& operator*() const noexcept {
            return m_current_chunk->arr[m_current_cell];
        }

        amortize_forward& operator++() {
            m_current_cell++;

            [[likely]]
            if(m_current_cell != cells_size) {
                return *this;
            }

            return fallback();
        }

        amortize_forward operator++(int)
        {
            auto tmp = *this;
            ++*this;
            return tmp;
        }

        bool operator==(amortize_forward const& other) const noexcept {
            return m_current_cell == other.m_current_cell && m_current_chunk == other.m_current_chunk;
        }

       private:
        amortize_forward& fallback() {
            // this is an fallback mechanism, we shouldn't hit that
            auto acquired = m_current_chunk->next.load(std::memory_order_acquire);

            [[likely]]
            if(acquired != nullptr) {
                m_current_cell = 0;
                m_current_chunk = acquired;
                return *this;
            }

            auto new_chunk = new chunk;
            chunk* desired = nullptr;
            bool swapped = m_current_chunk->next.compare_exchange_strong(
                desired,
                new_chunk,
                std::memory_order_release
            );

            assert(desired != nullptr);
            if(!swapped) {
                delete new_chunk;
            }

            m_current_chunk = new_chunk;
            m_current_cell = 0;
            return *this;
        }

        chunk* m_current_chunk = nullptr;
        std::size_t m_current_cell = 0;
    };

   public:
    amortize_forward begin() {
        return amortize_forward(&start_chunk, 0);
    }

    ~deque() {
        auto temp = start_chunk.next.load();
        while(temp != nullptr) {
            auto next = temp->next.load();
            delete temp;
            temp = next;
        }
    }

    static_assert(std::forward_iterator<amortize_forward>);
};

}

