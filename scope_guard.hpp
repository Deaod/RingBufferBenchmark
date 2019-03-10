#pragma once

template<typename functor>
struct scope_guard {
    scope_guard(functor&& f) :
        f(std::move(f))
    {}

    ~scope_guard() {
        f();
    }
private:
    functor f;
};
