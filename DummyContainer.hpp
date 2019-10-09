#pragma once

#include <immintrin.h>
#include <cstdint>

template<size_t n>
struct DummyContainer {
    char dummy[n]{};
};

template<>
struct DummyContainer<64> {
    char dummy[64];
    DummyContainer() {
        _mm256_storeu_ps((float*)dummy, _mm256_setzero_ps());
        _mm256_storeu_ps((float*)(dummy + 32), _mm256_setzero_ps());
    }
};

template<>
struct DummyContainer<56> {
    char dummy[56];
    DummyContainer() {
        __m256 a = _mm256_setzero_ps();
        _mm256_storeu_ps((float*)dummy, a);
        _mm256_storeu_ps((float*)(dummy + 24), a);
    }
};

template<>
struct DummyContainer<48> {
    char dummy[48];
    DummyContainer() {
        __m256 a = _mm256_setzero_ps();
        _mm256_storeu_ps((float*)dummy, a);
        _mm_storeu_ps((float*)(dummy + 32), _mm256_castps256_ps128(a));
    }
};

template<>
struct DummyContainer<40> {
    char dummy[40];
    DummyContainer() {
        _mm256_storeu_ps((float*)dummy, _mm256_setzero_ps());
        *((uint64_t*)(dummy + 32)) = 0;
    }
};

template<>
struct DummyContainer<32> {
    char dummy[32];
    DummyContainer() {
        _mm256_storeu_ps((float*)dummy, _mm256_setzero_ps());
    }
};

template<>
struct DummyContainer<24> {
    char dummy[24];
    DummyContainer() {
        _mm_storeu_ps((float*)dummy, _mm_setzero_ps());
        *((uint64_t*)(dummy + 16)) = 0;
    }
};

template<>
struct DummyContainer<16> {
    char dummy[16];
    DummyContainer() {
        _mm_storeu_ps((float*)dummy, _mm_setzero_ps());
    }
};

