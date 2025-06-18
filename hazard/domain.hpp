#pragma once

#include <atomic>
#include <thread>

namespace conc {

using hazard_pointer_t = std::atomic<void*>;


using domain_limit = std::size_t;

template<domain_limit size = 0>
class hazard_pointer_domain;


struct alignas(std::hardware_destructive_interference_size) 
domain_hazard_node {
    domain_hazard_node* next = nullptr;

    std::atomic<std::thread::id> id = std::thread::id();
    std::atomic<void*> pointer;
};

template<std::size_t size>
requires(size == 0)
class hazard_pointer_domain<size> {
   public:
    hazard_pointer_domain(std::size_t reserve = 0) {
        
    }


    ~hazard_pointer_domain() {
        auto temp = m_head.load(std::memory_order_acquire);
        while(temp != nullptr) {
            auto next = temp->next;
            delete(temp);
            temp = next;
        }

        return;
    }

    hazard_pointer_t& acquire() noexcept {
        domain_hazard_node* temp = m_head.load(std::memory_order_acquire);
        std::thread::id default_id = std::thread::id();

        while(!temp->id.compare_exchange_strong(default_id, std::this_thread::get_id(), std::memory_order_release)) {
            [[unlikely]] if(temp == nullptr) {
                return create_new_node();
            }

            temp = temp->next;
        } 

        return temp->pointer;
    }

    void retire(void* data) {
        
    }

   private:
    hazard_pointer_t& create_new_node() {
        domain_hazard_node* amortized = new domain_hazard_node();
        amortized->id = std::this_thread::get_id();

        amortized->next = m_head.load(std::memory_order_acquire);
        while(m_head.compare_exchange_weak(amortized->next, amortized, std::memory_order_release));

        return amortized->pointer;
    }

    std::atomic<domain_hazard_node*> m_head = new domain_hazard_node();
};


}

