#include "AmbientNode.hpp"
#include "AmbientAnalyzer.hpp"
#include "AmbientDecorator.hpp"
#include "../../button-skins/services/ButtonSkinRuntime.hpp"
#include "../../../utils/GLSLLoader.hpp"

#include <Geode/cocos/menu_nodes/CCMenuItem.h>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/DialogLayer.hpp>
#include <Geode/binding/EditorUI.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/GJDropDownLayer.hpp>
#include <Geode/binding/MenuGameLayer.hpp>
#include <Geode/binding/MultilineBitmapFont.hpp>
#include <Geode/binding/ProfilePage.hpp>
#include <Geode/binding/TextArea.hpp>
#include <Geode/binding/UILayer.hpp>
#include <Geode/cocos/extensions/GUI/CCControlExtension/CCScale9Sprite.h>
#include <Geode/cocos/shaders/ccGLStateCache.h>
#include <Geode/loader/GameEvent.hpp>
#include <Geode/ui/Button.hpp>
#include <Geode/ui/NineSlice.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace geode::prelude;

namespace paimon::ambient {
namespace {

enum HostType : std::uint8_t {
    kNoHost = 0,
    kNineSliceHost = 1,
    kScale9Host = 2,
    kSpriteHost = 3,
    kColorLayerHost = 4,
};

GLuint& solidWhiteTextureSlot() {
    static GLuint s_tex = 0;
    return s_tex;
}

GLuint ensureSolidWhiteTexture() {
    GLuint& tex = solidWhiteTextureSlot();
    if (tex) return tex;
    static constexpr std::uint8_t kWhitePixel[4] = {255, 255, 255, 255};
    glGenTextures(1, &tex);
    if (!tex) return 0;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    ccGLBindTexture2DN(0, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, kWhitePixel);
    return tex;
}

constexpr ccBlendFunc kNoDraw{GL_ZERO, GL_ONE};

bool isNoDraw(ccBlendFunc const& blend) {
    return blend.src == kNoDraw.src && blend.dst == kNoDraw.dst;
}

struct AmbientProgram {
    Ref<CCGLProgram> program;
    bool failed = false;
    GLint origin = -1;
    GLint size = -1;
    GLint uvBounds = -1;
    GLint uvAxes = -1;
    GLint light = -1;
    GLint keyColor = -1;
    GLint localColor = -1;
    GLint shadowColor = -1;
    GLint palPrimary = -1;
    GLint palSecondary = -1;
    GLint palSurface = -1;
    GLint palAccent = -1;
    GLint params = -1;
    GLint mode = -1;
};

AmbientProgram& ambientProgram() {
    static AmbientProgram prog;
    return prog;
}

AmbientProgram* ensureAmbientProgram() {
    auto& p = ambientProgram();
    if (p.program) return &p;
    if (p.failed) return nullptr;
    auto* program = paimon::shaders::loadShader(
        "paimbnails/dynamic-ambient-ui-v3",
        "ambient_plate_vertex.glsl",
        "ambient_ui.glsl",
        nullptr,
        nullptr
    );
    if (!program) {
        p.failed = true;
        log::warn("[DynamicAmbient] ambient_ui shader unavailable");
        return nullptr;
    }
    GLuint const id = program->getProgram();
    auto loc = [id](char const* name) { return glGetUniformLocation(id, name); };
    p.origin = loc("u_origin");
    p.size = loc("u_size");
    p.uvBounds = loc("u_uvBounds");
    p.uvAxes = loc("u_uvAxes");
    p.light = loc("u_light");
    p.keyColor = loc("u_keyColor");
    p.localColor = loc("u_localColor");
    p.shadowColor = loc("u_shadowColor");
    p.palPrimary = loc("u_palPrimary");
    p.palSecondary = loc("u_palSecondary");
    p.palSurface = loc("u_palSurface");
    p.palAccent = loc("u_palAccent");
    p.params = loc("u_params");
    p.mode = loc("u_mode");
    program->use();
    glUniform1i(loc("u_texture"), 0);
    p.program = program;
    return &p;
}

void bindUnit0(GLuint texture) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    ccGLBindTexture2DN(0, texture);
}

std::uint8_t hostTypeOf(CCNode* host) {
    if (typeinfo_cast<NineSlice*>(host)) return kNineSliceHost;
    if (typeinfo_cast<extension::CCScale9Sprite*>(host)) return kScale9Host;
    if (auto* sprite = typeinfo_cast<CCSprite*>(host)) {
        return sprite->getBatchNode() ? kNoHost : kSpriteHost;
    }
    if (typeinfo_cast<CCLayerColor*>(host) && !typeinfo_cast<CCLayerGradient*>(host)) {
        return kColorLayerHost;
    }
    return kNoHost;
}

bool spriteQuadReady(CCSprite* sprite) {
    if (!sprite) return false;
    if (typeinfo_cast<TextArea*>(sprite) || typeinfo_cast<MultilineBitmapFont*>(sprite)) return false;
    auto* tex = sprite->getTexture();
    if (!tex || !tex->getName()) return false;
    if (tex->getPixelsWide() <= 4 || tex->getPixelsHigh() <= 4) return false;
    auto const quad = sprite->getQuad();
    float const qw = quad.tr.vertices.x - quad.bl.vertices.x;
    float const qh = quad.tr.vertices.y - quad.bl.vertices.y;
    return qw > 0.5f && qh > 0.5f && std::isfinite(qw) && std::isfinite(qh);
}

CCSpriteBatchNode* sliceBatchOf(CCNode* host, std::uint8_t hostType) {
    if (hostType == kNineSliceHost) return static_cast<NineSlice*>(host)->getBatchNode();
    if (hostType == kScale9Host) return static_cast<extension::CCScale9Sprite*>(host)->_scale9Image;
    return nullptr;
}

struct QuadVertex {
    GLfloat x, y;
    GLfloat u, v;
    GLubyte r, g, b, a;
};

void pushSpriteQuadsRecursive(
    CCNode* node,
    CCAffineTransform const& toHost,
    bool useDisplayedColor,
    std::vector<QuadVertex>& outVerts
) {
    if (!node || !node->isVisible()) return;
    CCAffineTransform const xform = CCAffineTransformConcat(node->nodeToParentTransform(), toHost);
    if (auto* spr = typeinfo_cast<CCSprite*>(node)) {
        auto const sz = spr->getContentSize();
        if (sz.width > 0.1f && sz.height > 0.1f && std::isfinite(sz.width) && std::isfinite(sz.height)) {
            auto const& q = spr->getQuad();
            ccColor3B const col = useDisplayedColor ? spr->getDisplayedColor() : ccColor3B{255, 255, 255};
            GLubyte const op = useDisplayedColor ? spr->getDisplayedOpacity() : static_cast<GLubyte>(255);
            if (op > 0) {
                auto emit = [&](float lx, float ly, cocos2d::ccTex2F const& uv) {
                    CCPoint const pt = CCPointApplyAffineTransform({lx, ly}, xform);
                    outVerts.push_back({
                        pt.x, pt.y,
                        uv.u, uv.v,
                        col.r, col.g, col.b, op
                    });
                };
                // Use local (0,0)..(w,h) transformed once rather than m_sQuad.vertices,
                // because CCSpriteBatchNode::updateTransform() bakes parent transforms
                // directly into m_sQuad.vertices while texCoords stay untouched.
                emit(0.f,      0.f,       q.bl.texCoords);
                emit(sz.width, 0.f,       q.br.texCoords);
                emit(sz.width, sz.height, q.tr.texCoords);
                emit(0.f,      0.f,       q.bl.texCoords);
                emit(sz.width, sz.height, q.tr.texCoords);
                emit(0.f,      sz.height, q.tl.texCoords);
            }
        }
    }
    if (auto* children = node->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            pushSpriteQuadsRecursive(child, xform, useDisplayedColor, outVerts);
        }
    }
}

} // namespace

