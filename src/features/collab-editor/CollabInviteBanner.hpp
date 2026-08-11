#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <utility>
#include <vector>

namespace paimon::collab {

// Incoming collab invite, shown as a single GD panel at the top of the screen
// with accept / reject and a countdown bar. It lives in geode::OverlayManager so
// it draws over popups and survives scene changes, and only its two buttons take
// touches — the game underneath stays playable.
class CollabInviteBanner : public cocos2d::CCNode {
public:
    static void present(std::string const& room, std::string const& fromName);

    void onEnter() override;
    void onExit() override;

private:
    enum class Phase { Enter, Hold, Accepted, Exit };

    static CollabInviteBanner* s_instance;

    bool init(std::string const& room, std::string const& fromName);
    void tick(float dt);
    void captureFade(); // remembers each node's authored opacity for the fades
    void applyAlpha(float alpha);
    void toPhase(Phase phase, float duration);
    void onAccept(cocos2d::CCObject*);
    void onReject(cocos2d::CCObject*);

    cocos2d::CCMenu* m_menu = nullptr;
    cocos2d::CCLabelBMFont* m_roomLabel = nullptr;
    cocos2d::CCLayerColor* m_barFill = nullptr;
    std::vector<std::pair<geode::Ref<cocos2d::CCNode>, GLubyte>> m_fade;
    std::string m_room;

    Phase m_phase = Phase::Enter;
    float m_phaseTime = 0.f;
    float m_phaseDuration = 0.f;
    float m_showY = 0.f;
    float m_hideY = 0.f;
    bool m_priorityQueued = false;
};

} // namespace paimon::collab
