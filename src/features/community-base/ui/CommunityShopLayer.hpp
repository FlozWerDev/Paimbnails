#pragma once

#include <Geode/Geode.hpp>
#include <matjson.hpp>
#include <string>
#include <utility>
#include <vector>

namespace paimon::community {

// paimon shop, community side: every approved preset, gradient, icon and
// comment background, with likes, dislikes and downloads.
class CommunityShopLayer : public cocos2d::CCLayer {
public:
    // `type`: "presets", "gradients", "icons" or "backgrounds".
    static CommunityShopLayer* create(std::string const& type = "presets");
    static void open(std::string const& type = "presets");

protected:
    bool init(std::string const& type);
    void keyBackClicked() override;

private:
    std::string m_type;
    std::string m_filter;
    std::string m_sort = "new";
    int m_offset = 0;
    bool m_hasMore = false;
    int m_token = 0;
    cocos2d::CCMenu* m_cards = nullptr;
    cocos2d::CCMenu* m_filterMenu = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCLabelBMFont* m_message = nullptr;
    CCMenuItemSpriteExtra* m_sortButton = nullptr;
    CCMenuItemSpriteExtra* m_prev = nullptr;
    CCMenuItemSpriteExtra* m_next = nullptr;
    std::vector<std::pair<std::string, CCMenuItemSpriteExtra*>> m_tabs;
    std::vector<std::pair<std::string, CCMenuItemSpriteExtra*>> m_filters;

    void selectType(std::string const& type);
    void buildFilters();
    void load();
    void showItems(matjson::Value const& items);
    void openItem(matjson::Value const& item);
};

} // namespace paimon::community