AmbientNode* AmbientNode::createForHost(CCNode* host, AmbientKind kind, AmbientScope scope) {
    if (!host) return nullptr;
    auto const type = hostTypeOf(host);
    if (type == kNoHost) return nullptr;
    auto* node = new AmbientNode();
    node->m_hostType = type;
    if (!node->initPlate(host, kind, scope)) {
        delete node;
        return nullptr;
    }
    node->autorelease();
    return node;
}

bool AmbientNode::initPlate(CCNode* host, AmbientKind kind, AmbientScope scope) {
    auto size = host->getContentSize();
    size.width = std::max(1.f, std::abs(size.width));
    size.height = std::max(1.f, std::abs(size.height));
    if (!std::isfinite(size.width) || !std::isfinite(size.height)) return false;
    if (!CCNodeRGBA::init()) return false;
    m_host = host;
    m_kind = kind;
    m_scope = scope;
    // Always reveal immediately so newly entered layers/popups never flash vanilla colors.
    m_effectBlend = 1.f;
    this->setContentSize(size);
    this->setAnchorPoint({0.f, 0.f});
    this->setPosition({0.f, 0.f});
    this->setID("paimon-ambient-node"_spr);
    return true;
}

AmbientNode::~AmbientNode() {
    restoreArt();
}

