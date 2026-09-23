#pragma once

#include <Geode/loader/Log.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace paimon {

// The worker must exit before its owner releases the state it uses.
inline void joinWithWarning(std::thread& t, std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
    if (!t.joinable()) return;

#ifdef _WIN32
    DWORD result = WaitForSingleObject(t.native_handle(), static_cast<DWORD>(std::max<int64_t>(0, timeout.count())));
    if (result != WAIT_OBJECT_0) {
        geode::log::warn("[ThreadJoin] Worker exceeded {}ms (result={}); waiting for exit", timeout.count(), result);
        t.join();
        return;
    }
#else
    auto start = std::chrono::steady_clock::now();
    t.join();
    if (std::chrono::steady_clock::now() - start > timeout) {
        geode::log::warn("[ThreadJoin] Worker exceeded {}ms before exiting", timeout.count());
    }
    return;
#endif
    t.join();
}

}
