#include "domain.hpp"
#include <atomic>
#include <cstddef>

namespace conc {

template<typename T>
using default_domain = hazard_domain<T>;

template <typename T, typename domain = default_domain<T>>
class hazard_pointer {
   private:
    hazard_pointer(domain_cell<T>* cell) {
        this->m_cell = cell;
    }

   public:
    static hazard_pointer<T, domain> make_hazard_pointer() noexcept {
        return hazard_pointer<T, domain>(s_domain.capture_cell());
    }

    static void retire(T* data) {
        s_domain.retire(data);
    }

   public:
    hazard_pointer() noexcept = default;

    hazard_pointer(hazard_pointer&& hp) noexcept {
        swap(hp);
    }

    hazard_pointer& operator=(hazard_pointer&& hp) noexcept {
        swap(hp);
        hp.reset_protection();
        return *this;
    }

    ~hazard_pointer() {
        if(m_cell != nullptr) {
            reset_protection();
        }
    }

   public:
    [[nodiscard]] 
    bool empty() const noexcept {
        return m_cell->pointer == nullptr;
    }

    T* protect(const std::atomic<T*>& src) noexcept {
        T* ptr = src.load(std::memory_order::relaxed);
        while (!try_protect(ptr, src)) {}
        return ptr;
    }

    bool try_protect(T*& ptr, const std::atomic<T*>& src) noexcept {
        assert(this->m_cell != nullptr);
        auto old = ptr;
        reset_protection(old);
        ptr = src.load(std::memory_order_acquire);
        auto result = (old == ptr);
        if(!result) {
            reset_protection();
        }

        return result;
    }

    void reset_protection(T* ptr) noexcept {
        assert(this->m_cell != nullptr);
        if(ptr == nullptr) {
            reset_protection();
            return;
        }

        m_cell->pointer.store(ptr, std::memory_order_release);
    }

    void reset_protection(std::nullptr_t t = nullptr) noexcept {
        assert(this->m_cell != nullptr);
        m_cell->pointer.store(t, std::memory_order_release);
    }


    template<typename T_, typename domain_>
    friend void swap(hazard_pointer<T_, domain_>& t1, hazard_pointer<T_, domain_>& t2) noexcept;

    void swap(hazard_pointer& t) noexcept {
        std::swap(m_cell, t.m_cell);
    }

   private:
    inline static domain s_domain = domain();
    domain_cell<T>* m_cell = nullptr;
};

template<typename T, typename domain>
void swap(hazard_pointer<T, domain>& t1, hazard_pointer<T, domain>& t2) noexcept {
    t1.swap(t2);
}

}

