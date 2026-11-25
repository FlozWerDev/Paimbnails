#pragma once

#include <thread>
#include <atomic>
#include <mutex>

namespace paimon {

// no stable geode api for this: captured at mod load, false before that.
inline std::thread::id& getMainThreadId() {
    // heap-allocated on purpose: no destructor runs at exit.
    static auto* id = new std::thread::id{};
    return *id;
}

inline std::once_flag& getMainThreadInitFlag() {
    static auto* flag = new std::once_flag{};
    return *flag;
}

// stores the caller as the main thread. call once from the main thread.
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
