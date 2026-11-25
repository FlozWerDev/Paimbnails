#pragma once
#include <Geode/Geode.hpp>
#include "../data/ShopPreset.hpp"

namespace paimon::shop {
class PaimonShopLayer : public cocos2d::CCLayer {
public:
    static PaimonShopLayer* create();
    static void open();
    ~PaimonShopLayer() override;
protected:
    bool init() override;
    void keyBackClicked() override;
private:
    enum class EmptyKind { Library, NoResults, Error };

    geode::TextInput* m_search = nullptr;
    // card buttons and empty-state actions; m_content holds the art around them.
    cocos2d::CCMenu* m_cards = nullptr;
    cocos2d::CCNode* m_content = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    CCMenuItemSpriteExtra* m_previous = nullptr;
    CCMenuItemSpriteExtra* m_next = nullptr;
    CCMenuItemSpriteExtra* m_restore = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_filters;
    int m_filter = -1;
    size_t m_page = 0;
    void refresh(bool animate = false);
    void showEmptyState(EmptyKind kind, std::string const& detail = {});
    CCMenuItemSpriteExtra* makeCard(Listing const& entry, cocos2d::CCSize size);
    void clearFilters();
    void openPreset(std::string const& id);
    void createPreset();
    void importPreset();
    void restoreBackup();
    std::function<void()> refreshCallback();
};
} // namespace paimon::shop
