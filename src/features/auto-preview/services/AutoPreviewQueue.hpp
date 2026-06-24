#pragma once

#include <Geode/DefaultInclude.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace paimon::autopreview {

class AutoPreviewQueue {
public:
    static AutoPreviewQueue& get();
    void enqueueIfEligible(GJGameLevel* level);

private:
    AutoPreviewQueue() = default;

    void ensureTickScheduled();   // main thread
    void tick();                  // main thread
    void processNext();           // main thread
    void generateFor(GJGameLevel* level, int levelID); // main thread

    struct Pending {
        geode::Ref<GJGameLevel> level;
        std::chrono::steady_clock::time_point firstSeen;
        std::chrono::steady_clock::time_point lastSeen;
    };

    std::mutex m_mutex;
    std::unordered_map<int32_t, Pending> m_pending;
    bool m_active = false;
    bool m_tickScheduled = false;
    std::chrono::steady_clock::time_point m_lastGenAt{};
};

} // namespace paimon::autopreview
