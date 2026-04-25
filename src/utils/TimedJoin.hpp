#pragma once

#include <thread>
#include <chrono>
#include <future>
#include <Geode/loader/Log.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

namespace paimon {

/// Attempt to join a thread within a timeout. If the thread doesn't finish
/// in time, it is detached instead so the caller never blocks indefinitely.
/// Returns true if the thread was joined, false if it was detached.
inline bool timedJoin(std::thread& t, std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
    if (!t.joinable()) return true;

    // During DLL_PROCESS_DETACH (game exit), thread creation and
    // synchronization primitives may throw std::system_error because
    // the process is tearing down. Wrap the whole thing so we never
    // crash on exit — worst case we detach and let the OS clean up.
    try {
#ifdef _WIN32
        // Windows: use WaitForSingleObject on the native thread handle
        // for a true timeout without spawning extra threads.
        HANDLE handle = t.native_handle();
        DWORD ms = static_cast<DWORD>(timeout.count());
        DWORD result = WaitForSingleObject(handle, ms);
        if (result == WAIT_OBJECT_0) {
            t.join();
            return true;
        }
        geode::log::warn("[TimedJoin] Thread did not finish in {}ms (result={}), detaching", timeout.count(), result);
        if (t.joinable()) t.detach();
        return false;
#else
        // Portable fallback: packaged_task in a helper thread.
        // The main thread waits on the future with wait_for() — never
        // blocks on join() directly, so a stuck thread can't freeze us.
        std::packaged_task<void()> pt([&t]() {
            if (t.joinable()) t.join();
        });
        auto future = pt.get_future();
        std::thread helper(std::move(pt));

        if (future.wait_for(timeout) == std::future_status::timeout) {
            geode::log::warn("[TimedJoin] Thread did not finish in {}ms, detaching", timeout.count());
            if (t.joinable()) t.detach();
            // helper may be stuck in t.join() — detach to avoid blocking here
            if (helper.joinable()) helper.detach();
            return false;
        }
        future.get();
        if (helper.joinable()) helper.join();
        return true;
#endif
    } catch (std::system_error const& e) {
        // DLL_PROCESS_DETACH or similar teardown — thread primitives
        // are unavailable. Best we can do is detach and move on.
        geode::log::warn("[TimedJoin] system_error during join (process teardown?): {}, detaching", e.what());
        if (t.joinable()) t.detach();
        return false;
    }
}

/// Attempt to wait on a std::future with a timeout. If the future doesn't
/// resolve in time, the future is discarded (the task continues in background).
/// Returns true if the future resolved, false on timeout.
template <typename T>
bool timedWait(std::future<T>& f, std::chrono::milliseconds timeout = std::chrono::seconds(3)) {
    if (!f.valid()) return true;
    auto status = f.wait_for(timeout);
    if (status == std::future_status::timeout) {
        geode::log::warn("[TimedWait] Future did not resolve in {}ms, abandoning", timeout.count());
        // Let the future go out of scope — the task keeps running but we don't wait
        std::future<T> abandoned;
        std::swap(f, abandoned);
        return false;
    }
    // Consume the result to avoid "broken promise" issues
    (void)f.get();
    return true;
}

} // namespace paimon
