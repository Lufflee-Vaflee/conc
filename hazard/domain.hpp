#pragma once

#include <atomic>
#include <new>
#include <thread>
#include <vector>

#include <allocator.hpp>

namespace conc {

using hazard_pointer_t = std::atomic<void*>;

struct alignas(std::hardware_destructive_interference_size) 
domain_hazard_node {
    domain_hazard_node* next = nullptr;

    std::atomic<std::thread::id> id = std::thread::id();
    std::atomic<void*> pointer;
};

class hazard_pointer_domain {
   public:
    hazard_pointer_domain() {
        
    }

    ~hazard_pointer_domain() {
        auto temp = m_head_acquire.load(std::memory_order_acquire);
        while(temp != nullptr) {
            auto next = temp->next;
            delete(temp);
            temp = next;
        }

        return;
    }

    hazard_pointer_t& acquire() noexcept {
        domain_hazard_node* temp;
        std::thread::id default_id = std::thread::id();

        temp->next = m_head_acquire.load(std::memory_order_acquire); 
        do {
            temp = temp->next;

            [[unlikely]]
            if(temp == nullptr) {
                return create_new_node();
            }

        } while(!temp->id.compare_exchange_strong(default_id, std::this_thread::get_id(), std::memory_order_release));

        //delete_hazards

        return temp->pointer;
    }

    void retire(void* data) {
        tl_retire.push_back(data);
    }


   private:
    hazard_pointer_t& create_new_node() {
        domain_hazard_node* amortized = new domain_hazard_node();
        amortized->id = std::this_thread::get_id();

        do {
            amortized->next = m_head_acquire.load(std::memory_order_acquire);
        } while(!m_head_acquire.compare_exchange_weak(amortized->next, amortized, std::memory_order_release));

        return amortized->pointer;
    }

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
        domain_hazard_node* current;
        current->next = m_head_acquire.load(std::memory_order_acquire);

        while(current != nullptr) {

            [[unlikely]]
            if(pointer == current->pointer.load(std::memory_order_relaxed)) {
                return true;
            }

            current = current->next;
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
    std::atomic<domain_hazard_node*> m_head_acquire = new domain_hazard_node();

    inline static thread_local
     auto tl_retire = construct_with_capacity<void*, cache_aligned_alloc<void*>>(512);
};

}

