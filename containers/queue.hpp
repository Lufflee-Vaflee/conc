#pragma once

#include "domain.hpp"
#include <atomic>
#include <optional>
#include <hazard_pointer.hpp>
#include <chrono>
#include <thread>

namespace conc {

template<typename T>
requires(std::is_move_constructible_v<T> && std::is_nothrow_destructible_v<T>)
class queue {
   private:
    struct node {
        std::optional<T> element;
        std::atomic<node*> next;
    };

   public:
    using hazard_domain = conc::hazard_domain<node, 32, node>;

   private:
    using hazard_pointer_t = hazard_pointer<node, hazard_domain>;
    using guard_t = hazard_pointer_t::guard;

   public:
    queue(queue const&) = delete;
    queue(queue&& other) = delete;
    queue& operator=(queue const&) = delete;
    queue& operator=(queue &&) = delete;

    queue() {
        node* SENTINEL = new node;
        m_tail.store(SENTINEL, std::memory_order_relaxed);
        m_head.store(SENTINEL, std::memory_order_relaxed);
    }

    //not thread-safe
    ~queue() {
        node* curr = m_head.load(std::memory_order_relaxed);
        while(curr != nullptr) {
            auto next = curr->next.load(std::memory_order_relaxed);
            delete curr;
            curr = next;
        }
    }

    void enqueue(T&& element) {
        auto new_node = new node {
            T(std::move(element)),
            nullptr
        };

        node* curr_tail;
        auto hp = hazard_pointer_t::make_hazard_pointer();
        while(true) {
            curr_tail = hp.protect(m_tail);

            auto next = curr_tail->next.load();
            if(next != nullptr) {
                m_tail.compare_exchange_weak(curr_tail, next);
                continue;
            }

            if(curr_tail->next.compare_exchange_weak(next, new_node)) {
                break;
            }
        }

        m_tail.compare_exchange_strong(curr_tail, new_node);
        return;
    }

    std::optional<T> dequeue() noexcept(std::is_nothrow_move_constructible_v<T>) {
        auto hp_head = hazard_pointer_t::make_hazard_pointer();
        auto hp_next = hazard_pointer_t::make_hazard_pointer();

        while(true) {
            auto curr_head = hp_head.protect(m_head);
            auto next = hp_next.protect(curr_head->next);

            if(next == nullptr) {
                return std::nullopt;
            }

            if(m_head.compare_exchange_weak(curr_head, next)) {
                auto result = std::move(next->element);
                hazard_pointer_t::retire(curr_head);
                return result;
            }
        }
    }

   private:
    std::atomic<node*> m_tail;
    std::atomic<node*> m_head;
};

}

