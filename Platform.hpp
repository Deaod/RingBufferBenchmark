#pragma once

#include <cstdint>

#if defined(_WIN32)
#include <Windows.h>
#define PREPARE_THREAD(affinity) do {                                  \
    SetThreadAffinityMask(GetCurrentThread(), affinity);               \
    SetThreadPriority(GetCurrentThread(), THREAD_BASE_PRIORITY_LOWRT); \
} while(0)

#define PREPARE_PROCESS() do {                                      \
    SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS); \
} while(0)

#else
#define PREPARE_THREAD(affinity) do { } while(0)
#define PREPARE_PROCESS() do { } while(0)
#endif

constexpr uint64_t Thread1Affinity = 1 << 0;
constexpr uint64_t Thread2Affinity = 1 << 2;
