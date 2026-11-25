#include "GlassNode.hpp"
#include "GlassCapture.hpp"
#include "GlassDecorator.hpp"
#include "GlassGeometry.hpp"
#include "../../../utils/GLSLLoader.hpp"

#include <Geode/binding/CCScrollLayerExt.hpp>
#include <Geode/binding/DialogLayer.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/GJDropDownLayer.hpp>
#include <Geode/binding/MenuGameLayer.hpp>
#include <Geode/binding/UILayer.hpp>
#include <Geode/cocos/extensions/GUI/CCControlExtension/CCScale9Sprite.h>
#include <Geode/cocos/kazmath/include/kazmath/GL/matrix.h>
#include <Geode/cocos/shaders/ccGLStateCache.h>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/ui/General.hpp>
#include <Geode/ui/NineSlice.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

using namespace geode::prelude;

namespace paimon::glass {
namespace {

enum HostType : std::uint8_t {
    kNoHost = 0,
    kNineSliceHost = 1,
    kScale9Host = 2,
    kSpriteHost = 3,  // UI button / slider groove sprite
    kLayerHost = 4,   // list frame fill
    kBordersHost = 5, // geode::ListBorders
};

// round buttons: the circle's share of the sprite's shorter side. gd's
// circle art keeps a margin for its outline and shadow.
constexpr float kRoundButtonFill = 0.86f;
// wells sit concentric inside their panel.
constexpr float kWellCornerShare = 0.7f;

// leaves the framebuffer as is: suppressed art still runs draw() but adds
// nothing, and its visibility stays its owner's.
constexpr ccBlendFunc kNoDraw{GL_ZERO, GL_ONE};

bool isNoDraw(ccBlendFunc const& blend) {
    return blend.src == kNoDraw.src && blend.dst == kNoDraw.dst;
}

// sprites and layers are suppressed through their blend; anything else
// (slice batches, nineslice borders) is hidden.
CCBlendProtocol* blendOf(CCNode* node) {
    if (auto* sprite = typeinfo_cast<CCSprite*>(node)) {
        // a batched sprite is drawn by its batch, with the batch's blend.
        return sprite->getBatchNode() ? nullptr : sprite;
    }
    if (auto* layer = typeinfo_cast<CCLayerColor*>(node)) return layer;
    return nullptr;
}

struct PlateProgram {
    Ref<CCGLProgram> program;
    bool failed = false;
    GLint size = -1;
    GLint shape = -1;
    GLint fbOrigin = -1;
    GLint fbAxisX = -1;
    GLint fbAxisY = -1;
    GLint sharpMap = -1;
    GLint sharpClamp = -1;
    GLint blurMap = -1;
    GLint blurClamp = -1;
    GLint tint = -1;
    GLint material = -1;
    GLint light = -1;
    GLint shadow = -1;
    GLint state = -1;
    GLint spriteMap0 = -1;
    GLint spriteMap1 = -1;
    GLint spriteOpts = -1;
};

PlateProgram& plateProgram() {
    static PlateProgram program;
    return program;
}

PlateProgram* ensurePlateProgram() {
    auto& plate = plateProgram();
    if (plate.program) return &plate;
    if (plate.failed) return nullptr;
    auto* program = paimon::shaders::loadShader(
        "paimbnails/glass-ui-v5", "glass_plate_vertex.glsl", "liquid_glass_ui.glsl", nullptr, nullptr);
    if (!program) {
        // without the material the original art simply stays visible.
        plate.failed = true;
        log::warn("[Glass] plate shader unavailable; Liquid Glass stays off");
        return nullptr;
    }
    GLuint const id = program->getProgram();
    auto location = [id](char const* name) { return glGetUniformLocation(id, name); };
    plate.size = location("u_size");
    plate.shape = location("u_shape");
    plate.fbOrigin = location("u_fbOrigin");
    plate.fbAxisX = location("u_fbAxisX");
    plate.fbAxisY = location("u_fbAxisY");
    plate.sharpMap = location("u_sharpMap");
    plate.sharpClamp = location("u_sharpClamp");
    plate.blurMap = location("u_blurMap");
    plate.blurClamp = location("u_blurClamp");
    plate.tint = location("u_tint");
    plate.material = location("u_material");
    plate.light = location("u_light");
    plate.shadow = location("u_shadow");
    plate.state = location("u_state");
    plate.spriteMap0 = location("u_spriteMap0");
    plate.spriteMap1 = location("u_spriteMap1");
    plate.spriteOpts = location("u_spriteOpts");
    program->use();
    glUniform1i(location("u_texture"), 0);
    glUniform1i(location("u_blurTex"), 1);
    glUniform1i(location("u_spriteTex"), 2);
    plate.program = program;
    return &plate;
}

// real binding plus cocos' cache, ending on unit 0 like the rest of cocos.
void bindUnit(GLuint unit, GLuint texture) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture);
    ccGLBindTexture2DN(unit, texture);
    glActiveTexture(GL_TEXTURE0);
}

