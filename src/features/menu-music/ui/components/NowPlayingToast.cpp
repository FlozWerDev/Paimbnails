#include "NowPlayingToast.hpp"

#include "../../services/MenuMusicLibrary.hpp"
#include "../../services/MenuMusicPlayer.hpp"

#include "../../../../utils/PaimonDrawNode.hpp"

#include <fmt/format.h>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace geode::prelude;

namespace paimon::menumusic {

static const std::string kToastId = "menumusic/now-playing-toast";

static constexpr float kWaitDur     = 0.30f;
static constexpr float kDropInDur   = 0.30f;
static constexpr float kExpandDur   = 0.38f;
static constexpr float kCollapseDur = 0.30f;
static constexpr float kLiftOutDur  = 0.30f;

static constexpr int kCornerSegments = 10;

float NowPlayingToast::easeOutCubic(float t) {
    const float u = 1.f - t;
    return 1.f - u * u * u;
}

float NowPlayingToast::easeInCubic(float t) {
    return t * t * t;
}

float NowPlayingToast::easeInOutCubic(float t) {
    if (t < 0.5f) return 4.f * t * t * t;
    const float p = 2.f * t - 2.f;
    return 0.5f * p * p * p + 1.f;
}

// Redraw: rounded rect con radio dinamico
//
// El draw node se limpia y re-llena cada frame con un rounded rect
// que tiene el ancho `width` y un radio tal que:
//
//   * cuando width == height, radio = height/2 (circulo perfecto)
//   * cuando width >>> height, radio = height/2 (pill)
//
// El radio es SIEMPRE height/2 asi la pill pasa suavemente de
// circulo a pildora sin saltos visuales en las esquinas.

void NowPlayingToast::redrawPill(float width) {
    if (!m_pill) return;

    auto* paimonPill = static_cast<PaimonDrawNode*>(m_pill);
    paimonPill->clear();

    const float h = m_pillHeight;
    const float w = std::max(width, h);
    const float r = h / 2.f;

    const ccColor4F fill   = ccc4FFromccc4B({22, 24, 38, 235});
    const ccColor4F stroke = ccc4FFromccc4B({35, 38, 55, 255});

    std::vector<CCPoint> pts;
    pts.reserve(4 * kCornerSegments);

    auto addArc = [&](float cx, float cy, float startAngle) {
        for (int i = 0; i < kCornerSegments; ++i) {
            const float a = startAngle + (static_cast<float>(M_PI) * 0.5f) *
                (static_cast<float>(i) / static_cast<float>(kCornerSegments));
            pts.push_back({cx + cosf(a) * r, cy + sinf(a) * r});
        }
    };

    addArc(r,       r,       static_cast<float>(M_PI));
    addArc(w - r,   r,       static_cast<float>(M_PI) * 1.5f);
    addArc(w - r,   h - r,   0.f);
    addArc(r,       h - r,   static_cast<float>(M_PI) * 0.5f);

    paimonPill->drawPolygon(pts.data(),
        static_cast<unsigned int>(pts.size()),
        fill, 1.0f, stroke);

    if (w > h * 1.1f) {
        paimonPill->drawSegment(
            {r * 0.6f, h - 1.5f},
            {w - r * 0.6f, h - 1.5f},
            0.4f,
            ccc4FFromccc4B({80, 85, 105, 90}));
    }

    // CCDrawNode dibuja desde (0,0); centramos restando w/2,h/2 para que
    // la pill se expanda simetricamente.
    const CCSize parentSize = this->getContentSize();
    paimonPill->setContentSize({w, h});
    paimonPill->setAnchorPoint({0.f, 0.f});
    paimonPill->setPosition({
        parentSize.width  / 2.f - w / 2.f,
        parentSize.height / 2.f - h / 2.f
    });
}

void NowPlayingToast::setContentOpacity(float op) {
    if (!m_contentHolder) return;
    const GLubyte a = static_cast<GLubyte>(std::clamp(op, 0.f, 1.f) * 255.f);

    // Recursivo en vez de cascadeOpacity: CCLabelBMFont no siempre lo
    // propaga bien en cocos 2.x.
    std::function<void(CCNode*)> apply = [&](CCNode* n) {
        if (!n) return;
        if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(n)) {
            rgba->setOpacity(a);
        }
        if (auto* children = n->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) {
                apply(child);
            }
        }
    };
    apply(m_contentHolder);
}

