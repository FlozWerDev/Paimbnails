#pragma once

#include "GlassRuntime.hpp"
#include "GlassStyle.hpp"

#include <Geode/Geode.hpp>
#include <cstdint>
#include <optional>
#include <vector>

namespace paimon::glass {

// one Liquid Glass plate. Attached to a host it fills that host and, while
// the material is active, stops only the host's own GD art from drawing,
// so labels, icons and children stay on top and everything comes back
// untouched when the material is turned off. Hosts:
// - NineSlice / CCScale9Sprite: the slice batch is hidden (panels, buttons, insets, inputs);
// - CCSprite: the sprite's own quad (round/composite/masked UI buttons, slider grooves);
// - CCLayerColor: the layer's fill (GJListLayer / GJCommentListLayer frames);
// - geode::ListBorders: nothing of its own, its borders are companion art.
// free-standing plates (previews) keep the size they were created with.
class GlassNode : public cocos2d::CCNodeRGBA {
public:
    static GlassNode* create(cocos2d::CCSize const& size, GlassKind kind = GlassKind::Panel);
    static GlassNode* createForHost(cocos2d::CCNode* host, GlassKind kind, GlassScope scope);

    void setSelected(bool selected) { m_selected = selected; }
    // GD skin colour of a button; nullopt keeps the user's tint.
    void setSkinColor(std::optional<GlassColor> color) { m_skin = color; }
    void setGlyph(GlassGlyph glyph) { m_glyph = glyph; }
    void setShape(GlassShape shape) { m_shape = shape; }
    void setSpriteStyle(GlassSpriteStyle style) { m_spriteStyle = style; }
    void setFillFactor(float fill) { m_fill = fill; }
    void setSpriteDynamicSync(bool enabled) { m_dynamicSpriteSync = enabled; }
    GlassKind kind() const { return m_kind; }
    GlassShape shape() const { return m_shape; }
    // other GD art this plate stands in for (popup corners, list borders).
    // it is suppressed and restored together with the host's own art.
    void setCompanionArt(std::vector<cocos2d::CCNode*> const& nodes);

    // drop GL-side state while the old context is still current.
    static void onGLContextReloadAll();

    void visit() override;
    void draw() override;
    void onEnter() override;
    void onExit() override;
    ~GlassNode() override;

private:
    // a piece of art this plate currently keeps from drawing. Sprites and
    // layers keep their visibility (their owner may toggle it) and draw
    // nothing through a no-op blend; batches are hidden.
    struct HiddenArt {
        geode::WeakRef<cocos2d::CCNode> node; // empty for the host itself
        cocos2d::CCNode const* raw = nullptr; // identity only
        cocos2d::ccBlendFunc saved{};
        bool blend = false;
        bool self = false; // the host: reached through getParent(), never weakly held
    };

    bool initPlate(cocos2d::CCSize const& size, GlassKind kind, GlassScope scope, bool hostBound);
    bool syncToHost();
    void collectArt(std::vector<cocos2d::CCNode*>& out);
    void syncArt(std::vector<cocos2d::CCNode*> const& wanted);
    cocos2d::CCNode* lockArt(HiddenArt const& entry, geode::Ref<cocos2d::CCNode>& keep);
    void restoreArt(HiddenArt const& entry, cocos2d::CCNode* art);
    void restoreAllArt();
    void syncArtImmediate();
    void resolveRoot();
    float hostOpacity();
    float wellFill();
    bool hostPressed(bool& disabled);

    GlassKind m_kind = GlassKind::Panel;
    GlassScope m_scope = GlassScope::Always;
    GlassGlyph m_glyph = GlassGlyph::None;
    GlassShape m_shape = GlassShape::Auto;
    GlassSpriteStyle m_spriteStyle = GlassSpriteStyle::IconButton;
    float m_fill = 1.f;
    bool m_dynamicSpriteSync = false;
    cocos2d::CCTexture2D const* m_lastSpriteTex = nullptr;
    cocos2d::CCRect m_lastSpriteRect{};
    bool m_lastFlipX = false;
    bool m_hostBound = false;
    // the plate's parent; compared with getParent() before every use.
    cocos2d::CCNode* m_host = nullptr;
    std::uint8_t m_hostType = 0;
    std::optional<GlassColor> m_skin;
    bool m_selected = false;
    bool m_disabled = false;
    float m_press = 0.f;
    std::vector<geode::WeakRef<cocos2d::CCNode>> m_companions;
    std::vector<HiddenArt> m_hidden;
    std::vector<cocos2d::CCNode*> m_artScratch;
    void const* m_root = nullptr;
    bool m_rootDirty = true;
    bool m_blocked = false;
};

} // namespace paimon::glass