bool spriteMaskQuadReady(CCSprite* sprite) {
    if (!sprite) return false;
    auto* tex = sprite->getTexture();
    if (!tex || !tex->getName()) return false;
    auto const quad = sprite->getQuad();
    float const qw = quad.tr.vertices.x - quad.bl.vertices.x;
    float const qh = quad.tr.vertices.y - quad.bl.vertices.y;
    if (!(qw > 0.5f) || !(qh > 0.5f) || !std::isfinite(qw) || !std::isfinite(qh)) return false;
    float const ax = quad.br.texCoords.u - quad.bl.texCoords.u;
    float const ay = quad.br.texCoords.v - quad.bl.texCoords.v;
    float const bx = quad.tl.texCoords.u - quad.bl.texCoords.u;
    float const by = quad.tl.texCoords.v - quad.bl.texCoords.v;
    float const lenA = std::hypot(ax, ay);
    float const lenB = std::hypot(bx, by);
    return std::isfinite(lenA) && std::isfinite(lenB) && lenA > 1e-5f && lenB > 1e-5f;
}

std::uint8_t hostTypeOf(CCNode* host) {
    if (typeinfo_cast<NineSlice*>(host)) return kNineSliceHost;
    if (typeinfo_cast<extension::CCScale9Sprite*>(host)) return kScale9Host;
    if (auto* sprite = typeinfo_cast<CCSprite*>(host)) {
        // a batched sprite's quad belongs to its batch and cannot be suppressed alone.
        return sprite->getBatchNode() ? kNoHost : kSpriteHost;
    }
    if (typeinfo_cast<CCLayerColor*>(host)) return kLayerHost;
    if (typeinfo_cast<ListBorders*>(host)) return kBordersHost;
    return kNoHost;
}

} // namespace

GlassNode* GlassNode::create(CCSize const& size, GlassKind kind) {
    auto* node = new GlassNode();
    if (node->initPlate(size, kind, GlassScope::Always, false)) {
        node->autorelease();
        return node;
    }
    delete node;
    return nullptr;
}

GlassNode* GlassNode::createForHost(CCNode* host, GlassKind kind, GlassScope scope) {
    if (!host) return nullptr;
    auto const type = hostTypeOf(host);
    if (type == kNoHost) return nullptr;
    auto* node = new GlassNode();
    auto size = host->getContentSize();
    size.width = std::max(1.f, std::abs(size.width));
    size.height = std::max(1.f, std::abs(size.height));
    if (!node->initPlate(size, kind, scope, true)) {
        delete node;
        return nullptr;
    }
    node->m_host = host;
    node->m_hostType = type;
    node->autorelease();
    return node;
}

bool GlassNode::initPlate(CCSize const& size, GlassKind kind, GlassScope scope, bool hostBound) {
    if (!std::isfinite(size.width) || !std::isfinite(size.height) ||
        size.width <= 0.f || size.height <= 0.f) return false;
    if (!CCNodeRGBA::init()) return false;
    m_kind = kind;
    m_scope = scope;
    m_hostBound = hostBound;
    this->setContentSize(size);
    // a host-bound plate shares its host's local space: (0, 0) is the host's
    // bottom-left corner. previews are positioned by their centre.
    this->setAnchorPoint(hostBound ? CCPoint{0.f, 0.f} : CCPoint{0.5f, 0.5f});
    this->setPosition({0.f, 0.f});
    this->setID("paimon-glass-node"_spr);
    return true;
}

