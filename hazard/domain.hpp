#pragma once

#include <atomic>
#include <new>
#include <thread>

namespace conc {

struct domain_stats {
    std::atomic<size_t> acquire_calls{0};
    std::atomic<size_t> nodes_created{0};
    std::atomic<size_t> retire_enqueued{0};
    std::atomic<size_t> scans_run{0};
    std::atomic<size_t> retire_list_popped{0};
    std::atomic<size_t> scan_nodes_visited{0};
    std::atomic<size_t> hazard_checks{0};
    std::atomic<size_t> reclaimed{0};
    std::atomic<size_t> requeued{0};
};

inline domain_stats& stats() { static domain_stats s; return s; }

struct domain_stats_snapshot {
    size_t acquire_calls;
    size_t nodes_created;
    size_t retire_enqueued;
    size_t scans_run;
    size_t retire_list_popped;
    size_t scan_nodes_visited;
    size_t hazard_checks;
    size_t reclaimed;
    size_t requeued;
};

inline domain_stats_snapshot get_domain_stats() {
    auto& s = stats();
    return {
        s.acquire_calls.load(std::memory_order_relaxed),
        s.nodes_created.load(std::memory_order_relaxed),
        s.retire_enqueued.load(std::memory_order_relaxed),
        s.scans_run.load(std::memory_order_relaxed),
        s.retire_list_popped.load(std::memory_order_relaxed),
        s.scan_nodes_visited.load(std::memory_order_relaxed),
        s.hazard_checks.load(std::memory_order_relaxed),
        s.reclaimed.load(std::memory_order_relaxed),
        s.requeued.load(std::memory_order_relaxed)
    };
}

inline void reset_domain_stats() {
    auto& s = stats();
    s.acquire_calls.store(0, std::memory_order_relaxed);
    s.nodes_created.store(0, std::memory_order_relaxed);
    s.retire_enqueued.store(0, std::memory_order_relaxed);
    s.scans_run.store(0, std::memory_order_relaxed);
    s.retire_list_popped.store(0, std::memory_order_relaxed);
    s.scan_nodes_visited.store(0, std::memory_order_relaxed);
    s.hazard_checks.store(0, std::memory_order_relaxed);
    s.reclaimed.store(0, std::memory_order_relaxed);
    s.requeued.store(0, std::memory_order_relaxed);
}

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
        stats().acquire_calls.fetch_add(1, std::memory_order_relaxed);

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

        //TODO: it should be smarter and it will be
        delete_hazards();

        return temp->pointer;
    }

    void retire(void* data) {
        stats().retire_enqueued.fetch_add(1, std::memory_order_relaxed);

        retire_node* temp = new retire_node();
        temp->data = data;

        temp->next = m_head_retire.load(std::memory_order_acquire);
        while(!m_head_retire.compare_exchange_weak(temp->next, temp, std::memory_order_release));
    }


   private:
    hazard_pointer_t& create_new_node() {
        stats().nodes_created.fetch_add(1, std::memory_order_relaxed);

        domain_hazard_node* amortized = new domain_hazard_node();
        amortized->id = std::this_thread::get_id();

        do {
            amortized->next = m_head_acquire.load(std::memory_order_acquire);
        } while(!m_head_acquire.compare_exchange_weak(amortized->next, amortized, std::memory_order_release));

        return amortized->pointer;
    }

    void delete_hazards() {
        stats().scans_run.fetch_add(1, std::memory_order_relaxed);

        retire_node* current = m_head_retire.exchange(nullptr, std::memory_order_relaxed);

        while(current != nullptr) {
            stats().retire_list_popped.fetch_add(1, std::memory_order_relaxed);

            [[likely]]
            if(!scan_for_hazard(current)) {
                stats().reclaimed.fetch_add(1, std::memory_order_relaxed);

                //TODO: decide on will whole domain be parametrized by one type, or we follow "deleter" pattern
                delete current->data;
            } else {
                stats().requeued.fetch_add(1, std::memory_order_relaxed);

                retire(current->data);
            }

            current = current->next;
        }
    }

    bool scan_for_hazard(void* pointer) {
        domain_hazard_node* current;
        current->next = m_head_acquire.load(std::memory_order_acquire);

        while(current != nullptr) {
            stats().scan_nodes_visited.fetch_add(1, std::memory_order_relaxed);
            stats().hazard_checks.fetch_add(1, std::memory_order_relaxed);

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
    alignas(std::hardware_destructive_interference_size) std::atomic<retire_node*> m_head_retire = nullptr;

};

}

