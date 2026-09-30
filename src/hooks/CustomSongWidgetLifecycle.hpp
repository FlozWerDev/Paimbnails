#pragma once

#include <Geode/binding/CustomSongWidget.hpp>
#include <atomic>
#include <mutex>
#include <unordered_set>

// fmod may call back during onstopplaytest; liveness + teardown window guard uaf

namespace paimon::csw {

class Lifecycle {
public:
    static void registerWidget(CustomSongWidget* widget) {
        if (!widget) return;
        std::lock_guard lock(mutex());
        alive().insert(widget);
    }

    static void unregisterWidget(CustomSongWidget* widget) {
        if (!widget) return;
        std::lock_guard lock(mutex());
        alive().erase(widget);
    }

    static bool isAlive(CustomSongWidget const* widget) {
        if (!widget) return false;
        std::lock_guard lock(mutex());
        return alive().count(const_cast<CustomSongWidget*>(widget)) != 0;
    }

    static void beginEditorTeardown() {
        editorTeardownDepth().fetch_add(1, std::memory_order_acq_rel);
    }

    static void endEditorTeardown() {
        auto& depth = editorTeardownDepth();
        auto prev = depth.fetch_sub(1, std::memory_order_acq_rel);
        if (prev <= 1) {
            depth.store(0, std::memory_order_release);
        }
    }

    static bool isEditorTeardown() {
        return editorTeardownDepth().load(std::memory_order_acquire) > 0;
    }

    // parentless widgets during teardown are already detached
    static bool shouldSkipDelegateCall(CustomSongWidget const* widget) {
        if (!isAlive(widget)) return true;
        if (!isEditorTeardown()) return false;
        auto* mutableWidget = const_cast<CustomSongWidget*>(widget);
        return mutableWidget->getParent() == nullptr;
    }

private:
    static std::mutex& mutex() {
        static std::mutex m;
        return m;
    }

    static std::unordered_set<CustomSongWidget*>& alive() {
        static std::unordered_set<CustomSongWidget*> s;
        return s;
    }

    static std::atomic<int>& editorTeardownDepth() {
        static std::atomic<int> depth{0};
        return depth;
    }
};

} // namespace paimon::csw