GlassNode::~GlassNode() {
    // getparent() is already null here: the host's own art is left to the
    // dying host, other art (companions) gets its look back.
    restoreAllArt();
}

void GlassNode::onEnter() {
    CCNodeRGBA::onEnter();
    m_rootDirty = true;
    syncArtImmediate();
}

void GlassNode::onExit() {
    // leaving the scene (popup closed, node moved): give the art back.
    restoreAllArt();
    CCNodeRGBA::onExit();
}

void GlassNode::syncArtImmediate() {
    auto& runtime = GlassRuntime::get();
    if (!m_hostBound || !m_host || getParent() != m_host || !isVisible() ||
        !runtime.allows(m_scope) || !ensurePlateProgram()) {
        return;
    }
    if (m_rootDirty) resolveRoot();
    if (m_blocked) return;
    m_artScratch.clear();
    collectArt(m_artScratch);
    syncArt(m_artScratch);
}

void GlassNode::setCompanionArt(std::vector<CCNode*> const& nodes) {
    // rescans pass the same nodes again; keep the weak references then.
    bool same = nodes.size() == m_companions.size();
    for (std::size_t i = 0; same && i < nodes.size(); ++i) {
        same = m_companions[i].lock().data() == nodes[i];
    }
    if (!same) {
        m_companions.clear();
        m_companions.reserve(nodes.size());
        for (auto* node : nodes) {
            if (node && node != this && node != m_host) m_companions.emplace_back(node);
        }
    }
    // Suppress companion art immediately so lower-z corners/borders never
    // draw their vanilla frame before this plate visits on Frame 0.
    if (isRunning()) syncArtImmediate();
}

bool GlassNode::syncToHost() {
    auto size = m_host->getContentSize();
    CCPoint origin{0.f, 0.f};
    if (m_hostType == kSpriteHost) {
        auto* sprite = static_cast<CCSprite*>(m_host);
        if (m_dynamicSpriteSync) {
            auto const* tex = sprite->getTexture();
            auto const rect = sprite->getTextureRect();
            bool const flipX = sprite->isFlipX();
            if (tex != m_lastSpriteTex || !rect.equals(m_lastSpriteRect) || flipX != m_lastFlipX) {
                m_lastSpriteTex = tex;
                m_lastSpriteRect = rect;
                m_lastFlipX = flipX;
                refreshSpritePlate(this, sprite);
            }
        }
        if (m_shape == GlassShape::SpriteMask) {
            // Align exactly with the sprite's trimmed quad so UV mapping matches 1:1.
            auto const quad = sprite->getQuad();
            float const qw = quad.tr.vertices.x - quad.bl.vertices.x;
            float const qh = quad.tr.vertices.y - quad.bl.vertices.y;
            if (qw > 0.5f && qh > 0.5f && std::isfinite(qw) && std::isfinite(qh)) {
                origin = CCPoint{quad.bl.vertices.x, quad.bl.vertices.y};
                size = CCSize{qw, qh};
            }
        } else if (m_shape == GlassShape::Circle || (m_shape == GlassShape::Auto && m_kind == GlassKind::Button)) {
            // a round button: a circle centred on the sprite's art.
            float const fill = m_fill < 0.999f ? m_fill : kRoundButtonFill;
            float const side = std::min(std::abs(size.width), std::abs(size.height)) * fill;
            origin = CCPoint{(size.width - side) / 2.f, (size.height - side) / 2.f};
            size = CCSize{side, side};
        } else if (m_fill < 0.999f && m_fill > 0.1f) {
            float const w = std::abs(size.width) * m_fill;
            float const h = std::abs(size.height) * m_fill;
            origin = CCPoint{(size.width - w) / 2.f, (size.height - h) / 2.f};
            size = CCSize{w, h};
        }
    }
    if (!(size.width > 0.f) || !(size.height > 0.f) ||
        !std::isfinite(size.width) || !std::isfinite(size.height)) return false;
    if (size != getContentSize()) setContentSize(size);
    if (!getPosition().equals(origin)) setPosition(origin);
    return true;
}

