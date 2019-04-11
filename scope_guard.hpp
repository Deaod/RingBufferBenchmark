#pragma once
#include <type_traits>

template<typename functor>
struct scope_guard {
    scope_guard(functor&& f) :
        f(std::forward<functor>(f))
    {}

    ~scope_guard() {
        this->f();
    }
private:
    functor f;
};
