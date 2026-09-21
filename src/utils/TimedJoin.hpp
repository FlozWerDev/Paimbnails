#pragma once

#include <thread>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <Geode/loader/Log.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

namespace paimon {

/// Join a thread within a timeout. On timeout, optionally signal cancellation,
/// detach, and return false. A detached thread may still access shared resources.
inline bool timedJoin(std::thread& t, std::chrono::milliseconds timeout = std::chrono::seconds(3), std::atomic<bool>* cancelFlag = nullptr) {
    if (!t.joinable()) return true;

    // A std::thread cannot be joined and detached concurrently. The timeout is
    // still useful to signal cancellation, but ownership stays with this call.
    try {
#ifdef _WIN32
        HANDLE handle = t.native_handle();
        DWORD ms = static_cast<DWORD>(std::max<int64_t>(0, timeout.count()));
        DWORD result = WaitForSingleObject(handle, ms);
        if (result == WAIT_OBJECT_0) {
            t.join();
            return true;
        }
        geode::log::warn("[TimedJoin] Thread did not finish in {}ms (result={}), detaching", timeout.count(), result);
        if (cancelFlag) cancelFlag->store(true, std::memory_order_release);
        t.detach();
        return false;
#else
        (void)timeout;
        (void)cancelFlag;
        t.join();
        return true;
#endif
    } catch (std::system_error const& e) {
        geode::log::warn("[TimedJoin] system_error during join: {}", e.what());
        if (cancelFlag) cancelFlag->store(true, std::memory_order_release);
        return false;
    }
}

}
