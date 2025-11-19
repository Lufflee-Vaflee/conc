#pragma once

#include <atomic>
#include <new>
#include <type_traits>
#include <vector>
#include <array>
#include <cassert>

#include <allocator.hpp>

namespace conc {

template<typename T>
struct alignas(std::hardware_destructive_interference_size) 
domain_cell {
    std::atomic<T*> pointer;
};

struct default_placeholder {};

template<typename T, std::size_t max_objects = 128, typename placeholder = default_placeholder>
requires(std::is_nothrow_destructible_v<T>)
class hazard_domain {
   public:
    domain_cell<T>* capture_cell() noexcept {
        T* null;

        auto it = m_acquire_list.begin();
        for(;it != m_acquire_list.end(); ++it) {
            null = nullptr;

            if(it->pointer.compare_exchange_strong(
                null,
                SENTINEL
            )) {
                break;
            }
        }

        assert(it != m_acquire_list.end());

        if(tl_retire.size() > max_objects * 2) {
            delete_hazards();
        }

        return &(*it);
    }

    void retire(T* data) {
        /*thread_local*/ tl_retire.emplace_back(data);
    }

    void delete_hazards() noexcept {
        for(std::size_t i = 0; i < tl_retire.size(); ++i) {
            if(!scan_for_hazard(tl_retire[i])) {
                delete tl_retire[i];
                tl_retire[i] = tl_retire[tl_retire.size() - 1];
                tl_retire.pop_back();
                i--;
            }
        }
    }

    //for testing purposes
    void delete_all() {
        for(std::size_t i = 0; i < max_objects; ++i) {
            m_acquire_list[i].pointer.store(nullptr);
        }

        delete_hazards();
    }

   private:
    bool __attribute__((noinline)) scan_for_hazard(T* pointer) noexcept {
        for(auto it = m_acquire_list.begin(); it != m_acquire_list.end(); ++it) {
            [[unlikely]]
            if(pointer == it->pointer.load()) {
                return true;
            }
        }

        return false;
    }

   private:
    inline static
     std::array<domain_cell<T>, max_objects> m_acquire_list;

    inline static thread_local
     std::vector<T*> tl_retire;

    // placeholder value to a aligned storage to mark cell that is captured and yet to be used
    // could use reinterpreted cast to domain address, but made for compiler/standard grooming
    alignas(T) inline static 
     char sentinel_storage[sizeof(T)];

    inline static 
     T* const SENTINEL = reinterpret_cast<T*>(&sentinel_storage);
};

}

