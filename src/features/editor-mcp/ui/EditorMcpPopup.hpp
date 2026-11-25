#pragma once

#include "../../../ui/PaimonPopup.hpp"
#include "../../editor-suite/EditorPopupKit.hpp"

#include <Geode/Geode.hpp>

namespace paimon::editormcp {

class EditorMcpPopup : public PaimonPopup {
public:
    static EditorMcpPopup* create();

private:
    bool init() override;
    void onClose(cocos2d::CCObject* sender) override;
    void update(float dt) override;

    void refreshState();
    void toggleServer();
    void copyHttpConfig();
    void copyBridgeConfig();
    void runTestRender();
    void setStatus(std::string const& text, cocos2d::ccColor3B color);

    cocos2d::CCLabelBMFont* m_serverStatusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_urlLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statsLabel = nullptr;
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCSprite* m_statusIcon = nullptr;
    cocos2d::CCNode* m_previewFrame = nullptr;
    cocos2d::CCSprite* m_previewSprite = nullptr;
    cocos2d::CCLabelBMFont* m_previewHint = nullptr;

    editor::kit::Pill m_togglePill;
    editor::kit::Pill m_httpPill;
    editor::kit::Pill m_bridgePill;
    editor::kit::Pill m_renderPill;

    float m_pollClock = 0.f;
};

} // namespace paimon::editormcp
