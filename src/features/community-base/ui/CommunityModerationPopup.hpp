#pragma once

#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include <matjson.hpp>
#include <string>
#include <utility>
#include <vector>

namespace paimon::community {

// moderators: pending presets, gradients, icons and comment backgrounds, one at a time.
class CommunityModerationPopup : public PaimonPopup {
public:
    static CommunityModerationPopup* create();

protected:
    bool init() override;

private:
    std::string m_type = "presets";
    matjson::Value m_items = matjson::Value::array();
    size_t m_index = 0;
    bool m_busy = false;
    int m_token = 0;
    cocos2d::CCNode* m_card = nullptr;
    cocos2d::CCLabelBMFont* m_counter = nullptr;
    std::vector<std::pair<std::string, CCMenuItemSpriteExtra*>> m_tabs;
    CCMenuItemSpriteExtra* m_approve = nullptr;
    CCMenuItemSpriteExtra* m_reject = nullptr;

    void loadSummary();
    void load();
    void showCurrent();
    void decide(bool approve, std::string const& reason);
};

} // namespace paimon::community
