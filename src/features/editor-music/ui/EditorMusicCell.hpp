#pragma once

// Mini player cell floating top-left of the editor. Audio logic lives in EditorMusicPlayer.

#include <Geode/Geode.hpp>

namespace paimon::editormusic {

class EditorMusicCell : public cocos2d::CCNode {
public:
    static EditorMusicCell* create();
    void refresh();
    void animateIn();   // slide + pop in
    void animateOut();  // animate out; stops the player and removes itself

    void onExit() override;

protected:
    bool init() override;

    void onPrev(cocos2d::CCObject*);
    void onPlayPause(cocos2d::CCObject*);
    void onNext(cocos2d::CCObject*);
    void onMode(cocos2d::CCObject*);
    void onPlaylist(cocos2d::CCObject*);

    void tickMonitor(float);  // pause/resume based on the editor's main channel

    cocos2d::CCLabelBMFont* m_trackLabel = nullptr;
    cocos2d::CCLabelBMFont* m_modeLabel = nullptr;
    cocos2d::CCSprite* m_playSprite = nullptr;
};

} // namespace paimon::editormusic
