#pragma once

#include <Geode/DefaultInclude.hpp>
#include <Geode/utils/function.hpp>
#include <Geode/binding/LevelDownloadDelegate.hpp>
#include <cstdint>

class GJGameLevel;

namespace paimon::autopreview {

class LevelDataProvider : public LevelDownloadDelegate {
public:
    static LevelDataProvider& get();
    // nullptr on failure/timeout. Main thread only.
    using Callback = geode::CopyableFunction<void(GJGameLevel*)>;

    void request(int levelID, Callback cb);
    bool busy() const { return m_busy; }

    void levelDownloadFinished(GJGameLevel* level) override;
    void levelDownloadFailed(int response) override;

private:
    LevelDataProvider() = default;
    void finish(GJGameLevel* level);

    bool m_busy = false;
    int m_levelID = 0;
    int m_token = 0;
    Callback m_cb;
    LevelDownloadDelegate* m_prevDelegate = nullptr;
};

} // namespace paimon::autopreview
