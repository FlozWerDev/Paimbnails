#include "LevelDataProvider.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>

#include "../../../utils/MainThreadDelay.hpp"
#include "../../../utils/GDRobTopCache.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;

namespace paimon::autopreview {

bool LevelDataProvider::isInitialized() {
    return s_initialized.load(std::memory_order_acquire);
}

LevelDataProvider& LevelDataProvider::get() {
    static auto* inst = []() {
        auto* provider = new LevelDataProvider();
        s_initialized.store(true, std::memory_order_release);
        return provider;
    }();
    return *inst;
}

void LevelDataProvider::request(int levelID, Callback cb) {
    auto fail = [&cb]() { if (cb) cb(nullptr); };
    if (m_stopped || paimon::isRuntimeShuttingDown()) { fail(); return; }
    if (m_busy || levelID <= 0) { fail(); return; }

    auto* glm = GameLevelManager::get();
    if (!glm) { fail(); return; }

    // if gd already has the full level cached, use it directly.
    if (auto* saved = glm->getSavedLevel(levelID)) {
        if (!saved->m_levelString.empty()) {
            if (saved->m_levelString.size() > kMaxLevelStringBytes) {
                geode::log::debug("[AutoPreview] skip level {} (cached size {} > {} bytes)",
                                  levelID, saved->m_levelString.size(), kMaxLevelStringBytes);
                if (cb) cb(nullptr);
                return;
            }
            if (cb) cb(saved);
            return;
        }
    }

    m_busy = true;
    m_levelID = levelID;
    m_cb = std::move(cb);

    auto const token = ++m_token;
    paimon::scheduleMainThreadDelay(20.f, [token]() {
        auto& self = LevelDataProvider::get();
        if (self.m_busy && self.m_token == token) {
            geode::log::warn("[AutoPreview] level download timed out for {}", self.m_levelID);
            self.finish(nullptr);
        }
    });

    // own the response callback rather than borrowing GD's single UI delegate.
    // no increment/extras: this speculative download must not count as a play.
    paimon::gd::postCached(
        "downloadGJLevel22.php",
        fmt::format("gameVersion=22&gdw=0&levelID={}&secret=Wmfd2893gb7", levelID),
        [token](bool success, std::string response) {
            LevelDataProvider::get().completeRequest(token, success, response);
        },
        paimon::gd::CachePolicy::None
    );
}

void LevelDataProvider::completeRequest(std::uint64_t token, bool success, std::string const& response) {
    if (!m_busy || m_token != token) return;
    if (paimon::isRuntimeShuttingDown()) {
        shutdown();
        return;
    }

    if (!success || response.empty() || response.front() == '-'
        || response.size() > kMaxDownloadResponseBytes || response.find(':') == std::string::npos) {
        finish(nullptr);
        return;
    }

    // hashes follow '#'; the native dictionary/level parser handles the same
    // fields and encoded level string as a normal downloaded level.
    auto const payload = response.substr(0, response.find('#'));
    Ref<cocos2d::CCDictionary> fields = GameLevelManager::responseToDict(payload.c_str(), true);
    if (!fields) {
        finish(nullptr);
        return;
    }
    Ref<GJGameLevel> level = GJGameLevel::create(fields.data(), true);
    if (!level || level->m_levelID != m_levelID || level->m_levelString.empty()
        || level->m_levelString.size() > kMaxLevelStringBytes) {
        finish(nullptr);
        return;
    }
    finish(level.data());
}

void LevelDataProvider::finish(GJGameLevel* level) {
    auto cb = std::move(m_cb);
    ++m_token;
    m_cb = nullptr;
    m_busy = false;
    m_levelID = 0;

    if (cb) cb(level);
}

void LevelDataProvider::shutdown() {
    m_stopped = true;
    ++m_token;
    m_busy = false;
    m_levelID = 0;
    m_cb = nullptr;
}

} // namespace paimon::autopreview