void AmbientNode::onEnter() {
    CCNodeRGBA::onEnter();
    m_contextDirty = true;
}

void AmbientNode::onExit() {
    restoreArt();
    CCNodeRGBA::onExit();
}

void AmbientNode::resolveContext() {
    m_contextDirty = false;
    m_blocked = isNodeIgnoredForAmbient(m_host);
    m_inPopup = false;
    m_ownerScene = nullptr;
    m_ownerItem = {};

    bool excluded = false;
    for (CCNode* node = getParent(); node; node = node->getParent()) {
        if (!m_ownerItem && (typeinfo_cast<CCMenuItem*>(node) || typeinfo_cast<geode::Button*>(node))) {
            m_ownerItem = node;
        }
        if (!m_ownerScene) {
            if (auto* scene = typeinfo_cast<CCScene*>(node);
                scene && !typeinfo_cast<CCTransitionScene*>(scene)) {
                m_ownerScene = scene;
            }
        }
        if (typeinfo_cast<GJBaseGameLayer*>(node) || typeinfo_cast<MenuGameLayer*>(node) ||
            typeinfo_cast<UILayer*>(node) || typeinfo_cast<EditorUI*>(node) ||
            typeinfo_cast<ProfilePage*>(node)) {
            excluded = true;
        }
        if (typeinfo_cast<FLAlertLayer*>(node) || typeinfo_cast<GJDropDownLayer*>(node) ||
            typeinfo_cast<DialogLayer*>(node)) {
            m_inPopup = true;
        }
    }
    if (excluded) m_blocked = true;
}

bool AmbientNode::syncToHost() {
    if (!m_host || getParent() != m_host) return false;
    auto size = m_host->getContentSize();
    if (!(size.width > 0.f) || !(size.height > 0.f) ||
        !std::isfinite(size.width) || !std::isfinite(size.height)) {
        return false;
    }
    if (size != getContentSize()) setContentSize(size);
    if (!getPosition().equals({0.f, 0.f})) setPosition({0.f, 0.f});
    return true;
}

void AmbientNode::suppressArt() {
    if (!m_host || getParent() != m_host) return;
    if (m_hostType == kSpriteHost || m_hostType == kColorLayerHost) {
        auto const current = (m_hostType == kSpriteHost)
            ? static_cast<CCSprite*>(m_host)->getBlendFunc()
            : static_cast<CCLayerColor*>(m_host)->getBlendFunc();
        if (!isNoDraw(current)) {
            m_savedBlend = current;
            m_suppressedBlend = true;
            m_suppressed = true;
            if (m_hostType == kSpriteHost) static_cast<CCSprite*>(m_host)->setBlendFunc(kNoDraw);
            else static_cast<CCLayerColor*>(m_host)->setBlendFunc(kNoDraw);
        }
        return;
    }

    if (auto* batch = sliceBatchOf(m_host, m_hostType)) {
        if (batch->isVisible()) {
            batch->setVisible(false);
            m_suppressedBatch = batch;
            m_suppressedBlend = false;
            m_suppressed = true;
        }
    }
}

