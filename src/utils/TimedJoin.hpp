#pragma once

#include <thread>
#include <chrono>
#include <future>
#include <Geode/loader/Log.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

namespace paimon {

/// Join a thread within a timeout. On timeout, optionally signal cancellation,
/// detach, and return false. A detached thread may still access shared resources.
inline bool timedJoin(std::thread& t, std::chrono::milliseconds timeout = std::chrono::seconds(3), std::atomic<bool>* cancelFlag = nullptr) {
    if (!t.joinable()) return true;

    // During DLL_PROCESS_DETACH, thread primitives may fail; detach and let the OS clean up.
    try {
#ifdef _WIN32
    // Windows: wait on the native handle without spawning a helper.
        HANDLE handle = t.native_handle();
        DWORD ms = static_cast<DWORD>(timeout.count());
        DWORD result = WaitForSingleObject(handle, ms);
        if (result == WAIT_OBJECT_0) {
            t.join();
            return true;
        }
        geode::log::warn("[TimedJoin] Thread did not finish in {}ms (result={}), detaching", timeout.count(), result);
        if (cancelFlag) cancelFlag->store(true, std::memory_order_release);
        if (t.joinable()) t.detach();
        return false;
#else
    // Portable fallback: wait on a packaged_task future instead of joining directly.
        std::packaged_task<void()> pt([&t]() {
            if (t.joinable()) t.join();
        });
        auto future = pt.get_future();
        std::thread helper(std::move(pt));

        if (future.wait_for(timeout) == std::future_status::timeout) {
            geode::log::warn("[TimedJoin] Thread did not finish in {}ms, detaching", timeout.count());
            if (cancelFlag) cancelFlag->store(true, std::memory_order_release);
            if (t.joinable()) t.detach();
    // The helper may be stuck in join(); detach it to avoid blocking here.
            if (helper.joinable()) helper.detach();
            return false;
        }
        future.get();
        if (helper.joinable()) helper.join();
        return true;
#endif
    } catch (std::system_error const& e) {
    // Teardown may invalidate thread primitives; detach and continue.
        geode::log::warn("[TimedJoin] system_error during join (process teardown?): {}, detaching", e.what());
        if (cancelFlag) cancelFlag->store(true, std::memory_order_release);
        if (t.joinable()) t.detach();
        return false;
    }
}

/// Wait on a future with a timeout; false discards the future while the task continues.
template <typename T>
bool timedWait(std::future<T>& f, std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
    if (!f.valid()) return true;
    auto status = f.wait_for(timeout);
    if (status == std::future_status::timeout) {
        geode::log::warn("[TimedWait] Future did not resolve in {}ms, abandoning", timeout.count());
        std::future<T> abandoned;
        std::swap(f, abandoned);
        return false;
    }
    (void)f.get();
    return true;
}

}
