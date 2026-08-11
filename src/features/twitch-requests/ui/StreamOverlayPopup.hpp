#pragma once

#include <Geode/Geode.hpp>

#include "../services/StreamOverlayServer.hpp"

#include <functional>
#include <string>

namespace geode { class ScrollLayer; }

namespace paimon::twitch {

class StreamOverlayPopup : public geode::Popup {
public:
    static StreamOverlayPopup* create();

protected:
    bool init() override;
    void update(float dt) override;
    void scrollWheel(float x, float y) override;

private:
    void apply(std::function<void(StreamOverlayConfig&)> const& change);
    void setEnabled(bool enabled);
    void copyOverlayUrl();
    void openPreview();
    void refreshStatus();

    StreamOverlayConfig m_config;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    float m_wheelTargetY = 0.f;
    bool m_wheelTargetSet = false;
    std::string m_lastStatus;
};

} // namespace paimon::twitch
