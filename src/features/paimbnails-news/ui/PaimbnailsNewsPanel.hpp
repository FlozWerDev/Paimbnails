#pragma once

#include "../services/PaimbnailsNewsService.hpp"
#include "NewsRichText.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/GJCommentListLayer.hpp>
#include <Geode/ui/ScrollLayer.hpp>

namespace paimon::news {

// scrollable feed of Paimbnails announcements, sized to sit exactly where the
// vanilla NewsPopup draws its page list. Build it before the popup is shown so
// its scroll layer and menu register touches above the popup.
class PaimbnailsNewsPanel : public cocos2d::CCNode {
public:
    static PaimbnailsNewsPanel* create(cocos2d::CCSize const& size, std::string channelId = {});

    // shows the panel, takes the mouse wheel and refreshes a stale feed.
    void setActive(bool active);
    void refresh();
    std::string const& channelId() const { return m_channelId; }

protected:
    bool init(cocos2d::CCSize const& size, std::string channelId);

    void requestFeed(bool force);
    void onFeed(bool ok, NewsFeed const& feed);
    void showStatus(std::string const& text, bool withRetry);
    void hideStatus();
    void rebuildList(NewsFeed const& feed);
    cocos2d::CCNode* createItemNode(NewsItem const& item, float width);
    cocos2d::CCNode* createEmbedNode(NewsEmbed const& embed, float width);
    // play icon over a video's poster frame; opens the post when pressed.
    void addPlayButton(cocos2d::CCNode* box, cocos2d::CCSize const& size, std::string const& url);
    LinkCallback linkHandler();
    // asks before leaving the game, like Discord does for outside links.
    void openLink(std::string const& url, cocos2d::CCNode* sender);
    void onRetry(cocos2d::CCObject* sender);

    GJCommentListLayer* m_frame = nullptr;
    geode::ScrollLayer* m_scroll = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCMenu* m_statusMenu = nullptr;
    std::string m_channelId;
    bool m_loading = false;
    int m_shownRevision = 0;
};

} // namespace paimon::news