void GlassNode::collectArt(std::vector<CCNode*>& out) {
    switch (m_hostType) {
        case kNineSliceHost:
            if (auto* batch = static_cast<NineSlice*>(m_host)->getBatchNode()) out.push_back(batch);
            break;
        case kScale9Host:
            if (auto* batch = static_cast<extension::CCScale9Sprite*>(m_host)->_scale9Image) out.push_back(batch);
            break;
        case kSpriteHost:
        case kLayerHost:
            // their own quad or fill only; children still draw.
            out.push_back(m_host);
            break;
        default:
            break;
    }
    for (auto const& weak : m_companions) {
        if (auto node = weak.lock()) out.push_back(node.data());
    }
}

CCNode* GlassNode::lockArt(HiddenArt const& entry, Ref<CCNode>& keep) {
    if (entry.self) return m_host && getParent() == m_host ? m_host : nullptr;
    keep = entry.node.lock();
    return keep.data();
}

void GlassNode::restoreArt(HiddenArt const& entry, CCNode* art) {
    if (!art) return;
    if (!entry.blend) {
        art->setVisible(true);
        return;
    }
    // undo only our own no-op blend; a blend set since then is its owner's.
    if (auto* blend = blendOf(art); blend && isNoDraw(blend->getBlendFunc())) {
        blend->setBlendFunc(entry.saved);
    }
}

void GlassNode::restoreAllArt() {
    for (auto const& entry : m_hidden) {
        Ref<CCNode> keep;
        restoreArt(entry, lockArt(entry, keep));
    }
    m_hidden.clear();
}

void GlassNode::syncArt(std::vector<CCNode*> const& wanted) {
    // give back whatever is no longer replaced: the plate went inactive,
    // the host rebuilt its slices or a companion left the list.
    std::erase_if(m_hidden, [&](HiddenArt const& entry) {
        if (std::ranges::find(wanted, entry.raw) != wanted.end()) return false;
        Ref<CCNode> keep;
        restoreArt(entry, lockArt(entry, keep));
        return true;
    });
    for (auto* art : wanted) {
        auto it = std::ranges::find(m_hidden, static_cast<CCNode const*>(art), &HiddenArt::raw);
        bool const known = it != m_hidden.end();
        if (auto* blend = blendOf(art)) {
            auto const current = blend->getBlendFunc();
            if (isNoDraw(current)) continue;
            // new art, or its owner reset the blend (settexture does): keep
            // that one to restore.
            if (known) {
                it->saved = current;
                it->blend = true;
            } else {
                HiddenArt entry;
                entry.self = art == m_host;
                if (!entry.self) entry.node = WeakRef<CCNode>(art);
                entry.raw = art;
                entry.saved = current;
                entry.blend = true;
                m_hidden.push_back(std::move(entry));
            }
            blend->setBlendFunc(kNoDraw);
        } else if (art != m_host && art->isVisible()) {
            // only hide art that is showing; art hidden by its owner stays
            // theirs. the host itself is never hidden: the plate is its child.
            art->setVisible(false);
            if (!known) {
                HiddenArt entry;
                entry.self = art == m_host;
                if (!entry.self) entry.node = WeakRef<CCNode>(art);
                entry.raw = art;
                m_hidden.push_back(std::move(entry));
            }
        }
    }
}

void GlassNode::resolveRoot() {
    m_rootDirty = false;
    m_root = nullptr;
    bool gameplay = false;
    // Buttons inside a scroll list row (LevelCell, CommentCell, virtual rows)
    // keep their own menu as root because each row draws its own background
    // right before its buttons. Top-level UI layer buttons share their scene
    // layer's root so all buttons on MenuLayer / CreatorLayer / etc. share a
    // single framebuffer copy instead of exhausting the capture budget.
    CCNode* menu = nullptr;
    bool inScroll = false;
    CCNode* child = this;
    for (CCNode* node = getParent(); node; child = node, node = node->getParent()) {
        if (m_kind == GlassKind::Button && !menu && typeinfo_cast<CCMenu*>(node)) menu = node;
        if (typeinfo_cast<CCScrollLayerExt*>(node)) inScroll = true;
        // gameplay, level editor, animated menu background and gameplay HUD: stay out.
        if (typeinfo_cast<GJBaseGameLayer*>(node) || typeinfo_cast<MenuGameLayer*>(node) ||
            typeinfo_cast<UILayer*>(node) || typeinfo_cast<EditorUI*>(node)) {
            gameplay = true;
        }
        // every plate of one popup / dropdown / dialog shares its copies.
        if (typeinfo_cast<FLAlertLayer*>(node) || typeinfo_cast<GJDropDownLayer*>(node) ||
            typeinfo_cast<DialogLayer*>(node)) {
            if (!m_root) m_root = node;
            // keep walking up to be sure we are not inside a gameplay layer.
            continue;
        }
        if (!node->getParent()) {
            if (!m_root) m_root = child;
            break;
        }
    }
    if (!m_root) m_root = getParent();
    if (menu && inScroll) m_root = menu;
    m_blocked = gameplay && m_scope != GlassScope::Always;
}

