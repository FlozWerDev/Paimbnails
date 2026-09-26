#pragma once

// card effects never touch physics, hitboxes, geometry or speed, so a run
// with cards stays legitimate and Globed's synced state stays clean.

#include "../data/VersusCards.hpp"

#include <Geode/Geode.hpp>

#include <vector>

class PlayLayer;

namespace paimon::versus {

struct ActiveEffect {
    CardId card = CardId::Fog;
    float remaining = 0.f;
    float total = 0.f;
    bool fromRival = false;
};

class VersusEffects {
public:
    static VersusEffects& get();

    void attach(PlayLayer* layer);
    void detach();
    bool attached() const { return m_layer != nullptr; }

    // `fromRival` separates thrown cards from self-cast ones; only thrown
    // cards can be reflected or dispelled.
    void apply(CardId card, bool fromRival);
    void update(float dt);

    void dispelAll();
    bool has(CardId card) const;
    bool cardsLocked() const;
    // Eye lets the duel bar and rival hand through; Blackout takes the bar
    // from both.
    bool seesRival() const;
    bool barsHidden() const;
    bool reflectArmed() const { return m_reflect; }
    bool consumeReflect();

    std::vector<ActiveEffect> const& active() const { return m_active; }

private:
    VersusEffects() = default;

    void begin(CardId card, bool fromRival);
    void end(CardId card);
    void endAll();

    cocos2d::CCNode* overlayRoot();
    void addBand(float heightFraction, cocos2d::ccColor4B const& color, char const* id);
    void removeOverlay(char const* id);
    void flash(cocos2d::ccColor4B const& color, float duration);

    void applyCameraTransforms();
    void restoreCamera();

    PlayLayer* m_layer = nullptr;
    cocos2d::CCNode* m_overlay = nullptr;

    std::vector<ActiveEffect> m_active;
    bool m_reflect = false;

    // what our cards multiplied into the object scale last frame, so it
    // divides back out before the next one goes in.
    float m_cameraFactor = 1.f;
    bool m_cameraMirrored = false;
    float m_bombTimer = 0.f;
    float m_musicVolume = 1.f;
    float m_effectsVolume = 1.f;
    bool m_audioMuted = false;
};

} // namespace paimon::versus
