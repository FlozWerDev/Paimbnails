#pragma once

#include "../data/VersusModes.hpp"
#include "../data/VersusTypes.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/TextInput.hpp>

#include <string>
#include <vector>

namespace paimon::versus {

// friendly rooms leave ladder ratings unchanged.
class VersusFriendlyPopup : public geode::Popup {
public:
    static VersusFriendlyPopup* create(Mode mode);

protected:
    bool init(Mode mode);
    void onEnter() override;
    void onExit() override;
    void onClose(cocos2d::CCObject* sender) override;

    void buildFormatRow();
    void buildRoomPanel(cocos2d::CCRect const& area);
    void buildJoinPanel(cocos2d::CCRect const& area);

    void refreshFormat();
    void setStatus(std::string const& text, bool error = false);
    void send(std::string const& target);

    void onFormatStep(cocos2d::CCObject* sender);
    void onCreate(cocos2d::CCObject* sender);
    void onCopy(cocos2d::CCObject* sender);
    void onJoin(cocos2d::CCObject* sender);

    Mode m_mode = Mode::Classic;
    size_t m_formatIndex = 0;
    bool m_busy = false;
    std::string m_code;
    std::vector<FormatDef const*> m_formats;

    cocos2d::CCLabelBMFont* m_formatName = nullptr;
    cocos2d::CCLabelBMFont* m_formatRule = nullptr;
    cocos2d::CCSprite* m_formatGlyph = nullptr;
    cocos2d::CCLabelBMFont* m_codeLabel = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    CCMenuItemSpriteExtra* m_copyButton = nullptr;
    geode::TextInput* m_target = nullptr;
    cocos2d::CCMenu* m_menu = nullptr;
};

} // namespace paimon::versus
