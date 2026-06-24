#pragma once

#include <thread>
#include <atomic>
#include <mutex>

namespace paimon {

// Detects whether the current thread is the main thread. Geode has no stable API
// for this, so we capture the thread ID the first time captureMainThread() runs
// (call it from an early main-thread hook, e.g. MenuLayer::init). If it's never
// called, isMainThread() returns false (defensive).
inline std::thread::id& getMainThreadId() {
    // Heap-allocated to avoid a destructor at exit.
    static auto* id = new std::thread::id{};
    return *id;
}

inline std::once_flag& getMainThreadInitFlag() {
    static auto* flag = new std::once_flag{};
    return *flag;
}

// Capture the current thread ID as "main". Call ONLY from the main thread. Idempotent.
inline void captureMainThread() {
    std::call_once(getMainThreadInitFlag(), []() {
        getMainThreadId() = std::this_thread::get_id();
    });
}

// True if running on the thread captured via captureMainThread(); false if never captured.
inline bool isMainThread() {
    auto& id = getMainThreadId();
    if (id == std::thread::id{}) return false; // not initialized
    return std::this_thread::get_id() == id;
}

} // namespace paimon
