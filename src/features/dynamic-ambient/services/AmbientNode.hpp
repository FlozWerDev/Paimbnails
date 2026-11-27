#pragma once

#include "AmbientRuntime.hpp"

#include <Geode/Geode.hpp>
#include <cstdint>
#include <vector>

namespace paimon::ambient {

enum class AmbientKind : std::uint8_t {
    Button,
    ButtonOverlay,
    Panel,
    Asset
};

// Child plate attached to a CCSprite, NineSlice, or CCScale9Sprite.
// Suppresses the host's own draw reversibly and redraws the exact original
// artwork shaded by the background's detected light source and ambient colors.
class AmbientNode : public cocos2d::CCNodeRGBA {
public:
    static AmbientNode* createForHost(cocos2d::CCNode* host, AmbientKind kind, AmbientScope scope);

    AmbientKind kind() const { return m_kind; }
    AmbientScope scope() const { return m_scope; }
    void setRevealImmediate() { m_effectBlend = 1.f; }

    static void onGLContextReloadAll(bool deleteHandles = true);

    void visit() override;
    void draw() override;
    void onEnter() override;
    void onExit() override;
    ~AmbientNode() override;

private:
    bool initPlate(cocos2d::CCNode* host, AmbientKind kind, AmbientScope scope);
    bool syncToHost();
    void resolveContext();
    void suppressArt();
    void restoreArt();
    float hostOpacity();
    bool hostPressed(bool& disabled) const;

    AmbientKind m_kind = AmbientKind::Button;
    AmbientScope m_scope = AmbientScope::Buttons;
    cocos2d::CCNode* m_host = nullptr;
    std::uint8_t m_hostType = 0;

    bool m_contextDirty = true;
    bool m_blocked = false;
    bool m_inPopup = false;
    cocos2d::CCScene const* m_ownerScene = nullptr;
    geode::WeakRef<cocos2d::CCNode> m_ownerItem;

    // Suppressed art state for clean restoration.
    bool m_suppressed = false;
    bool m_suppressedBlend = false;
    cocos2d::ccBlendFunc m_savedBlend{};
    geode::WeakRef<cocos2d::CCNode> m_suppressedBatch;

    float m_press = 0.f;
    float m_effectBlend = 1.f;
    bool m_disabled = false;
};

} // namespace paimon::ambient
