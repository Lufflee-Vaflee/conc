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
        std::atomic<node*> next = nullptr;
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

        do {
            auto head = m_head.load();

            auto next = head->next.load();
            if(next != nullptr) {
                continue;
            }

            if(head->next.compare_exchange_strong(next, to_push)) {
                auto old = m_head.exchange(to_push);
                assert(head == old);
                return;
            }

        } while(true);
    }

    std::optional<int> pop() {
        auto tail = SENTINEL->next.load();
        if(tail == nullptr) {
            return {};
        }

        auto next_tail = tail->next.load();
        SENTINEL->next = 
    }

   private:
    std::atomic<node*> m_tail;
    std::atomic<node*> m_head;

   private:
    node *const SENTINEL = new node;
};

}