void AmbientNode::restoreArt() {
    if (!m_suppressed) return;
    m_suppressed = false;
    if (m_suppressedBlend) {
        if (m_host && getParent() == m_host) {
            if (m_hostType == kSpriteHost) {
                auto* spr = static_cast<CCSprite*>(m_host);
                if (isNoDraw(spr->getBlendFunc())) spr->setBlendFunc(m_savedBlend);
            } else if (m_hostType == kColorLayerHost) {
                auto* layer = static_cast<CCLayerColor*>(m_host);
                if (isNoDraw(layer->getBlendFunc())) layer->setBlendFunc(m_savedBlend);
            }
        }
        m_suppressedBlend = false;
        return;
    }
    if (auto batch = m_suppressedBatch.lock()) {
        batch->setVisible(true);
    }
    m_suppressedBatch = {};
}

float AmbientNode::hostOpacity() {
    if (!m_host || getParent() != m_host) return 0.f;
    auto* rgba = static_cast<CCNodeRGBA*>(m_host);
    float const own = static_cast<float>(rgba->getOpacity());
    if (own <= 0.f) return 0.f;
    return std::clamp(static_cast<float>(rgba->getDisplayedOpacity()) / 255.f, 0.f, 1.f);
}

bool AmbientNode::hostPressed(bool& disabled) const {
    disabled = false;
    if (m_kind != AmbientKind::Button || !m_host) return false;
    CCNode* node = m_host->getParent();
    for (int depth = 0; node && depth < 3; ++depth, node = node->getParent()) {
        if (auto* item = typeinfo_cast<CCMenuItem*>(node)) {
            disabled = !item->isEnabled();
            return item->isSelected();
        }
        if (auto* btn = typeinfo_cast<geode::Button*>(node)) {
            disabled = !btn->isEnabled();
            return btn->isSelected();
        }
    }
    return false;
}

void AmbientNode::visit() {
    auto& runtime = AmbientRuntime::get();
    bool active = isVisible() && runtime.allows(m_scope) && ensureAmbientProgram() != nullptr;
    if (active && m_contextDirty) resolveContext();
    if (m_blocked) active = false;

    if (active) {
        bool skinnedNow = false;
        if (auto item = m_ownerItem.lock()) {
            if (auto* menuItem = typeinfo_cast<CCMenuItem*>(item.data())) {
                skinnedNow = paimon::buttonskin::ButtonSkinRuntime::hasVisibleSkin(menuItem);
            }
        }
        if (!m_host || getParent() != m_host || skinnedNow) {
            active = false;
        } else if (m_hostType == kSpriteHost) {
            auto* spr = static_cast<CCSprite*>(m_host);
            auto const bf = spr->getBlendFunc();
            if ((!isNoDraw(bf) && bf.dst == GL_ONE && bf.src != GL_ZERO) || !spriteQuadReady(spr)) {
                active = false;
            }
        }
        if (active && !syncToHost()) {
            active = false;
        }
    }

    if (active) {
        suppressArt();
    } else {
        restoreArt();
        return;
    }

    bool disabled = false;
    bool const pressed = hostPressed(disabled);
    m_disabled = disabled;
    float const target = pressed ? 1.f : 0.f;
    float const dt = clampAmbientFloat(CCDirector::get()->getDeltaTime(), 0.f, 0.f, 0.1f);
    m_press += (target - m_press) * std::min(1.f, dt * 14.f);
    if (m_effectBlend < 1.f) {
        m_effectBlend = std::min(1.f, m_effectBlend + dt * 2.1f);
    }

    CCNodeRGBA::visit();
}