bool NowPlayingToast::init(const std::string& title, const std::string& subtitle) {
    if (!CCNode::init()) return false;

    const CCSize winSize = CCDirector::get()->getWinSize();
    const float cardW = 280.f;
    const float cardH = 38.f;

    this->setContentSize({cardW, cardH});
    this->setAnchorPoint({0.5f, 0.5f});
    this->setID(kToastId.c_str());
    this->setZOrder(500);

    m_pillWidth = cardW;
    m_pillHeight = cardH;
    m_circleWidth = cardH;

    m_showY = winSize.height - cardH * 0.65f;
    m_hideY = winSize.height + cardH * 1.5f;

    m_pill = PaimonDrawNode::create();
    if (m_pill) {
        this->addChild(m_pill, 0);
    }
    this->redrawPill(m_circleWidth);

    m_contentHolder = CCNode::create();
    if (m_contentHolder) {
        m_contentHolder->setContentSize({cardW, cardH});
        m_contentHolder->setAnchorPoint({0.5f, 0.5f});
        m_contentHolder->setPosition({cardW / 2.f, cardH / 2.f});
        m_contentHolder->setID("toast-content"_spr);
        this->addChild(m_contentHolder, 1);
    }

    if (m_contentHolder) {
        if (auto icon = CCSprite::createWithSpriteFrameName("GJ_musicOnBtn_001.png")) {
            icon->setScale(0.38f);
            icon->setPosition({22.f, cardH / 2.f});
            icon->setColor({255, 230, 140});
            m_contentHolder->addChild(icon, 1);
        }

        if (auto titleLbl = CCLabelBMFont::create(title.c_str(), "bigFont.fnt")) {
            titleLbl->setScale(0.45f);
            titleLbl->setAnchorPoint({0.f, 0.5f});
            titleLbl->setColor({255, 255, 255});
            titleLbl->limitLabelWidth(cardW - 60.f, 0.45f, 0.3f);
            titleLbl->setPosition({42.f, cardH * 0.65f});
            m_contentHolder->addChild(titleLbl, 2);
        }

        if (!subtitle.empty()) {
            if (auto subLbl = CCLabelBMFont::create(subtitle.c_str(), "chatFont.fnt")) {
                subLbl->setScale(0.35f);
                subLbl->setAnchorPoint({0.f, 0.5f});
                subLbl->setColor({200, 200, 220});
                subLbl->limitLabelWidth(cardW - 60.f, 0.35f, 0.25f);
                subLbl->setPosition({42.f, cardH * 0.28f});
                m_contentHolder->addChild(subLbl, 2);
            }
        }
    }

    // Contenido arranca invisible; se revelara durante Expand.
    this->setContentOpacity(0.f);

    float holdDuration = 1.5f;
    try {
        holdDuration = static_cast<float>(
            Mod::get()->getSettingValue<double>("menuLoopNotificationTime"));
    } catch (...) {
        // Setting no existe: usar default.
    }
    m_stayFor = std::clamp(holdDuration, 0.8f, 5.f);

    this->setPosition({winSize.width / 2.f, m_hideY});

    // Wait deja al main menu terminar su transicion; el toast queda
    // invisible para que el primer frame no muestre el circulo parado.
    this->setVisible(false);
    m_phase = Phase::Wait;
    m_phaseTime = 0.f;
    m_phaseDuration = kWaitDur;

    this->schedule(schedule_selector(NowPlayingToast::onTick));
    return true;
}

void NowPlayingToast::onExit() {
    this->unschedule(schedule_selector(NowPlayingToast::onTick));
    CCNode::onExit();
}

