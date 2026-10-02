#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include <Geode/Geode.hpp>

#include "../sources/ChatSource.hpp"

namespace geode { class ScrollLayer; }

namespace paimon::twitch {

geode::Popup* createRequestQueueSelector();

class RequestSourcesPopup : public PaimonPopup {
public:
    static RequestSourcesPopup* create();

protected:
    bool init() override;
    void update(float dt) override;
    void scrollWheel(float x, float y) override;

private:
    void rebuild();
    void scheduleRebuild();
    void editRoute(bool reward, size_t index = SIZE_MAX);
    void editQueue();

    Platform m_platform = Platform::Twitch;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    std::string m_lastStatus;
    float m_wheelTargetY = 0.f;
    bool m_wheelTargetSet = false;
};

} // namespace paimon::twitch
