#pragma once

#include <atomic>
#include <new>
#include <thread>

#include <allocator.hpp>

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


struct retire_node {
    retire_node* next = nullptr;

    void* data;
};

template<std::size_t size>
requires(size == 0)
class hazard_pointer_domain<size> {
   public:
    hazard_pointer_domain(std::size_t reserve = s_default_memory_amortization) {
        
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
        retire_node* temp = new retire_node();
        temp->data = data;

        temp->next = tl_head_retire.load(std::memory_order_acquire);
        while(!tl_head_retire.compare_exchange_weak(temp->next, temp, std::memory_order_release));
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
        retire_node* current = tl_head_retire.exchange(nullptr, std::memory_order_relaxed);

        while(current != nullptr) {

            [[likely]]
            if(!scan_for_hazard(current)) {
                delete current->data;
            } else {
                retire(current->data);
            }

            current = current->next;
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
    inline static const unsigned int s_default_memory_amortization = std::thread::hardware_concurrency() * 2;

    std::atomic<domain_hazard_node*> m_head_acquire = new domain_hazard_node();

    inline static thread_local std::atomic<retire_node*> tl_head_retire = nullptr;

};

}

