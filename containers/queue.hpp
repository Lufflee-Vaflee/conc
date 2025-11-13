#pragma once

#include "domain.hpp"
#include <atomic>
#include <optional>
#include <hazard_pointer.hpp>

namespace conc {

//template<typename T = int>
class queue {
   private:
    struct node {
        int* element = nullptr;
        std::atomic<node*> prev = nullptr;
    };

   public:
    queue() {
        m_tail.store(SENTINEL, std::memory_order_relaxed);
        m_head.store(SENTINEL, std::memory_order_relaxed);
    }

    void push(int&& element) {
        auto to_push = new node {       //should be smarter, i guess
            new int(std::move(element)),
            nullptr
        };

        auto head = m_head.load(std::memory_order_acquire);
        do {
            to_push->prev.store(head, std::memory_order_relaxed);
        } while(!m_head.compare_exchange_weak(head, to_push, std::memory_order_release));

        if(head == SENTINEL) {
            m_tail.compare_exchange_strong(head, to_push);
        }

        return;
    }

    std::optional<int> pop() {
        auto tail = m_tail.load();

        do {
            if(tail == SENTINEL) {
                return std::nullopt;
            }
        } while(m_tail.compare)

        

    }

   private:
    std::atomic<node*> m_tail;
    std::atomic<node*> m_head;

   private:
    node *const SENTINEL = new node;
};

}

