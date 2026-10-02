#pragma once
#include "../../../ui/PaimonPopup.hpp"

#include "../ThumbRequests.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/ScrollLayer.hpp>

#include <vector>

namespace paimon::thumbreq {

// the request queue as the team left it: asked, sent, with which difficulty and rate.
class ThumbRequestsPopup : public PaimonPopup {
public:
    static ThumbRequestsPopup* create();

protected:
    bool init() override;
    void onFilter(cocos2d::CCObject* sender);
    void onOpenLevel(cocos2d::CCObject* sender);
    void onVideo(cocos2d::CCObject* sender);

    void reload();
    void buildRows();
    void showMessage(std::string const& text);
    cocos2d::CCNode* createRow(Request const& request, float y, bool odd);

    int m_filter = 0;
    // bumped per reload: a stale filter's reply can't clobber the current list.
    int m_generation = 0;
    bool m_loading = true;
    bool m_failed = false;
    std::vector<Request> m_requests;
    geode::ScrollLayer* m_scroll = nullptr;
    std::vector<CCMenuItemSpriteExtra*> m_filterButtons;
};

} // namespace paimon::thumbreq
