#pragma once

#include <Geode/Geode.hpp>
#include <Geode/binding/MusicBrowserDelegate.hpp>

namespace paimon::menumusic {

class MusicTagsPopup : public geode::Popup, public MusicBrowserDelegate {
public:
    static MusicTagsPopup* create();

protected:
    bool init(float width, float height);
    void musicBrowserClosed(MusicBrowser* browser) override;

    void onGeometryDashAll(cocos2d::CCObject*);
    void onGeometryDashTags(cocos2d::CCObject*);
    void onNCSAll(cocos2d::CCObject*);
    void onNCSTags(cocos2d::CCObject*);
    void onNewgrounds(cocos2d::CCObject*);
    void openMusicBrowser(GJSongType type, bool showTags);
    void onExit() override;

    geode::WeakRef<cocos2d::CCNode> m_browser = nullptr;
};

} // namespace paimon::menumusic
