#include "CustomProgressBarNode.hpp"
#include "../../colorful-icons/services/IconColorMath.hpp"
#include "../../../utils/PaimonDrawNode.hpp"

#include <fmt/format.h>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace geode::prelude;
using namespace cocos2d;

namespace {

using paimon::icons::math::fromHSV;

ccColor3B lerpColor3(ccColor3B const& a, ccColor3B const& b, float t) {
    t = std::clamp(t, 0.f, 1.f);
    return {
        static_cast<GLubyte>(std::round(a.r + (b.r - a.r) * t)),
        static_cast<GLubyte>(std::round(a.g + (b.g - a.g) * t)),
        static_cast<GLubyte>(std::round(a.b + (b.b - a.b) * t)),
    };
}

ccColor4F toColor4F(ccColor3B c, float alpha) {
    return {c.r / 255.f, c.g / 255.f, c.b / 255.f, std::clamp(alpha, 0.f, 1.f)};
}

ccColor3B evalColorMode(
    BarColorMode mode,
    ccColor3B c1,
    ccColor3B c2,
    ccColor3B c3,
    float animTime,
    float speed,
    float frac,
    float xNorm
) {
    switch (mode) {
        case BarColorMode::Pulse: {
            float t = 0.5f + 0.5f * std::sin(animTime * speed * 2.1f * static_cast<float>(M_PI));
            return lerpColor3(c1, c2, t);
        }
        case BarColorMode::Rainbow: {
            float hue = std::fmod((animTime * speed * 0.22f + xNorm * 0.45f) * 360.f, 360.f);
            if (hue < 0.f) hue += 360.f;
            return fromHSV({hue, 0.92f, 1.f});
        }
        case BarColorMode::Gradient2: {
            return lerpColor3(c1, c2, xNorm);
        }
        case BarColorMode::Gradient3: {
            if (xNorm < 0.5f) return lerpColor3(c1, c2, xNorm * 2.f);
            return lerpColor3(c2, c3, (xNorm - 0.5f) * 2.f);
        }
        case BarColorMode::ProgressReactive: {
            float p = std::clamp(frac, 0.f, 1.f);
            if (p < 0.5f) return lerpColor3(c1, c2, p * 2.f);
            return lerpColor3(c2, c3, (p - 0.5f) * 2.f);
        }
        case BarColorMode::Solid:
        default:
            return c1;
    }
}

void buildRoundedRectPoly(
    float x, float y, float w, float h, float r,
    std::vector<CCPoint>& out, int arcSegments = 6
) {
    out.clear();
    if (w <= 0.1f || h <= 0.1f) return;
    r = std::clamp(r, 0.f, std::min(w, h) * 0.495f);
    if (r < 0.4f) {
        out.push_back(ccp(x, y));
        out.push_back(ccp(x + w, y));
        out.push_back(ccp(x + w, y + h));
        out.push_back(ccp(x, y + h));
        return;
    }

    auto appendArc = [&](float cx, float cy, float a0, float a1) {
        for (int i = 0; i <= arcSegments; ++i) {
            float a = a0 + (a1 - a0) * (static_cast<float>(i) / arcSegments);
            out.push_back(ccp(cx + std::cos(a) * r, cy + std::sin(a) * r));
        }
    };
    float const pi = static_cast<float>(M_PI);
    appendArc(x + w - r, y + r,     -pi * 0.5f, 0.f);
    appendArc(x + w - r, y + h - r,  0.f,       pi * 0.5f);
    appendArc(x + r,     y + h - r,  pi * 0.5f, pi);
    appendArc(x + r,     y + r,      pi,        pi * 1.5f);
}

void drawRoundedFilled(
    CCDrawNode* dn, float x, float y, float w, float h, float r,
    ccColor4F const& fill, ccColor4F const& border, float borderW
) {
    if (!dn || w <= 0.2f || h <= 0.2f) return;
    std::vector<CCPoint> poly;
    buildRoundedRectPoly(x, y, w, h, r, poly);
    if (poly.size() >= 3) {
        dn->drawPolygon(poly.data(), static_cast<unsigned int>(poly.size()), fill, borderW, border);
    }
}

std::string formatDurationTime(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    int totalMs = static_cast<int>(seconds * 100.0 + 0.5);
    int cs = totalMs % 100;
    int totalSec = totalMs / 100;
    int sec = totalSec % 60;
    int min = totalSec / 60;
    return fmt::format("{}:{:02d}.{:02d}", min, sec, cs);
}

} // namespace

