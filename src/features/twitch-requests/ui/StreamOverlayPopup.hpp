#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include <Geode/Geode.hpp>

#include "../services/StreamOverlayServer.hpp"

#include <functional>
#include <string>

namespace geode { class ScrollLayer; }

namespace paimon::twitch {

class StreamOverlayPopup : public PaimonPopup {
public:
    static StreamOverlayPopup* create();

protected:
    bool init() override;
    void update(float dt) override;
    bool scrollWheel(float x, float y) override;

private:
    void apply(std::function<void(StreamOverlayConfig&)> const& change);
    void setEnabled(bool enabled);
    void copyOverlayUrl();
    void openOverlayPage(std::string const& url, char const* warn);
    void openPreview();
    void openGallery();
    void refreshStatus();

    StreamOverlayConfig m_config;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    float m_wheelTargetY = 0.f;
    bool m_wheelTargetSet = false;
    float m_statusElapsed = 0.f;
    std::string m_lastStatus;
};

} // namespace paimon::twitch