void AmbientNode::draw() {
    auto* prog = ensureAmbientProgram();
    if (!prog || !m_host || getParent() != m_host) return;
    if (!AmbientRuntime::get().allows(m_scope)) return;

    float const opacity = hostOpacity();
    if (opacity <= 0.004f) return;

    auto const size = getContentSize();
    if (!(size.width > 0.f) || !(size.height > 0.f)) return;

    auto* director = CCDirector::get();
    auto const win = director ? director->getWinSize() : CCSize{569.f, 320.f};

    // Sample here only as fallback if no backdrop probe exists in the current scene.
    if (!m_inPopup && !AmbientAnalyzer::get().hasSceneCapture(m_ownerScene)) {
        GLint fb = 0;
        GLint vp[4] = {};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
        glGetIntegerv(GL_VIEWPORT, vp);
        AmbientAnalyzer::get().maybeSampleFramebuffer(m_ownerScene, fb, vp[0], vp[1], vp[2], vp[3]);
    }

    auto const& lightState = AmbientAnalyzer::get().stateForScene(m_ownerScene);
    auto const& settings = AmbientRuntime::get().settings();

    // Node center in normalized screen UV [0..1].
    CCPoint const worldCenter = m_host->convertToWorldSpace(size * 0.5f);
    float const nodeU = win.width > 0.f ? std::clamp(worldCenter.x / win.width, 0.f, 1.f) : 0.5f;
    float const nodeV = win.height > 0.f ? std::clamp(worldCenter.y / win.height, 0.f, 1.f) : 0.5f;
    float const aspect = (win.height > 0.f) ? (win.width / win.height) : (16.f / 9.f);
    auto const nodeLight = computeNodeLightDir(lightState, nodeU, nodeV, aspect);
    auto const localCol = sampleLocalAmbientColor(lightState, nodeU, nodeV);
    float const animTime = AmbientAnalyzer::get().timeSeconds();

    // Transform screen-space light vector into host-local coordinates (handles flipX/rotation/scale).
    auto const toWorld = m_host->nodeToWorldTransform();
    float const ax = toWorld.a;
    float const ay = toWorld.b;
    float const bx = toWorld.c;
    float const by = toWorld.d;
    float const lenA = std::max(1e-4f, std::hypot(ax, ay));
    float const lenB = std::max(1e-4f, std::hypot(bx, by));
    float localLightX = (nodeLight.dirX * ax + nodeLight.dirY * ay) / lenA;
    float localLightY = (nodeLight.dirX * bx + nodeLight.dirY * by) / lenB;
    float const localLen = std::max(1e-4f, std::hypot(localLightX, localLightY));
    localLightX /= localLen;
    localLightY /= localLen;

    bool const isPanel = (m_kind == AmbientKind::Panel);
    bool const isOverlay = (m_kind == AmbientKind::ButtonOverlay);
    bool const isAsset = (m_kind == AmbientKind::Asset);
    float const kindCode = isPanel ? 1.f : (isAsset ? 0.5f : (isOverlay ? 0.25f : 0.f));
    bool const isNineSlice = (m_hostType == kNineSliceHost || m_hostType == kScale9Host);

    ccGLEnable(m_eGLServerState);
    prog->program->use();
    prog->program->setUniformsForBuiltins();
    ccGLBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
#if CC_TEXTURE_ATLAS_USE_VAO
    ccGLBindVAO(0);
#endif
    ccGLEnableVertexAttribs(kCCVertexAttribFlag_PosColorTex);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    float const disabledFactor = m_disabled ? 0.65f : 1.0f;
    float const reveal = m_effectBlend * m_effectBlend * (3.f - 2.f * m_effectBlend);

    auto setCommonUniforms = [&](float passMode, float premul, float nineFlag,
                                 float origX, float origY, float sizeW, float sizeH,
                                 float uMin, float vMin, float uMax, float vMax,
                                 float axU, float axV, float bxU, float byV) {
        if (prog->origin >= 0) glUniform2f(prog->origin, origX, origY);
        if (prog->size >= 0) glUniform2f(prog->size, sizeW, sizeH);
        if (prog->uvBounds >= 0) glUniform4f(prog->uvBounds, uMin, vMin, uMax, vMax);
        if (prog->uvAxes >= 0) glUniform4f(prog->uvAxes, axU, axV, bxU, byV);
        if (prog->light >= 0) {
            glUniform4f(prog->light, localLightX, localLightY,
                        lightState.directionality, nodeLight.proximityBoost);
        }
        if (prog->keyColor >= 0) {
            glUniform4f(prog->keyColor, lightState.keyColor.r, lightState.keyColor.g,
                        lightState.keyColor.b, settings.intensity * reveal);
        }
        if (prog->localColor >= 0) {
            glUniform4f(prog->localColor, localCol.r, localCol.g, localCol.b, animTime);
        }
        if (prog->shadowColor >= 0) {
            float const shAlpha = settings.shadowStrength * reveal *
                (isPanel ? 0.20f : (isOverlay ? 0.f : (isAsset ? 0.24f : 0.32f)));
            glUniform4f(prog->shadowColor, lightState.shadowColor.r, lightState.shadowColor.g,
                        lightState.shadowColor.b, shAlpha);
        }
        if (prog->palPrimary >= 0) {
            glUniform4f(prog->palPrimary, lightState.palettePrimary.r, lightState.palettePrimary.g,
                        lightState.palettePrimary.b, settings.paletteStrength * reveal);
        }
        if (prog->palSecondary >= 0) {
            glUniform3f(prog->palSecondary, lightState.paletteSecondary.r, lightState.paletteSecondary.g,
                        lightState.paletteSecondary.b);
        }
        if (prog->palSurface >= 0) {
            glUniform3f(prog->palSurface, lightState.paletteSurface.r, lightState.paletteSurface.g,
                        lightState.paletteSurface.b);
        }
        if (prog->palAccent >= 0) {
            glUniform3f(prog->palAccent, lightState.paletteAccent.r, lightState.paletteAccent.g,
                        lightState.paletteAccent.b);
        }
        if (prog->params >= 0) {
            glUniform4f(prog->params, settings.tintStrength * reveal, settings.rimStrength * reveal,
                        kindCode, passMode);
        }
        if (prog->mode >= 0) {
            glUniform4f(prog->mode, premul, nineFlag, m_press * 0.9f, disabledFactor);
        }
    };

    if (m_hostType == kColorLayerHost) {
        GLuint const whiteTex = ensureSolidWhiteTexture();
        if (!whiteTex) return;
        bindUnit0(whiteTex);
        auto* layer = static_cast<CCLayerColor*>(m_host);
        auto const col = layer->getDisplayedColor();
        auto const alphaByte = static_cast<GLubyte>(std::lround(opacity * 255.f));
        GLfloat const pos[] = {
            0.f, 0.f,
            size.width, 0.f,
            0.f, size.height,
            size.width, size.height
        };
        GLfloat const uv[] = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 1.f, 1.f};
        GLubyte const colors[] = {
            col.r, col.g, col.b, alphaByte,
            col.r, col.g, col.b, alphaByte,
            col.r, col.g, col.b, alphaByte,
            col.r, col.g, col.b, alphaByte
        };
        setCommonUniforms(0.f, 0.f, 1.f, 0.f, 0.f, size.width, size.height,
                          0.f, 0.f, 1.f, 1.f, 1.f, 0.f, 0.f, 1.f);
        glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE, 0, pos);
        glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE, 0, uv);
        glVertexAttribPointer(kCCVertexAttrib_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, colors);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        CHECK_GL_ERROR_DEBUG();
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID)
        CC_INCREMENT_GL_DRAWS(1);