void NowPlayingToast::onTick(float dt) {
    if (m_phase == Phase::Done) return;

    m_phaseTime += dt;
    const float raw = m_phaseDuration > 0.f
        ? std::clamp(m_phaseTime / m_phaseDuration, 0.f, 1.f)
        : 1.f;

    const CCSize winSize = CCDirector::get()->getWinSize();

    switch (m_phase) {
        case Phase::Wait: {
            if (raw >= 1.f) {
                this->setVisible(true);
                m_phase = Phase::DropIn;
                m_phaseTime = 0.f;
                m_phaseDuration = kDropInDur;
            }
            break;
        }
        case Phase::DropIn: {
            const float t = easeOutCubic(raw);
            const float y = m_hideY + (m_showY - m_hideY) * t;
            this->setPosition({winSize.width / 2.f, y});
            const float s = 0.6f + 0.4f * t;
            this->setScale(s);

            if (raw >= 1.f) {
                m_phase = Phase::Expand;
                m_phaseTime = 0.f;
                m_phaseDuration = kExpandDur;
            }
            break;
        }
        case Phase::Expand: {
            const float t = easeInOutCubic(raw);
            const float w = m_circleWidth + (m_pillWidth - m_circleWidth) * t;
            this->redrawPill(w);
            const float contentT = std::clamp((raw - 0.3f) / 0.7f, 0.f, 1.f);
            this->setContentOpacity(easeOutCubic(contentT));
            this->setScale(1.f);

            if (raw >= 1.f) {
                m_phase = Phase::Hold;
                m_phaseTime = 0.f;
                m_phaseDuration = m_stayFor;
                this->setContentOpacity(1.f);
                this->redrawPill(m_pillWidth);
            }
            break;
        }
        case Phase::Hold: {
            if (raw >= 1.f) {
                m_phase = Phase::Collapse;
                m_phaseTime = 0.f;
                m_phaseDuration = kCollapseDur;
            }
            break;
        }
        case Phase::Collapse: {
            const float t = easeInOutCubic(raw);
            const float contentT = std::clamp(raw / 0.55f, 0.f, 1.f);
            this->setContentOpacity(1.f - easeInCubic(contentT));
            const float w = m_pillWidth + (m_circleWidth - m_pillWidth) * t;
            this->redrawPill(w);

            if (raw >= 1.f) {
                m_phase = Phase::LiftOut;
                m_phaseTime = 0.f;
                m_phaseDuration = kLiftOutDur;
                this->redrawPill(m_circleWidth);
                this->setContentOpacity(0.f);
            }
            break;
        }
        case Phase::LiftOut: {
            const float t = easeInCubic(raw);
            const float y = m_showY + (m_hideY - m_showY) * t;
            this->setPosition({winSize.width / 2.f, y});
            const float s = 1.f - 0.4f * t;
            this->setScale(s);

            if (raw >= 1.f) {
                m_phase = Phase::Done;
                this->unschedule(schedule_selector(NowPlayingToast::onTick));
                this->removeFromParent();
                return;
            }
            break;
        }
        case Phase::Done:
            break;
    }
}

void NowPlayingToast::showForCurrent(CCNode* parent) {
    if (!parent) return;
    if (!Mod::get()->getSettingValue<bool>("menuLoopEnableNotification")) return;

    if (auto old = parent->getChildByIDRecursive(kToastId.c_str())) {
        old->removeFromParentAndCleanup(true);
    }

    auto& player = MenuMusicPlayer::get();
    auto* track = player.currentTrack();
    if (!track) return;

    std::string prefix = Mod::get()->getSettingValue<std::string>("menuLoopCustomPrefix");
    if (prefix == "[Empty]") prefix.clear();

    std::string title = prefix.empty()
        ? track->displayName
        : fmt::format("{}: {}", prefix, track->displayName);

    std::string subtitle;
    switch (track->source) {
        case TrackSource::Downloaded: subtitle = "downloaded"; break;
        case TrackSource::Local:      subtitle = "local"; break;
        case TrackSource::GeometryDash: subtitle = "Geometry Dash"; break;
        default: break;
    }
    if (!track->artist.empty()) {
        subtitle = subtitle.empty() ? track->artist : fmt::format("{} - {}", subtitle, track->artist);
    }

    auto toast = new NowPlayingToast();
    if (toast && toast->init(title, subtitle)) {
        toast->autorelease();
        parent->addChild(toast, 500);
    } else {
        CC_SAFE_DELETE(toast);
    }
}

} // namespace paimon::menumusic
