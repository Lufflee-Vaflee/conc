#pragma once

#include <atomic>
#include <optional>

namespace conc {

template<typename T>
class stack {
   public:
    stack() = default;
    stack(stack const&) = delete;
    stack(stack &&) = delete;
    stack& operator=(stack const&) = delete;
    stack& operator=(stack &&) = delete;

   private:
    struct node {
        T element = nullptr;
        node* previous = nullptr;
    };

   public:
    void push(T&& element) {
        auto to_push = new node {
            T(std::move(element)),
            nullptr
        };

        node* acquire;
        do {
            acquire = m_head.load(std::memory_order_acquire);
            to_push->previous = acquire;
        } while(!m_head.compare_exchange_weak(acquire, to_push, std::memory_order_release));

        return;
    }

    std::optional<T> pop() {
        node* acquire;
        node* new_head;
        do {
            acquire = m_head.load(std::memory_order_acquire);

            [[unlikely]] if(acquire == nullptr) {
                return {};
            }

            new_head = acquire->previous;
        } while(!m_head.compare_exchange_weak(acquire, new_head, std::memory_order_release));

        T result = std::move(acquire->element);

        //pupupu
        delete acquire;

        return result;
    }

   private:
    std::atomic<node*> m_head = nullptr;
};

}