#endif
        return;
    }

    if (!isNineSlice) {
        auto* spr = static_cast<CCSprite*>(m_host);
        auto* tex = spr->getTexture();
        if (!tex || !tex->getName()) return;
        auto const q = spr->getQuad();

        float const ox = q.bl.texCoords.u;
        float const oy = q.bl.texCoords.v;
        float const axU = q.br.texCoords.u - ox;
        float const axV = q.br.texCoords.v - oy;
        float const bxU = q.tl.texCoords.u - ox;
        float const byV = q.tl.texCoords.v - oy;

        float const uMin = std::min({q.bl.texCoords.u, q.br.texCoords.u, q.tl.texCoords.u, q.tr.texCoords.u});
        float const vMin = std::min({q.bl.texCoords.v, q.br.texCoords.v, q.tl.texCoords.v, q.tr.texCoords.v});
        float const uMax = std::max({q.bl.texCoords.u, q.br.texCoords.u, q.tl.texCoords.u, q.tr.texCoords.u});
        float const vMax = std::max({q.bl.texCoords.v, q.br.texCoords.v, q.tl.texCoords.v, q.tr.texCoords.v});

        float const x0 = q.bl.vertices.x;
        float const y0 = q.bl.vertices.y;
        float const qw = std::max(1.f, q.tr.vertices.x - x0);
        float const qh = std::max(1.f, q.tr.vertices.y - y0);
        float const premul = tex->hasPremultipliedAlpha() ? 1.f : 0.f;

        auto const col = spr->getDisplayedColor();
        auto const alphaByte = static_cast<GLubyte>(std::lround(opacity * 255.f));
        bindUnit0(tex->getName());

        // Pass 1: directional drop shadow cast away from the light source.
        // Skip Pass 1 on small overlay assets or sprites parented inside another sprite/button
        // so badges and nested icons never paint muddy halos onto parent buttons.
        auto* hostParent = m_host->getParent();
        bool const nestedInSpriteOrButton = hostParent &&
            (typeinfo_cast<CCSprite*>(hostParent) != nullptr ||
             typeinfo_cast<ButtonSprite*>(hostParent) != nullptr);
        bool const castSpriteShadow = settings.shadowStrength > 0.01f &&
            m_kind != AmbientKind::ButtonOverlay &&
            (m_kind != AmbientKind::Asset || (qw >= 20.f && qh >= 20.f && !nestedInSpriteOrButton));
        if (castSpriteShadow) {
            float const shDist = std::clamp(std::min(qw, qh) * 0.08f, 2.0f, 7.5f) *
                (0.55f + 0.45f * lightState.directionality);
            float const shDx = -localLightX * shDist;
            float const shDy = -localLightY * shDist;
            float const pad = 2.5f;
            float const pu = pad / qw;
            float const pv = pad / qh;

            float const sx0 = x0 + shDx - pad;
            float const sy0 = y0 + shDy - pad;
            float const sx1 = x0 + qw + shDx + pad;
            float const sy1 = y0 + qh + shDy + pad;

            auto uvAt = [&](float fx, float fy) -> std::pair<GLfloat, GLfloat> {
                return {ox + fx * axU + fy * bxU, oy + fx * axV + fy * byV};
            };
            auto [uBL, vBL] = uvAt(-pu, -pv);
            auto [uBR, vBR] = uvAt(1.f + pu, -pv);
            auto [uTL, vTL] = uvAt(-pu, 1.f + pv);
            auto [uTR, vTR] = uvAt(1.f + pu, 1.f + pv);

            GLfloat const shPos[] = {sx0, sy0, sx1, sy0, sx0, sy1, sx1, sy1};
            GLfloat const shUv[]  = {uBL, vBL, uBR, vBR, uTL, vTL, uTR, vTR};
            GLubyte const shCol[] = {
                255, 255, 255, alphaByte,
                255, 255, 255, alphaByte,
                255, 255, 255, alphaByte,
                255, 255, 255, alphaByte
            };
            setCommonUniforms(1.f, premul, 0.f, sx0, sy0, qw + pad * 2.f, qh + pad * 2.f,
                              uMin, vMin, uMax, vMax, axU, axV, bxU, byV);
            glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE, 0, shPos);
            glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE, 0, shUv);
            glVertexAttribPointer(kCCVertexAttrib_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, shCol);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }

        // Pass 0: lit original sprite.
        GLfloat const pos[] = {
            q.bl.vertices.x, q.bl.vertices.y,
            q.br.vertices.x, q.br.vertices.y,
            q.tl.vertices.x, q.tl.vertices.y,
            q.tr.vertices.x, q.tr.vertices.y
        };
        GLfloat const uv[] = {
            q.bl.texCoords.u, q.bl.texCoords.v,
            q.br.texCoords.u, q.br.texCoords.v,
            q.tl.texCoords.u, q.tl.texCoords.v,
            q.tr.texCoords.u, q.tr.texCoords.v
        };
        GLubyte const colors[] = {
            col.r, col.g, col.b, alphaByte,
            col.r, col.g, col.b, alphaByte,
            col.r, col.g, col.b, alphaByte,
            col.r, col.g, col.b, alphaByte
        };
        setCommonUniforms(0.f, premul, 0.f, x0, y0, qw, qh,
                          uMin, vMin, uMax, vMax, axU, axV, bxU, byV);
        glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE, 0, pos);
        glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE, 0, uv);
        glVertexAttribPointer(kCCVertexAttrib_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, colors);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        CHECK_GL_ERROR_DEBUG();
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID)
        CC_INCREMENT_GL_DRAWS(1);
