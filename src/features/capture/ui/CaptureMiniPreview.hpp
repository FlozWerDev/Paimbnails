#pragma once
#include <Geode/DefaultInclude.hpp>
#include <Geode/cocos/label_nodes/CCLabelBMFont.h>
#include <Geode/cocos/sprite_nodes/CCSprite.h>

namespace paimon::capture {

// live capture thumbnail shared by layer editor and asset browser; toggle
// bursts coalesce into one render.
class MiniPreview : public cocos2d::CCNode {
public:
    static MiniPreview* create(float width, float height);

    // mirror the popup's hidden players so the thumb matches the upload.
    void setPlayersHidden(bool hideP1, bool hideP2);

    // coalesced: several calls in the same frame render once.
    void requestRefresh();

    void refreshNow();

protected:
    bool init(float width, float height);
    void onEnter() override;
    void onExit() override;

private:
    void onRefreshTick(float dt);
    void showStatus(char const* text);

    cocos2d::CCSprite*      m_sprite   = nullptr;
    cocos2d::CCLabelBMFont* m_status   = nullptr;
    float m_viewWidth  = 0.f;
    float m_viewHeight = 0.f;
    bool  m_hideP1     = false;
    bool  m_hideP2     = false;
    bool  m_pending    = false;
    int   m_busyRetries = 0;
};

} // namespace paimon::capture
