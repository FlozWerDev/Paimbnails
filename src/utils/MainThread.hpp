#pragma once

#include <thread>
#include <atomic>
#include <mutex>

namespace paimon {

// No stable Geode API: the ID is captured once from $on_mod(Loaded) /
// LoadingLayer::init, so isMainThread() reads false until then.
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
    if (id == std::thread::id{}) return false;
    return std::this_thread::get_id() == id;
}

} // namespace paimon
