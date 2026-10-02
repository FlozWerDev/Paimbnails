#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include "../data/ProgressionBadges.hpp"
#include <string>
#include <vector>

namespace paimon::progression {

class ProgressionPopup : public PaimonPopup {
public:
    static ProgressionPopup* create(BadgeContext const& ctx, std::string const& username);

protected:
    bool init(BadgeContext const& ctx, std::string const& username);

    void buildTabRow();
    void showTab(int index, bool animated);
    cocos2d::CCNode* buildOverview();
    cocos2d::CCNode* buildSources();
    cocos2d::CCNode* buildBadges();
    void rebuildBadgeGrid();

    void onTab(cocos2d::CCObject* sender);
    void onCategory(cocos2d::CCObject* sender);
    void onBadge(cocos2d::CCObject* sender);
    void onFormulaInfo(cocos2d::CCObject* sender);

    BadgeContext m_ctx;
    int m_tab = 0;
    int m_category = -1;
    cocos2d::CCNode* m_pageHolder = nullptr;
    cocos2d::CCNode* m_page = nullptr;
    geode::ScrollLayer* m_grid = nullptr;
    std::vector<cocos2d::CCNode*> m_tabs;
    std::vector<cocos2d::CCNode*> m_categoryChips;
    std::vector<BadgeDef const*> m_gridBadges;
};

} // namespace paimon::progression