float GlassNode::hostOpacity() {
    if (!m_hostBound) return static_cast<float>(getDisplayedOpacity()) / 255.f;
    if (!m_host || getParent() != m_host) return 0.f;
    CCRGBAProtocol* rgba = nullptr;
    switch (m_hostType) {
        case kNineSliceHost:
        case kScale9Host:
        case kSpriteHost: rgba = static_cast<CCNodeRGBA*>(m_host); break;
        case kLayerHost: rgba = static_cast<CCLayerColor*>(m_host); break;
        default: return 1.f; // listborders is a plain ccnode
    }
    // ratio of displayed to own opacity: parent fades (popup exits) apply,
    // while a translucent design value (insets at 80) keeps solid glass.
    float const own = static_cast<float>(rgba->getOpacity());
    // a list made clear on purpose (custom backgrounds) still gets its frame.
    if (own <= 0.f) return m_hostType == kLayerHost ? 1.f : 0.f;
    return std::min(1.f, static_cast<float>(rgba->getDisplayedOpacity()) / own);
}

// how dark a well is: follows the design opacity of the art it stands in
// for, so a list another feature made clear keeps only its frame.
float GlassNode::wellFill() {
    if (!m_hostBound) return 0.25f;
    if (!m_host || getParent() != m_host) return 0.f;
    float own = 0.f;
    switch (m_hostType) {
        case kNineSliceHost:
        case kScale9Host: own = static_cast<float>(static_cast<CCNodeRGBA*>(m_host)->getOpacity()); break;
        case kLayerHost: own = static_cast<float>(static_cast<CCLayerColor*>(m_host)->getOpacity()); break;
        case kSpriteHost: return 0.34f; // recessed groove (e.g. slider track)
        default: return 0.f; // drawn over its content: frame only
    }
    if (own <= 0.f) return 0.f;
    return std::min(0.42f, 0.08f + 0.36f * own / 255.f);
}

bool GlassNode::hostPressed(bool& disabled) {
    disabled = false;
    if (m_kind != GlassKind::Button || !m_host) return false;
    // a ButtonSprite background sits two levels below its menu item, a
    // sprite button one level, and a toggler child up to three levels.
    CCMenuItem* item = nullptr;
    CCNode* node = m_host->getParent();
    for (int depth = 0; node && depth < 3 && !item; ++depth, node = node->getParent()) {
        item = typeinfo_cast<CCMenuItem*>(node);
    }
    if (!item) return false;
    disabled = !item->isEnabled();
    return item->isSelected();
}

void GlassNode::visit() {
    auto& runtime = GlassRuntime::get();
    bool active = isVisible() && runtime.allows(m_scope) && ensurePlateProgram();
    if (active && m_rootDirty) resolveRoot();
    if (m_blocked) active = false;

    if (m_hostBound) {
        bool const attached = m_host && getParent() == m_host;
        if (active && (!attached ||
                       (m_hostType == kSpriteHost && m_shape == GlassShape::SpriteMask &&
                        !spriteMaskQuadReady(static_cast<CCSprite*>(m_host))) ||
                       !syncToHost())) {
            active = false;
        }
        m_artScratch.clear();
        if (active) collectArt(m_artScratch);
        syncArt(m_artScratch);
    }
    if (!active) return;

    bool disabled = false;
    bool const pressed = m_selected || hostPressed(disabled);
    m_disabled = disabled;
    float const target = pressed ? 1.f : 0.f;
    float const dt = sanitizeGlassFloat(CCDirector::get()->getDeltaTime(), 0.f, 0.f, 0.1f);
    float const rate = runtime.reducedMotion() ? 1.f : std::min(1.f, dt * 14.f);
    m_press += (target - m_press) * rate;

    CCNodeRGBA::visit();
}

