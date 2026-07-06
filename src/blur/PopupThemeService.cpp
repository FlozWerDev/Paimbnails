#include "PopupThemeService.hpp"

#include "PopupBlurService.hpp"
#include "../core/Settings.hpp"
#include "../utils/GLSLLoader.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/cocos/platform/CCGL.h>

#include <algorithm>
#include <vector>

using namespace geode::prelude;

using namespace cocos2d::extension;

namespace paimon::popuptheme {

namespace {

std::string themeDecorID() {
    return "paimon-popup-theme-decor"_spr;
}

CCScale9Sprite* findPopupBackground(CCNode* mainLayer) {
    if (!mainLayer) return nullptr;

    CCScale9Sprite* best = nullptr;
    float bestArea = 0.f;
    for (auto* child : CCArrayExt<CCNode*>(mainLayer->getChildren())) {
        auto* s9 = typeinfo_cast<CCScale9Sprite*>(child);
        if (!s9 || !s9->isVisible()) continue;
        auto sz = s9->getScaledContentSize();
        float area = sz.width * sz.height;
        if (area > bestArea) {
            bestArea = area;
            best = s9;
        }
    }
    if (best) return best;

    if (auto* byId = typeinfo_cast<CCScale9Sprite*>(mainLayer->getChildByID("background"))) {
        return byId;
    }
    for (auto* child : CCArrayExt<CCNode*>(mainLayer->getChildren())) {
        if (auto* s9 = typeinfo_cast<CCScale9Sprite*>(child)) {
            return s9;
        }
    }
    return nullptr;
}

class AuroraBorderSprite : public CCSprite {
public:
    float m_time = 0.f;
    CCGLProgram* m_cachedProgram = nullptr;
    GLint m_locTime = -2; // -2 = uninitialized, -1 = absent

    static AuroraBorderSprite* create(float width, float height) {
        auto* spr = new AuroraBorderSprite();

        unsigned char data[16];
        std::fill(std::begin(data), std::end(data), static_cast<unsigned char>(255));

        auto* tex = new CCTexture2D();
        if (tex && tex->initWithData(data, kCCTexture2DPixelFormat_RGBA8888, 2, 2, CCSizeMake(2.f, 2.f))) {
            ccTexParams params{GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
            tex->setTexParameters(&params);
            if (spr && spr->initWithTexture(tex)) {
                tex->release();
                spr->autorelease();
                spr->setTextureRect({0.f, 0.f, 2.f, 2.f});
                spr->setAnchorPoint({0.5f, 0.5f});
                if (width > 0.f) spr->setScaleX(width / 2.f);
                if (height > 0.f) spr->setScaleY(height / 2.f);
                spr->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});

                if (auto* prog = paimon::shaders::loadShader(
                        "paimon-aurora-border-v1",
                        "cell_vertex.glsl",
                        "aurora_border.glsl",
                        nullptr, nullptr)) {
                    spr->setShaderProgram(prog);
                }
                spr->scheduleUpdate();
                return spr;
            }
        }

        CC_SAFE_DELETE(tex);
        CC_SAFE_DELETE(spr);
        return nullptr;
    }

    void update(float dt) override {
        m_time += dt;
    }

    void draw() override {
        if (auto* prog = getShaderProgram()) {
            prog->use();
            prog->setUniformsForBuiltins();
            if (prog != m_cachedProgram) {
                m_cachedProgram = prog;
                m_locTime = prog->getUniformLocationForName("u_time");
            }
            if (m_locTime != -1) {
                prog->setUniformLocationWith1f(m_locTime, m_time);
            }
        }
        CCSprite::draw();
    }
};

void addBorderStrips(CCNode* decor, float w, float h, ThemeConfig const& theme) {
    float t = std::max(0.5f, theme.borderThickness);
    ccColor4B col = theme.borderColor;

    auto strip = [&](float sw, float sh, float x, float y) {
        auto* s = CCLayerColor::create(col, sw, sh);
        if (!s) return;
        s->ignoreAnchorPointForPosition(true);
        s->setPosition({x, y});
        decor->addChild(s);
    };

    strip(w, t, 0.f, h - t); // top
    strip(w, t, 0.f, 0.f);   // bottom
    strip(t, h, 0.f, 0.f);   // left
    strip(t, h, w - t, 0.f); // right
}

} // namespace

