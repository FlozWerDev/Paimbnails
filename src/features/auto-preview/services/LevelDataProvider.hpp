#pragma once

#include <Geode/DefaultInclude.hpp>
#include <Geode/utils/function.hpp>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

class GJGameLevel;

namespace paimon::autopreview {

class LevelDataProvider {
public:
    // hard pre-guard shared with queue/renderer: level strings larger than
    // this are never forwarded for offscreen render (stall/OOM risk).
    static constexpr std::size_t kMaxLevelStringBytes = 3 * 1024 * 1024;
    static LevelDataProvider& get();
    static bool isInitialized();
    // nullptr on failure/timeout. main thread only.
    using Callback = geode::CopyableFunction<void(GJGameLevel*)>;

    void request(int levelID, Callback cb);
    bool busy() const { return m_busy; }
    // main-thread shutdown releases callbacks before Cocos tears down.
    void shutdown();

private:
    static constexpr std::size_t kMaxDownloadResponseBytes = kMaxLevelStringBytes + 64 * 1024;
    LevelDataProvider() = default;
    void completeRequest(std::uint64_t token, bool success, std::string const& response);
    void finish(GJGameLevel* level);

    bool m_busy = false;
    bool m_stopped = false;
    int m_levelID = 0;
    std::uint64_t m_token = 0;
    Callback m_cb;
    inline static std::atomic<bool> s_initialized{false};
};

} // namespace paimon::autopreview