void GlassNode::draw() {
    auto* plate = ensurePlateProgram();
    if (!plate) return;
    auto const size = getContentSize();
    if (!(size.width > 0.f) || !(size.height > 0.f)) return;
    float const opacity = std::clamp(hostOpacity(), 0.f, 1.f);
    if (opacity <= 0.004f) return;

    bool const spriteMode = m_hostBound && m_hostType == kSpriteHost &&
        m_shape == GlassShape::SpriteMask && m_host && getParent() == m_host;
    CCTexture2D* spriteTex = nullptr;
    ccV3F_C4B_T2F_Quad spriteQuad{};
    if (spriteMode) {
        auto* sprite = static_cast<CCSprite*>(m_host);
        if (!spriteMaskQuadReady(sprite)) return;
        spriteTex = sprite->getTexture();
        spriteQuad = sprite->getQuad();
    }

    // where this plate lands in the bound framebuffer (screen or render target).
    kmMat4 projection, modelview, mvp;
    kmGLGetMatrix(KM_GL_PROJECTION, &projection);
    kmGLGetMatrix(KM_GL_MODELVIEW, &modelview);
    kmMat4Multiply(&mvp, &projection, &modelview);
    GLint rawViewport[4] = {};
    glGetIntegerv(GL_VIEWPORT, rawViewport);
    GlassRect const viewport{rawViewport[0], rawViewport[1], rawViewport[2], rawViewport[3]};
    auto const axes = framebufferAxesFor(mvp.mat, viewport, size.width, size.height);
    if (!axes) return;
    float const ppu = pixelsPerUnit(*axes);
    if (ppu < 1e-3f) return;
    GlassRect const bounds = boundsOf(*axes, 0.f, 0.f, size.width, size.height);
    if (!bounds.intersects(viewport)) return;

    auto& runtime = GlassRuntime::get();
    auto const& s = runtime.settings();
    auto const& profile = runtime.profile();
    bool const panel = m_kind == GlassKind::Panel;
    bool const well = m_kind == GlassKind::Well;
    bool const chromeSprite = spriteMode && m_spriteStyle == GlassSpriteStyle::Chrome;
    bool const tabSprite = spriteMode && m_spriteStyle == GlassSpriteStyle::Tab;

    auto const win = CCDirector::get()->getWinSize();
    float const pxPerPoint = win.width > 0.f ? static_cast<float>(viewport.w) / win.width : ppu;
    float const unitsPerPoint = pxPerPoint / ppu;
    float const onePx = 1.f / ppu;
    float const halfMin = std::min(size.width, size.height) * 0.5f;

    // panels use the configured corner and wells sit concentric inside
    // them; buttons follow their semantic shape (capsule, circle, squircle or sprite contour).
    float radius = 0.f;
    float bevel = 0.f;
    if (panel) {
        radius = std::min(s.cornerRadius * unitsPerPoint, halfMin);
        bevel = std::clamp(radius * 0.9f, 3.f * onePx, std::max(3.f * onePx, halfMin * 0.5f));
    } else if (well) {
        radius = m_shape == GlassShape::Pill
            ? halfMin
            : std::min(s.cornerRadius * kWellCornerShare * unitsPerPoint, halfMin);
        bevel = onePx;
    } else if (spriteMode) {
        radius = halfMin * 0.6f;
        bevel = chromeSprite
            ? std::clamp(halfMin * 0.65f, 3.f * onePx, std::max(3.f * onePx, halfMin * 0.8f))
            : std::clamp(halfMin * 0.45f, 3.f * onePx, std::max(3.f * onePx, halfMin * 0.65f));
    } else {
        bool const pill = m_shape == GlassShape::Circle || m_shape == GlassShape::Pill ||
            (m_shape == GlassShape::Auto && (m_hostType == kSpriteHost || size.width >= size.height * 1.25f));
        radius = pill ? halfMin : halfMin * 0.58f;
        bevel = std::max(radius * 0.8f, 2.f * onePx);
    }
    // the bezel normal is only smooth inside the corner radius.
    if (!spriteMode) bevel = std::min(bevel, std::max(radius, onePx));
    float const thickness = well ? 0.f : s.refraction * bevel * ((panel || chromeSprite) ? 1.25f : 1.55f);
    float frost = frostFor(s.blur, panel || chromeSprite);
    float const dispersion = profile.dispersion && thickness > 0.f ? 1.f : 0.f;
    float const vibrancy = (panel || chromeSprite) ? 1.22f : 1.35f;

    float tintR = static_cast<float>(s.tintR) / 255.f;
    float tintG = static_cast<float>(s.tintG) / 255.f;
    float tintB = static_cast<float>(s.tintB) / 255.f;
    float tintAmount = s.tintOpacity;
    float brightness = 1.f;
    if (m_kind == GlassKind::Button && !chromeSprite) {
        if (s.coloredButtons && m_skin) {
            tintR = static_cast<float>(m_skin->r) / 255.f;
            tintG = static_cast<float>(m_skin->g) / 255.f;
            tintB = static_cast<float>(m_skin->b) / 255.f;
            tintAmount = (tabSprite ? 0.24f : 0.30f) + s.tintOpacity * 0.6f;
        }
        bool const rgbaHost = m_hostType == kNineSliceHost || m_hostType == kScale9Host ||
            m_hostType == kSpriteHost;
        if (m_hostBound && m_host && rgbaHost) {
            // buttonSprite::setColor greys disabled buttons and marks selections.
            auto const color = static_cast<CCNodeRGBA*>(m_host)->getColor();
            tintR *= static_cast<float>(color.r) / 255.f;
            tintG *= static_cast<float>(color.g) / 255.f;
            tintB *= static_cast<float>(color.b) / 255.f;
            brightness = std::max(0.35f,
                static_cast<float>(std::max({color.r, color.g, color.b})) / 255.f);
        }
        if (tabSprite && m_skin && !s.coloredButtons) brightness *= 1.12f;
        if (m_disabled) brightness *= 0.65f;
    }

    // fixed top-left light; on desktop it leans toward the cursor.
    float lightX = -0.55f;
    float lightY = 0.83f;
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
    if (profile.interactiveLight && !runtime.reducedMotion()) {
        auto const mouse = geode::cocos::getMousePos();
        auto const center = convertToWorldSpace(size * 0.5f);
        float const dx = mouse.x - center.x;
        float const dy = mouse.y - center.y;
        float const len = std::hypot(dx, dy);
        if (len > 1.f && std::isfinite(len)) {
            lightX = lightX * 0.35f + dx / len * 0.65f;
            lightY = lightY * 0.35f + dy / len * 0.65f;
        }
    }
#endif
    float const lightLen = std::max(1e-3f, std::hypot(lightX, lightY));
    lightX /= lightLen;
    lightY /= lightLen;

    // panels and buttons cast a soft shadow below; a well shades its own
    // inside, under the edges that face the light.
    float shadowOffset = (panel ? 2.5f : 1.2f) * unitsPerPoint;
    float shadowSoft = (panel ? 9.f : 4.f) * unitsPerPoint;
    float shadowStrength = panel ? 0.30f : 0.22f;
    float margin = shadowSoft + shadowOffset + 2.f * onePx;
    if (well) {
        shadowOffset = 0.f;
        shadowSoft = 5.f * unitsPerPoint;
        shadowStrength = 0.32f;
        margin = 2.f * onePx;
    }

    auto& capture = GlassCapture::get();
    std::optional<CaptureView> view;
    // a well draws from its uniforms alone and costs no copy.
    if (profile.live && !well) {
        GLint framebuffer = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        view = capture.acquire(m_root, capture.ordinalFor(m_root, bounds), bounds, framebuffer, viewport);
    }
    // the sharp copy was overwritten by a later one: blend the blur only.
    if (view && !view->sharpValid) frost = 1.f;

    float const mode = well ? 1.f : (spriteMode ? 2.f : 0.f);

    ccGLEnable(m_eGLServerState);
    plate->program->use();
    plate->program->setUniformsForBuiltins();
    glUniform2f(plate->size, size.width, size.height);
    glUniform4f(plate->shape, radius, bevel, ppu, mode);
    glUniform2f(plate->fbOrigin, axes->origin.x, axes->origin.y);
    glUniform2f(plate->fbAxisX, axes->axisX.x, axes->axisX.y);
    glUniform2f(plate->fbAxisY, axes->axisY.x, axes->axisY.y);
    if (view) {
        glUniform4fv(plate->sharpMap, 1, view->sharpMap);
        glUniform4fv(plate->sharpClamp, 1, view->sharpClamp);
        glUniform4fv(plate->blurMap, 1, view->blurMap);
        glUniform4fv(plate->blurClamp, 1, view->blurClamp);
    }
    glUniform4f(plate->tint, tintR, tintG, tintB, tintAmount);
    glUniform4f(plate->material, frost, thickness, dispersion, vibrancy);
    glUniform4f(plate->light, lightX, lightY, s.rim, s.rim * (panel ? 0.85f : 1.f));
    glUniform4f(plate->shadow, shadowOffset, shadowSoft, shadowStrength, static_cast<float>(m_glyph));
    glUniform4f(plate->state, m_press * 0.9f, view ? 0.f : 1.f, brightness, well ? wellFill() : 0.f);
    if (spriteMode && spriteTex) {
        float const ox = spriteQuad.bl.texCoords.u;
        float const oy = spriteQuad.bl.texCoords.v;
        float const ax = spriteQuad.br.texCoords.u - ox;
        float const ay = spriteQuad.br.texCoords.v - oy;
        float const bx = spriteQuad.tl.texCoords.u - ox;
        float const by = spriteQuad.tl.texCoords.v - oy;
        if (plate->spriteMap0 >= 0) glUniform4f(plate->spriteMap0, ox, oy, ax, ay);
        if (plate->spriteMap1 >= 0) glUniform4f(plate->spriteMap1, bx, by, 0.f, 0.f);
        if (plate->spriteOpts >= 0) {
            float const colored = (m_spriteStyle == GlassSpriteStyle::IconButton && s.coloredButtons) ? 1.f : 0.f;
            float const iconStr = m_spriteStyle == GlassSpriteStyle::Chrome ? 0.f : 1.f;
            float const premul = spriteTex->hasPremultipliedAlpha() ? 1.f : 0.f;
            float const styleId = static_cast<float>(m_spriteStyle);
            glUniform4f(plate->spriteOpts, colored, iconStr, premul, styleId);
        }
        bindUnit(2, spriteTex->getName());
    }
    if (view) {
        bindUnit(1, view->blurTex);
        bindUnit(0, view->sharpTex);
    }

    ccGLBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
#if CC_TEXTURE_ATLAS_USE_VAO
    ccGLBindVAO(0);
#endif
    ccGLEnableVertexAttribs(kCCVertexAttribFlag_PosColorTex);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    float const x0 = -margin;
    float const y0 = -margin;
    float const x1 = size.width + margin;
    float const y1 = size.height + margin;
    GLfloat const positions[] = {x0, y0, x1, y0, x0, y1, x1, y1};
    auto const alpha = static_cast<GLubyte>(std::lround(opacity * 255.f));
    GLubyte const colors[] = {alpha, alpha, alpha, alpha, alpha, alpha, alpha, alpha,
                              alpha, alpha, alpha, alpha, alpha, alpha, alpha, alpha};
    glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE, 0, positions);
    glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE, 0, positions);
    glVertexAttribPointer(kCCVertexAttrib_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, colors);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    CHECK_GL_ERROR_DEBUG();
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID)
    CC_INCREMENT_GL_DRAWS(1);
#endif

    if (panel) capture.notePanelDrawn(m_root, bounds);
}

void GlassNode::onGLContextReloadAll() {
    // runs while the old context is still current: delete now. A second
    // report of the same reload finds nothing left to delete.
    GlassCapture::get().releaseGL(true);
    plateProgram() = PlateProgram{};
    invalidateSkinCache();
}

} // namespace paimon::glass

// also covers texture reloads initiated without GameManager::reloadAll.
$on_game(TexturesUnloaded) {
    paimon::glass::GlassNode::onGLContextReloadAll();
}
