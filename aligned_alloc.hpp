#pragma once
#include <cstddef>
#include <cstdlib>

#if _MSC_VER < 2000
#include <malloc.h>

inline void* aligned_alloc(std::size_t alignment, std::size_t size) {
    return _aligned_malloc(size, alignment);
}

inline void aligned_free(void* ptr) {
    _aligned_free(ptr);
}

#elif __STDC_VERSION__ >= 201112L

inline void aligned_free(void* ptr) {
    free(ptr);
}

#else

inline void* aligned_alloc(std::size_t alignment, std::size_t size) {
    return nullptr;
}

inline void aligned_free(void* ptr) {
    
}

#endif

// For use with std::unique_ptr as deleter
struct aligned_free_deleter {
    void operator()(void* ptr) {
        aligned_free(ptr);
    }
};

