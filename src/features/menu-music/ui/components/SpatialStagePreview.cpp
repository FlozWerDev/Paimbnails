#include "SpatialStagePreview.hpp"

#include "../../services/MenuMusicEffects.hpp"
#include "../../../../utils/SpriteHelper.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace paimon::menumusic {

namespace {

constexpr float kHeight = 112.f;
constexpr float kDegToRad = 0.017453292519943295f;

CCPoint pointOnStage(CCPoint center, float radius, float angle) {
    float const radians = angle * kDegToRad;
    return {center.x + std::sin(radians) * radius,
            center.y + std::cos(radians) * radius};
}

ccColor4F mixColor(ccColor4F a, ccColor4F b, float t) {
    return {
        a.r + (b.r - a.r) * t,
        a.g + (b.g - a.g) * t,
        a.b + (b.b - a.b) * t,
        a.a + (b.a - a.a) * t,
    };
}

} // namespace

SpatialStagePreview* SpatialStagePreview::create(float width) {
    auto* ret = new SpatialStagePreview();
    if (ret && ret->init(width)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool SpatialStagePreview::init(float width) {
    if (!CCNode::init()) return false;

    m_width = width;
    setID("menu-music-spatial-preview"_spr);
    setAnchorPoint({0.f, 0.f});
    setContentSize({width, kHeight});

    if (auto* bg = paimon::SpriteHelper::createColorPanel(
            width, kHeight, {8, 14, 31}, 205, 7.f)) {
        addChild(bg);
    }

    m_draw = PaimonDrawNode::create();
    m_draw->setID("spatial-stage-draw"_spr);
    addChild(m_draw);

    auto addCompassLabel = [this](char const* text, CCPoint position) {
        auto* label = CCLabelBMFont::create(text, "chatFont.fnt");
        label->setScale(0.32f);
        label->setColor({125, 148, 184});
        label->setPosition(position);
        addChild(label);
    };
    addCompassLabel("FRENTE", {width / 2.f, kHeight - 9.f});
    addCompassLabel("ATRAS", {width / 2.f, 24.f});
    addCompassLabel("I", {width / 2.f - 66.f, 62.f});
    addCompassLabel("D", {width / 2.f + 66.f, 62.f});

    if (auto* listener = paimon::SpriteHelper::safeCreateWithFrameName(
            "GJ_musicOnBtn_001.png")) {
        listener->setScale(0.22f);
        listener->setPosition({width / 2.f, 62.f});
        addChild(listener, 2);
    }

    m_statusLabel = CCLabelBMFont::create("", "goldFont.fnt");
    m_statusLabel->setID("spatial-stage-status"_spr);
    m_statusLabel->setScale(0.38f);
    m_statusLabel->setPosition({width / 2.f, 9.f});
    addChild(m_statusLabel);

    redraw();
    schedule(schedule_selector(SpatialStagePreview::tick), 0.05f);
    return true;
}

void SpatialStagePreview::tick(float) {
    redraw();
}

void SpatialStagePreview::redraw() {
    if (!m_draw) return;
    m_draw->clear();

    auto& effects = MenuMusicEffects::get();
    auto const& cfg = effects.config();
    CCPoint const center = {m_width / 2.f, 62.f};
    float const radius = 38.f;
    float const peak = effects.outputPeak();

    m_draw->drawSolidCircle(center, 8.f + peak * 7.f,
        {0.35f, 0.86f, 1.f, 0.12f + peak * 0.22f});
    for (float ring : {18.f, 29.f, 40.f}) {
        m_draw->drawCircle(center, ring, {0.f, 0.f, 0.f, 0.f}, 0.55f,
            {0.45f, 0.68f, 1.f, 0.2f}, 64);
    }
    m_draw->drawSegment({center.x - 44.f, center.y}, {center.x + 44.f, center.y},
        0.4f, {0.4f, 0.6f, 0.85f, 0.18f});
    m_draw->drawSegment({center.x, center.y - 43.f}, {center.x, center.y + 43.f},
        0.4f, {0.4f, 0.6f, 0.85f, 0.18f});

    float const width = std::clamp(cfg.spatialWidth, 0.f, 360.f);
    float const angle = effects.currentSpatialAngle();
    int const segments = std::max(2, static_cast<int>(std::ceil(width / 8.f)));
    ccColor4F const left = {1.f, 0.4f, 0.78f, 0.95f};
    ccColor4F const right = {0.25f, 0.9f, 1.f, 0.95f};
    CCPoint previous = pointOnStage(center, radius, angle - width / 2.f);
    for (int i = 1; i <= segments; ++i) {
        float const t = static_cast<float>(i) / segments;
        CCPoint const current = pointOnStage(
            center, radius, angle - width / 2.f + width * t);
        m_draw->drawSegment(previous, current, 1.25f, mixColor(left, right, t));
        previous = current;
    }

    auto const leftPoint = pointOnStage(center, radius, angle - width / 2.f);
    auto const rightPoint = pointOnStage(center, radius, angle + width / 2.f);
    m_draw->drawSolidCircle(leftPoint, 3.2f + peak * 1.5f, left);
    m_draw->drawSolidCircle(rightPoint, 3.2f + peak * 1.5f, right);

    if (!m_statusLabel) return;
    if (effects.isAuditionBypassed()) {
        m_statusLabel->setString("A/B: SONIDO ORIGINAL");
        m_statusLabel->setColor({255, 190, 110});
    } else if (!cfg.enabled || !cfg.spatialEnabled) {
        m_statusLabel->setString("ESPACIAL APAGADO");
        m_statusLabel->setColor({160, 165, 180});
    } else if (effects.isSpatialActive()) {
        m_statusLabel->setString("ESPACIAL EN VIVO");
        m_statusLabel->setColor({120, 255, 180});
    } else {
        m_statusLabel->setString("LISTO PARA REPRODUCIR");
        m_statusLabel->setColor({255, 220, 120});
    }
}

} // namespace paimon::menumusic