ThemeConfig getThemeConfig(std::string const& id) {
    ThemeConfig c;
    c.id = id;

    if (id == "glass") {
        c.forceBlur = true;
        c.bgOpacity = 165;
        c.hasBorders = true;
        c.borderThickness = 3.f;
        c.borderColor = {0, 0, 0, 255};
    } else if (id == "dark") {
        c.forceBlur = true;
        c.blurDarknessOverride = 0.55f;
        c.bgOpacity = 200;
        c.bgColor = {60, 60, 70};
        c.hasBorders = true;
        c.borderThickness = 1.5f;
        c.borderColor = {185, 185, 195, 255};
    } else if (id == "carbon") {
        c.forceBlur = true;
        c.blurDarknessOverride = 0.6f;
        c.bgOpacity = 210;
        c.bgColor = {30, 30, 34};
        c.hasBorders = true;
        c.borderThickness = 4.f;
        c.borderColor = {90, 90, 95, 255};
    } else if (id == "aurora") {
        c.forceBlur = true;
        c.bgOpacity = 180;
        c.animatedBorder = true;
    } else if (id == "minimal") {
        c.softShadow = true;
    }
    // "gd" and any unknown id => defaults (no-op theme).
    return c;
}

void applyTheme(FLAlertLayer* popup, bool blurAlreadyApplied) {
    if (!popup) return;

    auto theme = getThemeConfig(paimon::settings::popupblur::uiTheme());
    if (theme.id == "gd") return; // original look, nothing to do

    auto* mainLayer = popup->m_mainLayer;
    if (!mainLayer) return;
    if (theme.forceBlur && !blurAlreadyApplied) {
        auto cfg = paimon::popupblur::getConfig();
        cfg.enabled = true;
        cfg.style = "paiblur";
        if (theme.blurDarknessOverride >= 0.f) {
            cfg.darkness = theme.blurDarknessOverride;
        }
        paimon::popupblur::captureAndApplyWithConfig(popup, cfg);
    }
    cleanupTheme(popup);

    auto* bg = findPopupBackground(mainLayer);
    geode::log::info("[PopupTheme] theme='{}' bgFound={} blurAlready={}",
        theme.id, bg != nullptr, blurAlreadyApplied);
    if (bg) {
        if (theme.bgOpacity < 255) {
            bg->setOpacity(static_cast<GLubyte>(theme.bgOpacity));
        }
        if (theme.bgColor.r != 255 || theme.bgColor.g != 255 || theme.bgColor.b != 255) {
            bg->setColor(theme.bgColor);
        }
    }

    if (!bg) return;
    auto bounds = bg->boundingBox();
    float w = bounds.size.width;
    float h = bounds.size.height;
    if (w <= 0.f || h <= 0.f) return;

    int bgZ = bg->getZOrder();

    if (theme.softShadow) {
        if (auto* shadow = CCScale9Sprite::create("GJ_square01.png")) {
            shadow->setID(themeDecorID());
            shadow->setContentSize({w + 14.f, h + 14.f});
            shadow->setPosition({bounds.getMidX(), bounds.getMidY() - 3.f});
            shadow->setColor({0, 0, 0});
            shadow->setOpacity(70);
            mainLayer->addChild(shadow, bgZ - 1);
        }
    }

    if (theme.hasBorders || theme.animatedBorder) {
        constexpr int kDecorZ = 10000;
        auto* decor = CCNode::create();
        decor->setID(themeDecorID());
        decor->setAnchorPoint({0.f, 0.f});
        decor->setContentSize({w, h});
        decor->setPosition(bounds.origin);
        mainLayer->addChild(decor, kDecorZ);

        if (theme.animatedBorder) {
            if (auto* aurora = AuroraBorderSprite::create(w, h)) {
                aurora->setPosition({w / 2.f, h / 2.f});
                decor->addChild(aurora);
            }
        } else if (theme.hasBorders) {
            addBorderStrips(decor, w, h, theme);
        }
    }
}

void cleanupTheme(FLAlertLayer* popup) {
    if (!popup) return;
    auto* mainLayer = popup->m_mainLayer;
    if (!mainLayer) return;

    auto id = themeDecorID();
    std::vector<CCNode*> toRemove;
    for (auto* child : CCArrayExt<CCNode*>(mainLayer->getChildren())) {
        if (child && child->getID() == id) {
            toRemove.push_back(child);
        }
    }
    for (auto* node : toRemove) {
        node->removeFromParent();
    }
}

} // namespace paimon::popuptheme
