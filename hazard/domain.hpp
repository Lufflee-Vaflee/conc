#pragma once

#include <atomic>
#include <vector>
#include <array>
#include <cassert>
#include <utility>
#include <algorithm>

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
                SENTINEL,
                std::memory_order_acq_rel,
                std::memory_order_relaxed
            )) {

//                tl_block& tl_data = get_tl_data();
//                if(!tl_data.delete_turn && (tl_data.tl_retire.size() > tl_data.amortization_factor)) {
//                    delete_hazards();
//                }

                return &(*it);
            }
        }

        assert(it != m_acquire_list.end());
        std::unreachable();
    }

    void retire(T* data) {
        tl_block& tl_data = get_tl_data();
        tl_data.tl_retire.emplace_back(data);

        if(/*tl_data.delete_turn && */(tl_data.tl_retire.size() > tl_data.amortization_factor)) {
            delete_hazards();
        }
    }

    void delete_hazards() noexcept {
        tl_block& tl_data = get_tl_data();
        auto snapshot = acquire_snapshot();
        auto& retire_list = tl_data.tl_retire;

        for(std::size_t i = 0; i < retire_list.size(); ++i) {
            if(!scan_for_hazard(retire_list[i], snapshot)) {
                delete retire_list[i];
                retire_list[i] = retire_list[retire_list.size() - 1];
                retire_list.pop_back();
                i--;
            }
        }

        tl_data.delete_turn = !tl_data.delete_turn;
        tl_data.amortization_factor = std::min(tl_data.amortization_factor * 2, max_objects * 32);
    }

   private:
    std::array<T*, max_objects> acquire_snapshot() noexcept {
        std::array<T*, max_objects> result;
        for(std::size_t i = 0; i < max_objects; ++i) {
            result[i] = m_acquire_list[i].pointer.load(std::memory_order_acquire);
        }

        return result;
    }

    bool scan_for_hazard(T* pointer, std::array<T*, max_objects> const& snapshot) noexcept {
        for(auto& ptr : snapshot) {
            [[unlikely]]
            if(pointer == ptr) {
                return true;
            }
        }

        return false;
    }

   private:
    inline static
     std::array<domain_cell<T>, max_objects> m_acquire_list;

    struct tl_block {
         std::vector<T*> tl_retire;
         bool delete_turn = false;
         std::size_t amortization_factor = max_objects;
    };

    tl_block& get_tl_data() noexcept {
        thread_local tl_block data;
        return data;
    }


    // placeholder value to a aligned storage to mark cell that is captured and yet to be used
    // could use reinterpreted cast to domain address, but made for compiler/standard grooming
    alignas(T) inline static 
     char sentinel_storage[sizeof(T)];

   public:
    inline static 
     T* const SENTINEL = reinterpret_cast<T*>(&sentinel_storage);
};

}

