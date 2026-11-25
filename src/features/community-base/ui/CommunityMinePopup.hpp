#pragma once

#include "../../../ui/PaimonPopup.hpp"
#include <Geode/Geode.hpp>
#include <matjson.hpp>
#include <string>
#include <utility>
#include <vector>

namespace paimon::community {

// the player's own submissions of every type, with their review status,
// ratings and downloads.
class CommunityMinePopup : public PaimonPopup {
public:
    // `type`: the tab shown first ("presets", "gradients", "icons" or "backgrounds").
    static CommunityMinePopup* create(std::string const& type = "presets");

protected:
    bool init(std::string const& type);

private:
    std::string m_type;
    matjson::Value m_items = matjson::Value::array();
    size_t m_page = 0;
    int m_token = 0;
    cocos2d::CCNode* m_list = nullptr;
    CCMenuItemSpriteExtra* m_prev = nullptr;
    CCMenuItemSpriteExtra* m_next = nullptr;
    std::vector<std::pair<std::string, CCMenuItemSpriteExtra*>> m_tabs;

    void selectType(std::string const& type);
    void load();
    void showPage();
    void remove(std::string const& id, std::string const& title);
};

} // namespace paimon::community
