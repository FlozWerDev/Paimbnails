#pragma once
#include <Geode/DefaultInclude.hpp>
#include <Geode/cocos/label_nodes/CCLabelBMFont.h>
#include <Geode/cocos/sprite_nodes/CCSprite.h>

namespace paimon::capture {

// Live thumbnail of what the capture is going to look like, shared by the layer
// editor and the asset browser. Refreshes are coalesced: a burst of toggles
// (a whole category, "hide all") renders the level once instead of once per row.
class MiniPreview : public cocos2d::CCNode {
public:
    static MiniPreview* create(float width, float height);

    // Mirror the hidden players from the preview popup so the thumbnail matches
    // the image that will actually be uploaded.
    void setPlayersHidden(bool hideP1, bool hideP2);

    // Coalesced: several calls in the same frame render once.
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