CustomProgressBarNode* CustomProgressBarNode::create() {
    auto* ret = new CustomProgressBarNode();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool CustomProgressBarNode::init() {
    if (!CCNode::init()) return false;
    this->setID("paimon-custom-progressbar"_spr);
    this->setAnchorPoint({0.5f, 0.5f});
    this->setContentSize({220.f, 12.f});

    m_shadowDraw = PaimonDrawNode::create();
    m_trackDraw  = PaimonDrawNode::create();
    m_fillDraw   = PaimonDrawNode::create();
    m_markerDraw = PaimonDrawNode::create();
    m_borderDraw = PaimonDrawNode::create();

    this->addChild(m_shadowDraw, 0);
    this->addChild(m_trackDraw, 1);

    m_bgStencil = CCDrawNode::create();
    m_bgClip = CCClippingNode::create(m_bgStencil);
    m_bgClip->setAlphaThreshold(0.05f);
    m_bgClip->setVisible(false);
    this->addChild(m_bgClip, 2);

    this->addChild(m_fillDraw, 3);

    char const* fillFile = "sliderBar2.png";
    m_gdFill = CCSprite::create(fillFile);
    if (!m_gdFill || !m_gdFill->getTexture()) {
        fillFile = "sliderBar.png";
        m_gdFill = CCSprite::create(fillFile);
    }
    if (m_gdFill && m_gdFill->getTexture()) {
        ccTexParams tp{GL_LINEAR, GL_LINEAR, GL_REPEAT, GL_CLAMP_TO_EDGE};
        m_gdFill->getTexture()->setTexParameters(&tp);
        m_gdFill->setAnchorPoint({0.f, 0.5f});
        m_gdFill->setVisible(false);
        this->addChild(m_gdFill, 3);
    } else {
        m_gdFill = nullptr;
    }

    m_fillStencil = CCDrawNode::create();
    m_fillClip = CCClippingNode::create(m_fillStencil);
    m_fillClip->setAlphaThreshold(0.05f);
    m_fillClip->setVisible(false);
    this->addChild(m_fillClip, 4);

    bool usingGroove2 = true;
    char const* grooveFile = "slidergroove2.png";
    auto* grooveTex = CCTextureCache::sharedTextureCache()->addImage(grooveFile, false);
    if (!grooveTex) {
        usingGroove2 = false;
        grooveFile = "slidergroove_02.png";
        grooveTex = CCTextureCache::sharedTextureCache()->addImage(grooveFile, false);
    }
    if (grooveTex) {
        auto sz = grooveTex->getContentSize();
        if (sz.width > 4.f && sz.height > 4.f) {
            m_gdFrameTexH = sz.height;
            m_gdFrameVisualH = usingGroove2 ? (sz.height * 0.75f) : sz.height;
            m_gdFrameCapX = std::min(10.f, sz.width * 0.25f);
            float const capY = std::min(4.f, sz.height * 0.30f);
            m_gdFrame = CCScale9Sprite::create(
                grooveFile,
                CCRectMake(0.f, 0.f, sz.width, sz.height),
                CCRectMake(m_gdFrameCapX, capY, std::max(1.f, sz.width - m_gdFrameCapX * 2.f), std::max(1.f, sz.height - capY * 2.f))
            );
            if (m_gdFrame) {
                m_gdFrame->setAnchorPoint({0.5f, 0.5f});
                m_gdFrame->setVisible(false);
                this->addChild(m_gdFrame, 5);
            }
        }
    }

    this->addChild(m_markerDraw, 6);
    this->addChild(m_borderDraw, 6);

    m_label = CCLabelBMFont::create("0%", "bigFont.fnt");
    m_label->setID("paimon-custom-progressbar-label"_spr);
    this->addChild(m_label, 7);

    return true;
}

CCRect CustomProgressBarNode::getTrackLocalRect() const {
    return {0.f, 0.f, m_cachedW, m_cachedH};
}

void CustomProgressBarNode::renderState(
    ProgressBarConfig const& cfg,
    ProgressBarTelemetry const& tel,
    float animTime,
    CCTexture2D* fillTex,
    CCTexture2D* bgTex
) {
    float const w = std::max(36.f, cfg.barWidth);
    float const h = std::max(4.f, cfg.barHeight);
    m_cachedW = w;
    m_cachedH = h;
    this->setContentSize({w, h});

    float const alpha = std::clamp(cfg.opacity / 255.f, 0.f, 1.f);
    float const frac = std::clamp(tel.smoothedPercent / 100.f, 0.f, 1.f);
    float const startFrac = (tel.startPosPercent > 0.5f && tel.startPosPercent < tel.smoothedPercent)
        ? std::clamp(tel.startPosPercent / 100.f, 0.f, frac)
        : 0.f;

    float radius = cfg.cornerRadius;
    bool const isNativeGD = (cfg.style == BarStyle::VanillaGD);
    if (isNativeGD) {
        radius = h * 0.48f;
    } else if (cfg.style == BarStyle::Classic || cfg.style == BarStyle::Segmented) {
        radius = std::min(radius, 2.5f);
    } else if (cfg.style == BarStyle::Minimal) {
        radius = std::min(radius, h * 0.45f);
    }
    radius = std::clamp(radius, 0.f, std::min(w, h) * 0.495f);

    if (m_gdFrame) {
        m_gdFrame->setVisible(isNativeGD);
        if (isNativeGD) {
            float const scaleY = std::max(0.2f, h / std::max(1.f, m_gdFrameVisualH));
            float const contentW = std::max(m_gdFrameCapX * 2.f + 2.f, w / scaleY);
            m_gdFrame->setContentSize({contentW, m_gdFrameTexH});
            m_gdFrame->setScaleX(w / contentW);
            m_gdFrame->setScaleY(scaleY);
            m_gdFrame->setPosition({w * 0.5f, h * 0.5f});
            m_gdFrame->setColor(cfg.useCustomBgColor ? cfg.borderColor : ccColor3B{255, 255, 255});
            m_gdFrame->setOpacity(static_cast<GLubyte>(alpha * 255.f));
        }
    }

    m_shadowDraw->clear();
    m_trackDraw->clear();
    m_fillDraw->clear();
    m_markerDraw->clear();
    m_borderDraw->clear();

    ccColor4F const noBorder = {0.f, 0.f, 0.f, 0.f};

    // 1. Drop shadow & Neon outer bloom
    if (cfg.shadowEnabled) {
        drawRoundedFilled(m_shadowDraw, 1.5f, -2.5f, w, h, radius,
            {0.f, 0.f, 0.f, 0.38f * alpha}, noBorder, 0.f);
    }
    if (cfg.style == BarStyle::NeonGlow && frac > 0.005f) {
        ccColor3B glowCol = evalColorMode(
            cfg.fillColorMode, cfg.fillColor, cfg.fillColor2, cfg.fillColor3,
            animTime, cfg.colorAnimSpeed, frac, frac * 0.5f);
        float glowW = std::max(h * 0.9f, w * frac);
        drawRoundedFilled(m_shadowDraw, -3.f, -3.f, glowW + 6.f, h + 6.f, radius + 3.f,
            toColor4F(glowCol, 0.20f * alpha), noBorder, 0.f);
        drawRoundedFilled(m_shadowDraw, -1.5f, -1.5f, glowW + 3.f, h + 3.f, radius + 1.5f,
            toColor4F(glowCol, 0.28f * alpha), noBorder, 0.f);
    }

    // 2. Track background
    ccColor3B trackCol = cfg.useCustomBgColor
        ? evalColorMode(cfg.bgColorMode, cfg.bgColor, cfg.bgColor2, cfg.bgColor2,
                        animTime, cfg.colorAnimSpeed, frac, 0.5f)
        : (isNativeGD ? ccc3(0, 0, 0) : ccc3(18, 22, 34));
    float trackAlpha = isNativeGD ? 0.42f :
                       (cfg.style == BarStyle::Glass ? 0.55f :
                       (cfg.style == BarStyle::Minimal ? 0.45f : 0.88f));
    if (isNativeGD) {
        float const tPad = std::max(1.f, h * 0.16f);
        float const tw = std::max(1.f, w - tPad * 2.f);
        float const th = std::max(1.f, h - tPad * 2.f);
        drawRoundedFilled(m_trackDraw, tPad, tPad, tw, th, std::max(0.f, th * 0.45f),
            toColor4F(trackCol, trackAlpha * alpha), noBorder, 0.f);
    } else {
        drawRoundedFilled(m_trackDraw, 0.f, 0.f, w, h, radius,
            toColor4F(trackCol, trackAlpha * alpha), noBorder, 0.f);
    }

    if (cfg.style == BarStyle::Glass) {
        drawRoundedFilled(m_trackDraw, 1.5f, h * 0.52f, std::max(1.f, w - 3.f), h * 0.36f,
            std::max(0.f, radius - 1.5f), {1.f, 1.f, 1.f, 0.14f * alpha}, noBorder, 0.f);
    }

    // 2b. Optional custom background texture
    if (cfg.useBgTexture && bgTex) {
        m_bgStencil->clear();
        std::vector<CCPoint> poly;
        buildRoundedRectPoly(0.f, 0.f, w, h, radius, poly);
        if (poly.size() >= 3) {
            m_bgStencil->drawPolygon(poly.data(), static_cast<unsigned int>(poly.size()),
                {1, 1, 1, 1}, 0.f, {1, 1, 1, 1});
        }
        if (!m_bgTexSprite || m_bgTexSprite->getTexture() != bgTex) {
            if (m_bgTexSprite) m_bgTexSprite->removeFromParent();
            m_bgTexSprite = CCSprite::createWithTexture(bgTex);
            if (m_bgTexSprite) m_bgClip->addChild(m_bgTexSprite);
        }
        if (m_bgTexSprite) {
            auto ts = m_bgTexSprite->getContentSize();
            m_bgTexSprite->setPosition({w * 0.5f, h * 0.5f});
            m_bgTexSprite->setScaleX(w / std::max(1.f, ts.width));
            m_bgTexSprite->setScaleY(h / std::max(1.f, ts.height));
            m_bgTexSprite->setColor(trackCol);
            m_bgTexSprite->setOpacity(static_cast<GLubyte>(alpha * 255.f));
            m_bgClip->setVisible(true);
        }
    } else {
        m_bgClip->setVisible(false);
    }

    // 3. Fill layer
    if (m_gdFill) m_gdFill->setVisible(false);
    float const pad = isNativeGD ? std::max(1.2f, h * 0.17f)
                                 : ((cfg.style == BarStyle::Minimal) ? 0.f : std::min(1.5f, h * 0.16f));
    float const innerW = std::max(1.f, w - pad * 2.f);
    float const innerH = std::max(1.f, h - pad * 2.f);
    float const innerR = isNativeGD ? (innerH * 0.45f) : std::max(0.f, radius - pad);
    float const fillStartX = pad + innerW * startFrac;
    float const fillEndX = pad + innerW * frac;
    float const fillW = std::max(0.f, fillEndX - fillStartX);
    ccColor3B const defaultFillCol = isNativeGD
        ? (tel.isPractice ? ccc3(0, 255, 255) : ccc3(0, 255, 0))
        : ccc3(80, 220, 255);

    bool const customTexActive = cfg.useFillTexture && fillTex;
    if (customTexActive && fillW > 0.5f) {
        m_fillStencil->clear();
        std::vector<CCPoint> poly;
        buildRoundedRectPoly(fillStartX, pad, fillW, innerH, std::min(innerR, fillW * 0.49f), poly);
        if (poly.size() >= 3) {
            m_fillStencil->drawPolygon(poly.data(), static_cast<unsigned int>(poly.size()),
                {1, 1, 1, 1}, 0.f, {1, 1, 1, 1});
        }
        if (!m_fillTexSprite || m_fillTexSprite->getTexture() != fillTex) {
            if (m_fillTexSprite) m_fillTexSprite->removeFromParent();
            m_fillTexSprite = CCSprite::createWithTexture(fillTex);
            if (m_fillTexSprite) m_fillClip->addChild(m_fillTexSprite);
        }
        if (m_fillTexSprite) {
            auto ts = m_fillTexSprite->getContentSize();
            m_fillTexSprite->setPosition({w * 0.5f, h * 0.5f});
            m_fillTexSprite->setScaleX(w / std::max(1.f, ts.width));
            m_fillTexSprite->setScaleY(h / std::max(1.f, ts.height));
            ccColor3B tint = cfg.useCustomFillColor
                ? evalColorMode(cfg.fillColorMode, cfg.fillColor, cfg.fillColor2, cfg.fillColor3,
                                animTime, cfg.colorAnimSpeed, frac, frac * 0.5f)
                : ccc3(255, 255, 255);
            m_fillTexSprite->setColor(tint);
            m_fillTexSprite->setOpacity(static_cast<GLubyte>(alpha * 255.f));
            m_fillClip->setVisible(true);
        }
    } else {
        m_fillClip->setVisible(false);
    }

    if (!customTexActive && fillW > 0.5f) {
        if (cfg.style == BarStyle::Segmented) {
            int const segments = 20;
            float const gap = 2.f;
            float const segW = std::max(2.f, (innerW - gap * (segments - 1)) / segments);
            for (int i = 0; i < segments; ++i) {
                float sx = pad + i * (segW + gap);
                float segMid = (static_cast<float>(i) + 0.5f) / segments;
                if (segMid < startFrac || sx >= fillEndX) continue;
                float drawW = std::min(segW, fillEndX - sx);
                if (drawW <= 0.5f) continue;
                ccColor3B col = evalColorMode(
                    cfg.fillColorMode, cfg.fillColor, cfg.fillColor2, cfg.fillColor3,
                    animTime, cfg.colorAnimSpeed, frac, segMid);
                if (cfg.newBestFlash && tel.newBestFlashTimer > 0.f) {
                    col = lerpColor3(col, {255, 230, 90}, std::min(1.f, tel.newBestFlashTimer));
                }
                drawRoundedFilled(m_fillDraw, sx, pad, drawW, innerH, 1.f,
                    toColor4F(col, alpha), noBorder, 0.f);
            }
        } else {
            // Draw smooth gradient strips clipped neatly inside the track shape.
            bool const multiSlice =
                cfg.fillColorMode == BarColorMode::Gradient2 ||
                cfg.fillColorMode == BarColorMode::Gradient3 ||
                cfg.fillColorMode == BarColorMode::Rainbow;
            int const slices = multiSlice ? std::clamp(static_cast<int>(fillW / 4.f), 8, 40) : 1;

            // Subtle StartPos tick instead of obscuring the left fill region
            if (startFrac > 0.005f) {
                float sx = pad + innerW * startFrac;
                m_markerDraw->drawSegment(ccp(sx, pad), ccp(sx, pad + innerH), 1.0f,
                    {1.f, 1.f, 1.f, 0.55f * alpha});
            }

            if (slices == 1) {
                ccColor3B col = (cfg.useCustomFillColor || cfg.fillColorMode != BarColorMode::Solid)
                    ? evalColorMode(cfg.fillColorMode, cfg.fillColor, cfg.fillColor2, cfg.fillColor3,
                                    animTime, cfg.colorAnimSpeed, frac, frac * 0.5f)
                    : defaultFillCol;
                if (cfg.newBestFlash && tel.newBestFlashTimer > 0.f) {
                    col = lerpColor3(col, {255, 235, 95}, std::min(1.f, tel.newBestFlashTimer));
                }
                if (isNativeGD && m_gdFill && m_gdFill->getTexture()) {
                    float const texH = std::max(1.f, m_gdFill->getTexture()->getContentSize().height);
                    float const sy = std::max(0.05f, innerH / texH);
                    m_gdFill->setTextureRect(CCRectMake(0.f, 0.f, std::max(1.f, fillW / sy), texH));
                    m_gdFill->setScale(sy);
                    m_gdFill->setPosition({fillStartX, h * 0.5f});
                    m_gdFill->setColor(col);
                    m_gdFill->setOpacity(static_cast<GLubyte>(alpha * 255.f));
                    m_gdFill->setVisible(true);
                } else {
                    drawRoundedFilled(m_fillDraw, fillStartX, pad, fillW, innerH,
                        std::min(innerR, fillW * 0.49f), toColor4F(col, alpha), noBorder, 0.f);
                }
            } else {
                float sliceW = fillW / static_cast<float>(slices);
                for (int i = 0; i < slices; ++i) {
                    float sx = fillStartX + i * sliceW;
                    float xMid = ((sx + sliceW * 0.5f) - pad) / innerW;
                    ccColor3B col = (cfg.useCustomFillColor || cfg.fillColorMode != BarColorMode::Solid)
                        ? evalColorMode(cfg.fillColorMode, cfg.fillColor, cfg.fillColor2, cfg.fillColor3,
                                        animTime, cfg.colorAnimSpeed, frac, std::clamp(xMid, 0.f, 1.f))
                        : defaultFillCol;
                    if (cfg.newBestFlash && tel.newBestFlashTimer > 0.f) {
                        col = lerpColor3(col, {255, 235, 95}, std::min(1.f, tel.newBestFlashTimer));
                    }
                    float sw = (i == slices - 1) ? (fillEndX - sx) : (sliceW + 0.6f);
                    float rHere = (i == 0 || i == slices - 1) ? std::min(innerR, sw * 0.49f) : 0.f;
                    drawRoundedFilled(m_fillDraw, sx, pad, sw, innerH, rHere,
                        toColor4F(col, alpha), noBorder, 0.f);
                }
            }

            // Style overlays: Classic / Sleek / Glass top highlight; Striped diagonal bands
            if (cfg.style == BarStyle::Classic || cfg.style == BarStyle::Sleek || cfg.style == BarStyle::Glass) {
                float hiH = std::max(1.5f, innerH * 0.34f);
                drawRoundedFilled(m_fillDraw, fillStartX + 1.f, pad + innerH - hiH - 0.5f,
                    std::max(1.f, fillW - 2.f), hiH, std::min(innerR * 0.5f, hiH * 0.45f),
                    {1.f, 1.f, 1.f, (cfg.style == BarStyle::Classic ? 0.25f : 0.18f) * alpha},
                    noBorder, 0.f);
            } else if (cfg.style == BarStyle::Striped && fillW > 6.f) {
                float period = 14.f;
                float shift = std::fmod(animTime * cfg.colorAnimSpeed * 28.f, period);
                float stripeW = 5.5f;
                float skew = std::min(innerH * 0.7f, 8.f);
                for (float sx = fillStartX - period + shift; sx < fillEndX; sx += period) {
                    float x0 = std::max(fillStartX, sx);
                    float x1 = std::min(fillEndX, sx + stripeW);
                    if (x1 <= x0 + 0.5f) continue;
                    float x2 = std::min(fillEndX, x1 + skew);
                    float x3 = std::min(fillEndX, x0 + skew);
                    CCPoint quad[4] = {
                        ccp(x0, pad), ccp(x1, pad),
                        ccp(x2, pad + innerH), ccp(x3, pad + innerH)
                    };
                    m_fillDraw->drawPolygon(quad, 4, {1.f, 1.f, 1.f, 0.20f * alpha}, 0.f, noBorder);
                }
            }
        }
    }

    // Leading-edge tip glow / spark
    if (cfg.tipGlow && frac > 0.01f && frac < 0.999f) {
        ccColor4F tipCore = {1.f, 1.f, 1.f, 0.92f * alpha};
        float tipH = innerH + 2.f;
        drawRoundedFilled(m_markerDraw, fillEndX - 1.5f, (h - tipH) * 0.5f, 3.f, tipH, 1.5f,
            tipCore, noBorder, 0.f);
        m_markerDraw->drawDot(ccp(fillEndX, h * 0.5f), std::min(6.f, h * 0.55f),
            {1.f, 1.f, 1.f, 0.28f * alpha});
    }

    // 4. Milestones (25 / 50 / 75), Checkpoints, Best (PB), and Last Death markers
    if (cfg.showMilestones) {
        for (float m : {0.25f, 0.50f, 0.75f}) {
            float mx = pad + innerW * m;
            m_markerDraw->drawSegment(ccp(mx, 1.f), ccp(mx, h - 1.f), 0.7f,
                {1.f, 1.f, 1.f, 0.22f * alpha});
        }
    }
    if (cfg.showCheckpoints && !tel.checkpoints.empty()) {
        for (float cpPct : tel.checkpoints) {
            float cx = pad + innerW * std::clamp(cpPct / 100.f, 0.f, 1.f);
            m_markerDraw->drawDot(ccp(cx, h * 0.5f), std::clamp(h * 0.22f, 1.5f, 3.f),
                {0.25f, 0.92f, 1.f, 0.9f * alpha});
        }
    }
    if (cfg.showDeathMarker && tel.lastDeathPercent > 0.5f && tel.lastDeathPercent < 100.f) {
        float dx = pad + innerW * (tel.lastDeathPercent / 100.f);
        m_markerDraw->drawSegment(ccp(dx, 0.5f), ccp(dx, h - 0.5f), 1.0f,
            {1.f, 0.30f, 0.35f, 0.85f * alpha});
    }
    if (cfg.showPbMarker && tel.bestPercent > 0.5f && tel.bestPercent < 100.f) {
        float bx = pad + innerW * (tel.bestPercent / 100.f);
        m_markerDraw->drawSegment(ccp(bx, -1.5f), ccp(bx, h + 1.5f), 1.2f,
            {1.f, 0.85f, 0.22f, 0.95f * alpha});
        m_markerDraw->drawDot(ccp(bx, h + 2.5f), 2.0f,
            {1.f, 0.90f, 0.30f, 0.95f * alpha});
    }

    // 5. Outer border frame (skipped for Minimal and Native GD which draws slidergroove2.png)
    float const bw = std::clamp(cfg.borderWidth, 0.f, 4.f);
    if (bw > 0.1f && cfg.style != BarStyle::Minimal && cfg.style != BarStyle::VanillaGD) {
        ccColor4F borderCol = toColor4F(cfg.borderColor,
            (cfg.style == BarStyle::Glass ? 0.65f : 0.90f) * alpha);
        drawRoundedFilled(m_borderDraw, 0.f, 0.f, w, h, radius, noBorder, borderCol, bw * 0.5f);
    }

    // 6. Telemetry label
    updateLabel(cfg, tel, animTime, w, h, fillEndX);
}

void CustomProgressBarNode::updateLabel(
    ProgressBarConfig const& cfg,
    ProgressBarTelemetry const& tel,
    float animTime,
    float w,
    float h,
    float fillEndX
) {
    if (!m_label) return;
    m_label->setVisible(cfg.showPercentage);
    if (!cfg.showPercentage) return;

    int const prec = std::clamp(cfg.decimalPrecision, 0, 2);
    float const pct = std::clamp(tel.rawPercent, 0.f, 100.f);
    auto fmtPct = [&](float v) {
        if (prec == 0) return fmt::format("{:.0f}%", std::floor(v + 0.0001f));
        if (prec == 1) return fmt::format("{:.1f}%", v);
        return fmt::format("{:.2f}%", v);
    };

    std::string text;
    if (tel.isPlatformer && cfg.labelFormat == BarLabelFormat::AutoTime) {
        text = formatDurationTime(tel.elapsedSeconds);
    } else {
        switch (cfg.labelFormat) {
            case BarLabelFormat::PercentBest:
                text = fmt::format("{} / {:.0f}%", fmtPct(pct), std::clamp(tel.bestPercent, 0.f, 100.f));
                break;
            case BarLabelFormat::RunSegment:
                if (tel.startPosPercent > 0.5f) {
                    text = fmt::format("{:.0f}% -> {}", tel.startPosPercent, fmtPct(pct));
                } else {
                    text = fmtPct(pct);
                }
                break;
            case BarLabelFormat::Compact:
                text = prec == 0 ? fmt::format("{:.0f}", std::floor(pct + 0.0001f))
                                 : (prec == 1 ? fmt::format("{:.1f}", pct) : fmt::format("{:.2f}", pct));
                break;
            case BarLabelFormat::AutoTime:
            case BarLabelFormat::Percent:
            default:
                text = fmtPct(pct);
                break;
        }
    }

    if (text != m_lastLabelText) {
        m_lastLabelText = text;
        m_label->setString(text.c_str());
    }

    std::string const& desiredFont = !cfg.percentageFont.empty() ? cfg.percentageFont : std::string("bigFont.fnt");
    ProgressBarManager::applyFontIfNeeded(m_label, desiredFont);

    float const baseScale = 0.42f * std::clamp(cfg.percentageScale, 0.25f, 3.0f);
    m_label->setScale(baseScale);
    m_label->setOpacity(static_cast<GLubyte>(std::clamp(cfg.opacity, 0, 255)));

    ccColor3B col = cfg.useCustomPercentageColor
        ? evalColorMode(cfg.pctColorMode, cfg.percentageColor, cfg.pctColor2, cfg.pctColor2,
                        animTime, cfg.colorAnimSpeed, pct / 100.f, 0.5f)
        : ccc3(255, 255, 255);
    if (cfg.newBestFlash && tel.newBestReached && tel.bestPercent > 1.f && !tel.isPractice) {
        col = lerpColor3(col, {255, 228, 85}, 0.75f);
    }
    m_label->setColor(col);

    // Position relative to the bar node
    if (cfg.useCustomLabelPosition || cfg.labelPlacement == BarLabelPlacement::CustomFreePos) {
        if (auto* parent = this->getParent()) {
            CCPoint world = ccp(cfg.labelPosX, cfg.labelPosY);
            CCPoint local = this->convertToNodeSpace(world);
            m_label->setAnchorPoint({0.5f, 0.5f});
            m_label->setPosition(local);
            return;
        }
    }

    CCPoint pos = ccp(w * 0.5f, h * 0.5f);
    CCPoint anchor = ccp(0.5f, 0.5f);
    switch (cfg.labelPlacement) {
        case BarLabelPlacement::InsideCenter:
            anchor = ccp(0.5f, 0.5f);
            pos = ccp(w * 0.5f, h * 0.5f);
            break;
        case BarLabelPlacement::RightOfBar:
            anchor = ccp(0.f, 0.5f);
            pos = ccp(w + 8.f, h * 0.5f);
            break;
        case BarLabelPlacement::LeftOfBar:
            anchor = ccp(1.f, 0.5f);
            pos = ccp(-8.f, h * 0.5f);
            break;
        case BarLabelPlacement::AboveBar:
            anchor = ccp(0.5f, 0.f);
            pos = ccp(w * 0.5f, h + 5.f);
            break;
        case BarLabelPlacement::BelowBar:
            anchor = ccp(0.5f, 1.f);
            pos = ccp(w * 0.5f, -5.f);
            break;
        case BarLabelPlacement::FollowTip:
            anchor = ccp(0.5f, 0.f);
            pos = ccp(std::clamp(fillEndX, 12.f, w - 12.f), h + 6.f);
            break;
        case BarLabelPlacement::CustomFreePos:
            break;
    }
    m_label->setAnchorPoint(anchor);
    m_label->setPosition(pos + ccp(cfg.percentageOffsetX, cfg.percentageOffsetY));
}
