#pragma once

#include <thread>
#include <atomic>
#include <mutex>

namespace paimon {

// no stable Geode API: captured once at mod load; false until then.
inline std::thread::id& getMainThreadId() {
    // heap-allocated; no exit destructor.
    static auto* id = new std::thread::id{};
    return *id;
}

inline std::once_flag& getMainThreadInitFlag() {
    static auto* flag = new std::once_flag{};
    return *flag;
}

// capture the caller as main. main thread only; idempotent.
inline void captureMainThread() {
    std::call_once(getMainThreadInitFlag(), []() {
        getMainThreadId() = std::this_thread::get_id();
    });
}

inline bool isMainThread() {
    auto& id = getMainThreadId();
    if (id == std::thread::id{}) return false;
    return std::this_thread::get_id() == id;
}

} // namespace paimon
