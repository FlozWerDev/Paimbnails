#pragma once

#include <Geode/ui/Popup.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>

namespace paimon::updates {

// Popup con progreso de descarga del .geode + boton "Restart" al terminar.
class UpdateProgressPopup : public geode::Popup {
public:
    static UpdateProgressPopup* create();

protected:
    bool init() override;

    void startDownload();
    void onProgress(uint64_t received, uint64_t total);
    void onDone(bool ok, std::string const& msgOrPath);

    void onCancel(cocos2d::CCObject* sender);
    void onRestart(cocos2d::CCObject* sender);
    void onClose(cocos2d::CCObject* sender) override;

private:
    cocos2d::CCLabelBMFont* m_statusLabel = nullptr;
    cocos2d::CCLabelBMFont* m_percentLabel = nullptr;
    cocos2d::CCNode* m_barBg = nullptr;
    cocos2d::CCLayerColor* m_barFill = nullptr;

    cocos2d::CCMenu* m_actionMenu = nullptr;
    CCMenuItemSpriteExtra* m_cancelBtn = nullptr;
    CCMenuItemSpriteExtra* m_restartBtn = nullptr;

    bool m_finished = false;
    bool m_succeeded = false;
};

} // namespace paimon::updates
