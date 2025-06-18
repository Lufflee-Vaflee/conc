#pragma once

#include <atomic>
#include <thread>

namespace conc {

struct hazard_pointer;



using domain_limit = std::size_t;

template<domain_limit size = 0>
class hazard_pointer_domain;



struct alignas(std::hardware_destructive_interference_size) 
domain_hazard_node {
    std::atomic<std::thread::id> id = std::thread::id();
    std::atomic<domain_hazard_node*> next = nullptr;

    std::atomic<void*> pointer;
};

template<std::size_t size>
requires(size == 0)
class hazard_pointer_domain<size> {
    domain_hazard_node* start = new domain_hazard_node();
    ~hazard_pointer_domain() {
        delete start;
    }

    hazard_pointer& acquire() {
    }

    void retire();


};




template<std::size_t size>
requires(size > 0)
class hazard_pointer_domain<size> {

    hazard_pointer& acquire() {
        
    }

    void retire();

};



}
