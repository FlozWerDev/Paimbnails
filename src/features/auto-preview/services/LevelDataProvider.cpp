#include "LevelDataProvider.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>

#include "../../../utils/MainThreadDelay.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;

namespace paimon::autopreview {

LevelDataProvider& LevelDataProvider::get() {
    static auto* inst = new LevelDataProvider();
    return *inst;
}

void LevelDataProvider::request(int levelID, Callback cb) {
    if (paimon::isRuntimeShuttingDown()) { if (cb) cb(nullptr); return; }
    if (m_busy || levelID <= 0) { if (cb) cb(nullptr); return; }

    auto* glm = GameLevelManager::get();
    if (!glm) { if (cb) cb(nullptr); return; }

    // If GD already has the full level cached, use it directly.
    if (auto* saved = glm->getSavedLevel(levelID)) {
        if (!saved->m_levelString.empty()) {
            if (cb) cb(saved);
            return;
        }
    }

    m_busy = true;
    m_levelID = levelID;
    m_cb = std::move(cb);
    m_prevDelegate = glm->m_levelDownloadDelegate;
    glm->m_levelDownloadDelegate = this;

    int const token = ++m_token;
    glm->downloadLevel(levelID, false, 0);
    paimon::scheduleMainThreadDelay(20.f, [token]() {
        auto& self = LevelDataProvider::get();
        if (self.m_busy && self.m_token == token) {
            geode::log::warn("[AutoPreview] level download timed out for {}", self.m_levelID);
            self.finish(nullptr);
        }
    });
}

void LevelDataProvider::levelDownloadFinished(GJGameLevel* level) {
    if (!m_busy) return;
    finish(level);
}

void LevelDataProvider::levelDownloadFailed(int) {
    if (!m_busy) return;
    finish(nullptr);
}

void LevelDataProvider::finish(GJGameLevel* level) {
    auto cb = m_cb;
    auto* glm = GameLevelManager::get();
    if (glm && glm->m_levelDownloadDelegate == this) {
        glm->m_levelDownloadDelegate = m_prevDelegate;
    }
    m_prevDelegate = nullptr;
    ++m_token;
    m_cb = nullptr;
    m_busy = false;
    m_levelID = 0;

    if (cb) cb(level);
}

} // namespace paimon::autopreview
