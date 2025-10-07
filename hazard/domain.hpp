#pragma once

#include <atomic>
#include <new>
#include <thread>
#include <vector>
#include <array>

#include <allocator.hpp>

namespace conc {

using hazard_pointer_t = std::atomic<void*>;

struct alignas(std::hardware_destructive_interference_size) 
domain_hazard_cell {
    std::atomic<std::thread::id> id = std::thread::id();
    std::atomic<void*> pointer;
};

class hazard_pointer_domain {
   public:

    ~hazard_pointer_domain() {
        
        return;
    }

    hazard_pointer_t& acquire() noexcept {
        std::thread::id default_id;

        auto it = m_acquire_list.begin();
        for(;it != m_acquire_list.end(); ++it) {
            default_id = std::thread::id();

            if(it->id.compare_exchange_strong(
                default_id,
                std::this_thread::get_id(),
                std::memory_order_acq_rel,
                std::memory_order_relaxed
            )) {
                break;
            }
        }

        if(tl_retire.size() > 128 * 2) {
            delete_hazards();
        }

        return it->pointer;
    }

    void retire(void* data) {
        tl_retire.push_back(data);
    }

   private:
    void delete_hazards() {
        for(std::size_t i = 0; i < tl_retire.size(); ++i) {
            if(!scan_for_hazard(tl_retire[i])) {
                delete tl_retire[i];
                tl_retire[i] = tl_retire[tl_retire.size() - 1];
                tl_retire.pop_back();
                i--;
            }
        }
    }

    bool scan_for_hazard(void* pointer) {
        auto it = m_acquire_list.begin();
        for(auto it = m_acquire_list.begin();it != m_acquire_list.end(); ++it) {
            [[unlikely]]
            if(pointer == it->pointer.load(std::memory_order_relaxed)) {
                return true;
            }
        }

        return false;
    }

   private:
    template<typename T, typename alloc, typename... args>
    static std::vector<T, alloc> construct_with_capacity(std::size_t capacity, args&&... arg) {
        std::vector<T, alloc> vec(std::forward<args>(arg)...);
        vec.reserve(capacity);
        return vec;
    }

   private:
    inline static
     std::array<domain_hazard_cell, 128> m_acquire_list;

    inline static thread_local
     auto tl_retire = construct_with_capacity<void*, cache_aligned_alloc<void*>>(512);
};

}

