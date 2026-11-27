#pragma once

#include "../services/PaimbnailsNewsService.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/ui/NineSlice.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace paimon::news {

// Ranked layer showing the most-voted news channel providers.
// Voting ("Fav" +1) requires both an active GD account and a verified Paimbnails session.
class TopNewsForumsLayer final : public cocos2d::CCLayer {
public:
    using SelectChannelCallback = std::function<void(std::string const& channelId)>;

    static TopNewsForumsLayer* create(SelectChannelCallback onSelectChannel = {});
    static cocos2d::CCScene* scene(SelectChannelCallback onSelectChannel = {});
    static void open(SelectChannelCallback onSelectChannel = {});

private:
    bool init(SelectChannelCallback onSelectChannel);
    ~TopNewsForumsLayer() override;
    void onExit() override;
    void onEnterTransitionDidFinish() override;
    void keyBackClicked() override;

    void buildAmbientDecor(cocos2d::CCSize const& winSize);
    void buildChrome();
    void refreshAuthBanner();
    void refreshSummaryStats();
    void loadTop(bool force);
    void rebuildList();
    cocos2d::CCNode* createServerIconFace(NewsProvider const& provider, int rank);
    cocos2d::CCNode* createForumRow(NewsProvider const& provider, float width);

    void onBack(cocos2d::CCObject* sender);
    void onRefresh(cocos2d::CCObject* sender);
    void onOpenAuth();
    void onToggleFavorite(std::string const& channelId);
    void onOpenForum(std::string const& channelId);

    SelectChannelCallback m_onSelectChannel;
    std::vector<NewsProvider> m_providers;
    std::unordered_set<std::string> m_pendingVotes;
    std::string m_searchQuery;

    cocos2d::CCNode* m_panel = nullptr;
    cocos2d::CCNode* m_authBanner = nullptr;
    geode::NineSlice* m_authBorder = nullptr;
    cocos2d::CCNode* m_authIconSlot = nullptr;
    cocos2d::CCLabelBMFont* m_authLabel = nullptr;
    cocos2d::CCLabelBMFont* m_authStatsLabel = nullptr;
    CCMenuItemSpriteExtra* m_verifyButton = nullptr;
    CCMenuItemSpriteExtra* m_clearSearchButton = nullptr;
    cocos2d::CCLabelBMFont* m_panelSummaryLabel = nullptr;
    cocos2d::CCLabelBMFont* m_footerLabel = nullptr;
    geode::TextInput* m_searchInput = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    geode::LoadingSpinner* m_spinner = nullptr;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    bool m_loading = false;
    bool m_isExiting = false;
};

} // namespace paimon::news
