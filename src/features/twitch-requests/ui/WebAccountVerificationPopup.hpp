#pragma once

#include "../../../ui/PaimonPopup.hpp"
#include "../services/RequestVerificationFlow.hpp"

namespace paimon::twitch {

class WebAccountVerificationPopup final : public PaimonPopup {
public:
    static WebAccountVerificationPopup* create();

protected:
    ~WebAccountVerificationPopup() override;
    bool init() override;
    void update(float) override;
    void onClose(cocos2d::CCObject* sender) override;

private:
    void onAutomatic();
    void onForgetComment();
    void refresh();

    std::shared_ptr<RequestVerificationFlow> m_flow;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCLabelBMFont* m_code = nullptr;
    CCMenuItemSpriteExtra* m_automatic = nullptr;
    CCMenuItemSpriteExtra* m_manual = nullptr;
    CCMenuItemSpriteExtra* m_copy = nullptr;
    CCMenuItemSpriteExtra* m_check = nullptr;
    CCMenuItemSpriteExtra* m_cleanup = nullptr;
    CCMenuItemSpriteExtra* m_forget = nullptr;
    std::string m_lastStatus;
    std::string m_lastCode;
};

} // namespace paimon::twitch
