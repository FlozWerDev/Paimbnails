#include "CoverHero.hpp"
#include "VinylDisc.hpp"

#include "../../../../utils/PaimonDrawNode.hpp"
#include "../../../../utils/SpriteHelper.hpp"
#include "../../../../utils/TextureBudget.hpp"

#include "../../services/MenuMusicCoverLog.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::menumusic {

namespace {

constexpr float kTonearmPlayAngle = 16.f;
constexpr float kTonearmRestAngle = -7.f;

void drawCircle(PaimonDrawNode* node, CCPoint c, float r, ccColor4F col, int segs = 28) {
    if (!node || r <= 0.f) return;
    std::vector<CCPoint> pts;
    pts.reserve(static_cast<std::size_t>(segs));
    constexpr float kTau = 6.28318530717958647692f;
    for (int i = 0; i < segs; ++i) {
        float const a = kTau * static_cast<float>(i) / static_cast<float>(segs);
        pts.emplace_back(c.x + r * std::cos(a), c.y + r * std::sin(a));
    }
    node->drawPolygon(pts.data(), static_cast<unsigned int>(pts.size()), col, 0.f, {0, 0, 0, 0});
}

} // namespace

CoverHero* CoverHero::create(const CCSize& size, float skew) {
    auto ret = new CoverHero();
    if (ret && ret->init(size, skew)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool CoverHero::init(const CCSize& size, float skew) {
    if (!CCNode::init()) return false;

    m_size = size;
    m_skew = std::max(0.f, skew);

    this->setContentSize(m_size);
    this->setAnchorPoint({0.f, 0.5f});

    // Rounded showcase mask when skew == 0, diagonal mask if skew > 0.
    CCDrawNode* stencil = nullptr;
    if (m_skew <= 0.5f) {
        stencil = paimon::SpriteHelper::createRoundedRectStencil(m_size.width, m_size.height, 6.f);
    } else {
        stencil = PaimonDrawNode::create();
        if (stencil) {
            CCPoint poly[4] = {
                ccp(0.f, 0.f),
                ccp(m_size.width, 0.f),
                ccp(m_size.width - m_skew, m_size.height),
                ccp(0.f, m_size.height)
            };
            ccColor4F white = {1.f, 1.f, 1.f, 1.f};
            stencil->drawPolygon(poly, 4, white, 0.f, white);
        }
    }

    m_clip = CCClippingNode::create();
    if (!m_clip) return false;
    m_clip->setStencil(stencil);
    m_clip->setAlphaThreshold(0.05f);
    m_clip->setContentSize(m_size);
    m_clip->setAnchorPoint({0.f, 0.f});
    m_clip->setPosition({0.f, 0.f});
    this->addChild(m_clip, 0);

    // Rich turntable studio backdrop fallback when no cover is present.
    m_fallback = CCNode::create();
    if (m_fallback) {
        m_fallback->setContentSize(m_size);
        m_fallback->setAnchorPoint({0.f, 0.f});

        if (auto* bg = paimon::SpriteHelper::safeCreateNineSliceFromFile("square02b_001.png")) {
            bg->setScaleMultiplier(0.45f);
            bg->setContentSize(m_size);
            bg->setAnchorPoint({0.f, 0.f});
            bg->setColor({16, 20, 32});
            bg->setOpacity(240);
            m_fallback->addChild(bg, 0);
        }

        if (m_skew > 0.5f) {
            auto* note = paimon::SpriteHelper::safeCreateWithFrameName("GJ_musicOnBtn_001.png");
            if (note) {
                note->setScale(1.1f);
                note->setColor({145, 175, 225});
                note->setOpacity(60);
                note->setPosition({m_size.width * 0.47f, m_size.height * 0.52f});
                m_fallback->addChild(note, 1);
            }
        }

        m_clip->addChild(m_fallback, 0);
    }

    // Smooth dark stage vignette (high-step bands so there is no visible striping).
    m_gradient = PaimonDrawNode::create();
    if (m_gradient) {
        auto* d = static_cast<PaimonDrawNode*>(m_gradient);
        CCPoint full[4] = {
            ccp(0.f, 0.f), ccp(m_size.width, 0.f),
            ccp(m_size.width, m_size.height), ccp(0.f, m_size.height)
        };
        d->drawPolygon(full, 4, ccc4f(0.02f, 0.03f, 0.06f, 0.30f), 0.f, ccc4f(0, 0, 0, 0));

        // Smooth bottom legibility gradient for the level thumbnail caption.
        constexpr int kBotBands = 18;
        constexpr float kBotH = 42.f;
        for (int i = 0; i < kBotBands; ++i) {
            float const t0 = static_cast<float>(i) / kBotBands;
            float const t1 = static_cast<float>(i + 1) / kBotBands;
            float const tm = 0.5f * (t0 + t1);
            float const y0 = kBotH * t0;
            float const y1 = kBotH * t1;
            float const alpha = 0.48f * (1.f - tm) * (1.f - tm);
            CCPoint q[4] = {
                ccp(0.f, y0), ccp(m_size.width, y0),
                ccp(m_size.width, y1), ccp(0.f, y1)
            };
            d->drawPolygon(q, 4, ccc4f(0.01f, 0.01f, 0.03f, alpha), 0.f, ccc4f(0, 0, 0, 0));
        }

        // Gentle top shadow lip.
        constexpr int kTopBands = 12;
        constexpr float kTopH = 22.f;
        for (int i = 0; i < kTopBands; ++i) {
            float const t0 = static_cast<float>(i) / kTopBands;
            float const t1 = static_cast<float>(i + 1) / kTopBands;
            float const tm = 0.5f * (t0 + t1);
            float const y0 = m_size.height - kTopH + kTopH * t0;
            float const y1 = m_size.height - kTopH + kTopH * t1;
            float const alpha = 0.28f * tm * tm;
            CCPoint q[4] = {
                ccp(0.f, y0), ccp(m_size.width, y0),
                ccp(m_size.width, y1), ccp(0.f, y1)
            };
            d->drawPolygon(q, 4, ccc4f(0.01f, 0.01f, 0.03f, alpha), 0.f, ccc4f(0, 0, 0, 0));
        }
        m_gradient->setContentSize(m_size);
        m_clip->addChild(m_gradient, 2);
    }

    // Classic GD subtle showcase border frame (GJ_square07.png).
    if (m_skew <= 0.5f) {
        if (auto* frame = paimon::SpriteHelper::safeCreateNineSliceFromFile("GJ_square07.png")) {
            frame->setScaleMultiplier(0.45f);
            frame->setContentSize(m_size);
            frame->setAnchorPoint({0.f, 0.f});
            frame->setPosition({0.f, 0.f});
            frame->setOpacity(125);
            this->addChild(frame, 3);
        }
    } else {
        m_borderGloss = PaimonDrawNode::create();
        if (m_borderGloss) {
            CCPoint s0 = ccp(m_size.width - 1.f, 0.f);
            CCPoint s1 = ccp(m_size.width - m_skew - 1.f, m_size.height);
            m_borderGloss->drawSegment(s0, s1, 0.7f, ccc4f(0.f, 0.f, 0.f, 0.3f));
            this->addChild(m_borderGloss, 3);
        }
    }

    // Large, centerpiece HD vinyl disc.
    float const discRadius = (m_skew <= 0.5f)
        ? std::min(m_size.width, m_size.height) * 0.375f
        : std::min(m_size.width * 0.28f, m_size.height * 0.30f);

    CCPoint const discCenter = {
        m_size.width * (m_skew <= 0.5f ? 0.45f : 0.44f),
        m_size.height * (m_skew <= 0.5f ? 0.52f : 0.50f)
    };

    // Subtle machined turntable platter ring underneath the vinyl disc.
    if (m_skew <= 0.5f) {
        if (auto* platter = PaimonDrawNode::create()) {
            drawCircle(platter, {discCenter.x + 1.5f, discCenter.y - 2.f}, discRadius + 6.5f,
                {0.0f, 0.0f, 0.0f, 0.38f}, 64);
            drawCircle(platter, discCenter, discRadius + 5.2f,
                {0.20f, 0.23f, 0.30f, 0.78f}, 64);
            drawCircle(platter, discCenter, discRadius + 3.4f,
                {0.07f, 0.08f, 0.12f, 0.92f}, 64);
            // Subtle strobe dots around the turntable platter bezel.
            constexpr int kDots = 36;
            constexpr float kTau = 6.28318530717958647692f;
            float const dotRingR = discRadius + 4.3f;
            for (int i = 0; i < kDots; ++i) {
                float const a = kTau * static_cast<float>(i) / static_cast<float>(kDots);
                drawCircle(platter,
                    {discCenter.x + dotRingR * std::cos(a), discCenter.y + dotRingR * std::sin(a)},
                    0.75f, {0.62f, 0.68f, 0.78f, 0.42f}, 6);
            }
            this->addChild(platter, 3);
        }
    }

    m_smallDisc = VinylDisc::create(discRadius);
    if (m_smallDisc) {
        m_smallDisc->setAnchorPoint({0.5f, 0.5f});
        m_smallDisc->setPosition(discCenter);
        this->addChild(m_smallDisc, 4);
    }

    if (m_skew <= 0.5f) {
        buildTonearm();
        this->schedule(schedule_selector(CoverHero::tickTonearm));
    }

    return true;
}

void CoverHero::buildTonearm() {
    CCPoint const pivotPos = {m_size.width - 20.f, m_size.height - 22.f};
    float const armLen = std::min(m_size.width, m_size.height) * 0.46f;

    // Stationary brushed gimbal mounting collar + parked tonearm rest clip.
    auto* baseDraw = PaimonDrawNode::create();
    if (baseDraw) {
        // Tonearm rest post along the right edge near the parked arm shaft.
        CCPoint const restPos = {pivotPos.x - 4.5f, pivotPos.y - armLen * 0.62f};
        drawCircle(baseDraw, {restPos.x + 1.f, restPos.y - 1.f}, 4.0f, {0.f, 0.f, 0.f, 0.35f}, 16);
        drawCircle(baseDraw, restPos, 3.6f, {0.22f, 0.25f, 0.32f, 0.95f}, 16);
        drawCircle(baseDraw, restPos, 2.0f, {0.58f, 0.63f, 0.72f, 0.85f}, 12);

        drawCircle(baseDraw, {pivotPos.x + 1.5f, pivotPos.y - 2.f}, 11.5f, {0.f, 0.f, 0.f, 0.40f});
        drawCircle(baseDraw, pivotPos, 11.f, {0.14f, 0.16f, 0.22f, 0.96f});
        drawCircle(baseDraw, pivotPos, 8.6f, {0.68f, 0.73f, 0.80f, 0.96f});
        drawCircle(baseDraw, pivotPos, 6.2f, {0.22f, 0.25f, 0.32f, 1.f});
        drawCircle(baseDraw, {pivotPos.x - 1.5f, pivotPos.y + 1.5f}, 2.0f, {0.95f, 0.97f, 1.f, 0.80f});
        this->addChild(baseDraw, 5);
    }

    // Rotating tonearm assembly pivoted at pivotPos.
    m_tonearmPivot = CCNode::create();
    m_tonearmPivot->setPosition(pivotPos);
    m_tonearmAngle = m_paused ? kTonearmRestAngle : kTonearmPlayAngle;
    m_tonearmPivot->setRotation(m_tonearmAngle);
    this->addChild(m_tonearmPivot, 6);

    auto* arm = PaimonDrawNode::create();
    if (!arm) return;

    CCPoint const backEnd  = {0.f, 12.f};
    CCPoint const elbow    = {0.f, -armLen * 0.74f};
    CCPoint const tip      = {-11.f, -armLen};

    // Soft cast shadow beneath the arm.
    arm->drawSegment({backEnd.x + 2.f, backEnd.y - 2.f}, {elbow.x + 2.f, elbow.y - 2.f}, 2.4f, {0.f, 0.f, 0.f, 0.32f});
    arm->drawSegment({elbow.x + 2.f, elbow.y - 2.f}, {tip.x + 2.f, tip.y - 2.f}, 2.2f, {0.f, 0.f, 0.f, 0.32f});

    // Rear cylindrical counterweight.
    CCPoint cw[4] = {{-3.4f, 7.f}, {3.4f, 7.f}, {3.4f, 14.5f}, {-3.4f, 14.5f}};
    arm->drawPolygon(cw, 4, {0.32f, 0.35f, 0.42f, 1.f}, 0.6f, {0.72f, 0.76f, 0.84f, 0.9f});

    // Brushed metallic arm tube + specular highlight spine.
    arm->drawSegment(backEnd, elbow, 1.85f, {0.76f, 0.80f, 0.88f, 1.f});
    arm->drawSegment(elbow, tip, 1.65f, {0.76f, 0.80f, 0.88f, 1.f});
    arm->drawSegment({-0.5f, backEnd.y}, {-0.5f, elbow.y}, 0.65f, {0.96f, 0.98f, 1.f, 0.85f});
    arm->drawSegment({elbow.x - 0.5f, elbow.y}, {tip.x - 0.5f, tip.y}, 0.55f, {0.96f, 0.98f, 1.f, 0.85f});

    // Angled dark headshell cartridge + gold stylus trim.
    CCPoint const dir = ccpNormalize(ccpSub(tip, elbow));
    CCPoint const perp = {-dir.y, dir.x};
    CCPoint const hStart = ccpSub(tip, ccpMult(dir, 5.f));
    CCPoint const hEnd   = ccpAdd(tip, ccpMult(dir, 7.f));
    CCPoint head[4] = {
        ccpAdd(hStart, ccpMult(perp, 3.0f)),
        ccpSub(hStart, ccpMult(perp, 3.0f)),
        ccpSub(hEnd,   ccpMult(perp, 2.4f)),
        ccpAdd(hEnd,   ccpMult(perp, 2.4f))
    };
    arm->drawPolygon(head, 4, {0.12f, 0.14f, 0.20f, 1.f}, 0.7f, {0.92f, 0.78f, 0.36f, 0.92f});

    // Center gimbal cap screw.
    drawCircle(arm, {0.f, 0.f}, 3.8f, {0.86f, 0.90f, 0.96f, 1.f}, 20);
    drawCircle(arm, {0.f, 0.f}, 1.8f, {0.20f, 0.22f, 0.28f, 1.f}, 16);

    m_tonearmPivot->addChild(arm, 0);
}

void CoverHero::tickTonearm(float dt) {
    if (!m_tonearmPivot) return;
    float const safeDt = std::clamp(dt, 0.f, 0.05f);
    float const target = m_paused ? kTonearmRestAngle : kTonearmPlayAngle;
    float const rate = 1.f - std::exp(-7.5f * safeDt);
    m_tonearmAngle = m_tonearmAngle + (target - m_tonearmAngle) * rate;
    m_tonearmPivot->setRotation(m_tonearmAngle);
}

void CoverHero::setCoversFromPaths(std::vector<std::string> const& absolutePaths) {
    this->unschedule(schedule_selector(CoverHero::tickCarousel));
    m_coverPaths.clear();
    m_carouselIndex = 0;
    m_carouselElapsed = 0.f;

    for (auto const& path : absolutePaths) {
        if (!path.empty()) m_coverPaths.push_back(path);
    }

    if (m_coverPaths.empty()) {
        coverlog::info("[MenuMusicCover] CoverHero: no cover paths, showing fallback");
        clearCover();
        return;
    }

    coverlog::info("[MenuMusicCover] CoverHero: loading {} cover(s), first='{}'",
        m_coverPaths.size(), m_coverPaths.front());

    if (m_coverPaths.size() == 1) {
        setCoverFromPath(m_coverPaths.front());
        return;
    }

    showCoverAtIndex(0);
    this->schedule(schedule_selector(CoverHero::tickCarousel), 1.f);
}

void CoverHero::showCoverAtIndex(std::size_t index) {
    if (m_coverPaths.empty()) return;
    m_carouselIndex = index % m_coverPaths.size();
    setCoverFromPath(m_coverPaths[m_carouselIndex]);
}

void CoverHero::tickCarousel(float dt) {
    if (m_coverPaths.size() <= 1) return;
    m_carouselElapsed += dt;
    if (m_carouselElapsed < kCarouselInterval) return;
    m_carouselElapsed = 0.f;
    showCoverAtIndex(m_carouselIndex + 1);
}

std::string CoverHero::getCurrentCoverPath() const {
    if (m_coverPaths.empty()) return {};
    return m_coverPaths[m_carouselIndex % m_coverPaths.size()];
}

void CoverHero::setCoverFromPath(const std::string& absolutePath) {
    if (absolutePath == m_coverPath && (m_coverSprite || m_coverLoading)) return;
    m_coverPath = absolutePath;
    auto const generation = ++m_coverGeneration;
    m_coverLoading = false;
    if (absolutePath.empty()) {
        if (m_coverSprite) { m_coverSprite->removeFromParent(); m_coverSprite = nullptr; }
        if (m_fallback) m_fallback->setVisible(true);
        if (m_smallDisc) m_smallDisc->setCoverFromPath("");
        return;
    }
    m_coverLoading = true;
    if (m_smallDisc) m_smallDisc->setCoverFromPath(absolutePath);
    WeakRef<CoverHero> weak = this;
    paimon::image::loadBudgetedAsync(absolutePath, [weak, generation](CCTexture2D* texture) {
        auto self = weak.lock();
        if (!self || generation != self->m_coverGeneration) return;
        self->m_coverLoading = false;
        if (!texture) {
            if (self->m_coverSprite) { self->m_coverSprite->removeFromParent(); self->m_coverSprite = nullptr; }
            if (self->m_fallback) self->m_fallback->setVisible(true);
            return;
        }
        self->applyCoverTexture(texture);
    });
}

void CoverHero::applyCoverTexture(CCTexture2D* texture) {
    if (m_coverSprite) { m_coverSprite->removeFromParent(); m_coverSprite = nullptr; }
    m_coverSprite = CCSprite::createWithTexture(texture);
    if (!m_coverSprite) {
        return;
    }

    const CCSize ts = m_coverSprite->getContentSize();
    const float sx = m_size.width / ts.width;
    const float sy = m_size.height / ts.height;
    const float scale = std::max(sx, sy);
    m_coverSprite->setScale(scale);
    m_coverSprite->setAnchorPoint({0.5f, 0.5f});
    m_coverSprite->setPosition({m_size.width / 2.f, m_size.height / 2.f});

    if (m_clip) m_clip->addChild(m_coverSprite, 1);
    if (m_fallback) m_fallback->setVisible(false);
    setPausedAppearance(m_paused);
}

void CoverHero::onExit() {
    ++m_coverGeneration;
    m_coverLoading = false;
    unschedule(schedule_selector(CoverHero::tickCarousel));
    CCNode::onExit();
}

void CoverHero::clearCover() {
    this->unschedule(schedule_selector(CoverHero::tickCarousel));
    m_coverPaths.clear();
    m_carouselIndex = 0;
    m_carouselElapsed = 0.f;
    setCoverFromPath("");
}

void CoverHero::startSpinning() {
    if (m_smallDisc) m_smallDisc->startSpinning();
}

void CoverHero::stopSpinning() {
    if (m_smallDisc) m_smallDisc->stopSpinning();
}

void CoverHero::setPausedAppearance(bool paused) {
    m_paused = paused;
    const ccColor3B tint = paused
        ? ccColor3B{130, 130, 140}
        : ccColor3B{255, 255, 255};

    if (m_coverSprite) {
        m_coverSprite->setColor(tint);
        m_coverSprite->setOpacity(paused ? 200 : 255);
    }
    if (m_smallDisc) {
        m_smallDisc->setPausedAppearance(paused);
        if (paused) m_smallDisc->stopSpinning();
        else m_smallDisc->startSpinning();
    }
}

} // namespace paimon::menumusic