#endif
        return;
    }

    // 9-slice host (NineSlice / CCScale9Sprite).
    auto* batch = sliceBatchOf(m_host, m_hostType);
    if (!batch) return;
    auto* tex = batch->getTexture();
    if (!tex || !tex->getName()) return;
    float const premul = tex->hasPremultipliedAlpha() ? 1.f : 0.f;
    bindUnit0(tex->getName());

    auto const alphaByte = static_cast<GLubyte>(std::lround(opacity * 255.f));

    // Pass 1: directional rounded-box drop shadow behind solid 9-slice buttons/panels.
    if (settings.shadowStrength > 0.01f && opacity >= 0.50f && m_kind != AmbientKind::ButtonOverlay) {
        float const shDist = (isPanel ? 3.5f : 3.0f) * (0.55f + 0.45f * lightState.directionality);
        float const shDx = -localLightX * shDist;
        float const shDy = -localLightY * shDist;
        float const pad = isPanel ? 8.f : 5.f;
        float const sx0 = -pad + shDx;
        float const sy0 = -pad + shDy;
        float const sx1 = size.width + pad + shDx;
        float const sy1 = size.height + pad + shDy;
        GLfloat const shPos[] = {sx0, sy0, sx1, sy0, sx0, sy1, sx1, sy1};
        GLubyte const shCol[] = {
            255, 255, 255, alphaByte,
            255, 255, 255, alphaByte,
            255, 255, 255, alphaByte,
            255, 255, 255, alphaByte
        };
        setCommonUniforms(1.f, premul, 1.f, shDx, shDy, size.width, size.height,
                          0.f, 0.f, 1.f, 1.f, 1.f, 0.f, 0.f, 1.f);
        glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE, 0, shPos);
        glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE, 0, shPos);
        glVertexAttribPointer(kCCVertexAttrib_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, shCol);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    // Pass 0: lit 9-slice quads in one batch draw.
    static std::vector<QuadVertex> s_verts;
    s_verts.clear();
    auto* hostRgba = static_cast<CCNodeRGBA*>(m_host);
    bool const useChildDisplayed = (m_hostType == kNineSliceHost);
    CCAffineTransform const batchXform = batch->nodeToParentTransform();
    if (auto* children = batch->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            pushSpriteQuadsRecursive(child, batchXform, useChildDisplayed, s_verts);
        }
    }
    if (s_verts.empty()) return;

    // CCScale9Sprite copies own color/opacity onto _scale9Image sprites only on direct setters,
    // not parent cascades, so apply hostRgba's single displayedColor/Opacity cleanly once.
    if (m_hostType == kScale9Host) {
        auto const col = hostRgba->getDisplayedColor();
        for (auto& v : s_verts) {
            v.r = col.r;
            v.g = col.g;
            v.b = col.b;
            v.a = alphaByte;
        }
    }

    setCommonUniforms(0.f, premul, 1.f, 0.f, 0.f, size.width, size.height,
                      0.f, 0.f, 1.f, 1.f, 1.f, 0.f, 0.f, 1.f);
    glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), &s_verts[0].x);
    glVertexAttribPointer(kCCVertexAttrib_TexCoords, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), &s_verts[0].u);
    glVertexAttribPointer(kCCVertexAttrib_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(QuadVertex), &s_verts[0].r);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(s_verts.size()));
    CHECK_GL_ERROR_DEBUG();
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_ANDROID)
    CC_INCREMENT_GL_DRAWS(1);
#endif
}

void AmbientNode::onGLContextReloadAll(bool deleteHandles) {
    AmbientAnalyzer::get().releaseGL(deleteHandles);
    ambientProgram() = AmbientProgram{};
    if (solidWhiteTextureSlot()) {
        if (deleteHandles) {
            glDeleteTextures(1, &solidWhiteTextureSlot());
        }
        solidWhiteTextureSlot() = 0;
    }
}

} // namespace paimon::ambient

$on_game(TexturesUnloaded) {
    paimon::ambient::AmbientNode::onGLContextReloadAll();
}
