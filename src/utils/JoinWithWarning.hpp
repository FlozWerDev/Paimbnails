#pragma once

#include <Geode/loader/Log.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string_view>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace paimon {

// worker must exit before its owner releases shared state.
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

// marks the done flag on every exit path so stopDecoding() can spot
// a stuck thread without platform timed-join APIs.
struct ThreadExitSignal {
    explicit ThreadExitSignal(std::atomic<bool>& done) : m_done(done) {
        m_done.store(false, std::memory_order_release);
    }
    ~ThreadExitSignal() { m_done.store(true, std::memory_order_release); }
    ThreadExitSignal(const ThreadExitSignal&) = delete;
    ThreadExitSignal& operator=(const ThreadExitSignal&) = delete;
private:
    std::atomic<bool>& m_done;
};

// bounded join for decode workers (crash #9): waits up to `timeout` for
// `threadExited()`, then joins. Stuck workers (corrupt file / hung read)
// get detached and return false. The detached orphan must only touch
// shared_ptr state captured at spawn, never `this`.
template <typename Pred>
inline bool joinOrDetachWithWarning(std::thread& t, std::chrono::milliseconds timeout,
                                    std::string_view ownerTag, Pred&& threadExited) {
    if (!t.joinable()) return true;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!threadExited()) {
        if (std::chrono::steady_clock::now() >= deadline) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (threadExited()) {
        t.join();
        return true;
    }
    geode::log::warn("[ThreadJoin] {} worker exceeded {}ms; detaching orphan (shared state keeps it alive)",
                     ownerTag, timeout.count());
    t.detach();
    return false;
}

// Bounded join without a custom done-flag: waits up to `timeout` for OS thread
// exit on Windows (or falls back to join on platforms without timed wait) and
// detaches if still stuck. Caller must guarantee the worker owns all state it
// touches (e.g. via shared_ptr).
inline bool joinOrDetachAfter(std::thread& t, std::chrono::milliseconds timeout,
                              std::string_view ownerTag) {
    if (!t.joinable()) return true;
#ifdef _WIN32
    DWORD result = WaitForSingleObject(
        t.native_handle(),
        static_cast<DWORD>(std::max<int64_t>(0, timeout.count()))
    );
    if (result == WAIT_OBJECT_0) {
        t.join();
        return true;
    }
    geode::log::warn("[ThreadJoin] {} worker exceeded {}ms (result={}); detaching orphan",
                     ownerTag, timeout.count(), result);
    t.detach();
    return false;
#else
    auto start = std::chrono::steady_clock::now();
    t.join();
    if (std::chrono::steady_clock::now() - start > timeout) {
        geode::log::warn("[ThreadJoin] {} worker exceeded {}ms before exiting", ownerTag, timeout.count());
    }
    return true;
#endif
}

}
