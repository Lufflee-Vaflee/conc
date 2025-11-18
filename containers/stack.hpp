#pragma once

#include "domain.hpp"
#include <atomic>
#include <optional>
#include <hazard_pointer.hpp>
#include <type_traits>

namespace conc {

template<typename T>
requires(std::is_move_constructible_v<T> && std::is_nothrow_destructible_v<T>)
class stack {
   private:
    struct node {
        T element;
        node* previous = nullptr;
    };

   public:
    using hazard_domain = conc::hazard_domain<node, 16, stack<T>>;

    stack() = default;
    stack(stack const&) = delete;
    stack(stack&& other) = delete;
    stack& operator=(stack const&) = delete;
    stack& operator=(stack &&) = delete;

    ~stack() {
        //preconditions: no threads perform concurrent acsess
        auto temp = m_head.load(std::memory_order_relaxed);
        while(temp != nullptr) {
            auto pr = temp->previous;
            delete temp;
            temp = pr;
        }
    }

   public:
    void push(T&& element) {
        auto to_push = new node {       //should be smarter, i guess
            T(std::move(element)),
            nullptr
        };

        to_push->previous = m_head.load(std::memory_order_acquire);
        while(!m_head.compare_exchange_weak(to_push->previous, to_push, std::memory_order_release));

        return;
    }

    std::optional<T> pop() noexcept(std::is_nothrow_move_constructible_v<T>) {
        using hazard_ptr_t = hazard_pointer<node, hazard_domain>;
        using guard_t = hazard_ptr_t::guard;
        hazard_ptr_t hp = hazard_ptr_t::make_hazard_pointer();

        node* acquire;
        do {
            acquire = hp.protect(m_head);

            [[unlikely]] 
            if(acquire == nullptr) {
                return std::nullopt;
            }

        } while(!m_head.compare_exchange_weak(acquire, acquire->previous, std::memory_order_release));

        T result = std::move(acquire->element);
        hazard_ptr_t::retire(acquire);

        return result;
    }

   private:
    std::atomic<node*> m_head = nullptr;
};

}

