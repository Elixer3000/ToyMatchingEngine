#pragma once

#include <iostream>
#include <thread>
#include <system_error>

#ifdef __linux__
#include <sys/mman.h>
#include <sched.h>
#include <pthread.h>
#include <unistd.h>
#elif defined(__APPLE__)
#include <sys/mman.h>
#include <pthread.h>
#include <mach/thread_policy.h>
#include <mach/thread_act.h>
#endif

namespace OSPlatform {

// 1. Thread Pinning to avoid migration jitter
inline void pinThreadToCore(int core_id) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_t current_thread = pthread_self();
    if (pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) != 0) {
        std::cerr << "Warning: Failed to pin thread to core " << core_id << "\n";
    } else {
        std::cout << "[HFT Linux] Pinned matching thread to core " << core_id << "\n";
    }
#elif defined(__APPLE__)
    thread_port_t mach_thread = pthread_mach_thread_np(pthread_self());
    thread_affinity_policy_data_t policy = { core_id };
    thread_policy_set(mach_thread, THREAD_AFFINITY_POLICY, (thread_policy_t)&policy, 1);
    std::cout << "[macOS Fallback] Set thread affinity to core " << core_id << "\n";
#else
    std::cout << "[Fallback] OS not supported for thread pinning.\n";
#endif
}

// 2. Real-Time Priority to avoid preemption
inline void setRealTimePriority() {
#ifdef __linux__
    pthread_t current_thread = pthread_self();
    struct sched_param params;
    params.sched_priority = sched_get_priority_max(SCHED_FIFO);
    if (pthread_setschedparam(current_thread, SCHED_FIFO, &params) != 0) {
        std::cerr << "Warning: Failed to set SCHED_FIFO priority (needs root)\n";
    } else {
        std::cout << "[HFT Linux] Set matching thread to SCHED_FIFO real-time priority\n";
    }
#elif defined(__APPLE__)
    std::cout << "[macOS Fallback] SCHED_FIFO not fully supported for user threads.\n";
#endif
}

// 3. Memory Locking to prevent Page Faults
inline void lockMemoryToRAM() {
#if defined(__linux__) || defined(__APPLE__)
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::cerr << "Warning: mlockall failed (often requires root). Continuing without memory lock.\n";
    } else {
        std::cout << "[HFT] mlockall active: all pages pinned to physical RAM.\n";
    }
#endif
}

} // namespace OSPlatform
