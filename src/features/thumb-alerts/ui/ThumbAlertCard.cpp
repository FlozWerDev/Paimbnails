#include "ThumbAlertCard.hpp"

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/PaimonButtonHighlighter.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../thumbnails/services/LevelColors.hpp"
#include "../../twitch-requests/services/TwitchLevelOpen.hpp"

#include <Geode/binding/GJDifficultySprite.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <string_view>

using namespace geode::prelude;

namespace paimon::thumbalerts {

namespace {

constexpr float kCardW = 328.f;
constexpr float kCardH = 102.f;
constexpr float kRadius = 9.f;
constexpr float kBadgeX = 34.f;
constexpr float kDividerX = 68.f;
constexpr float kTextX = 78.f;
constexpr float kRightPad = 12.f;
constexpr float kScreenMargin = 10.f;

// scrim stays solid under the labels, then fades: the card's right end stays artwork.
constexpr float kPanelW = 176.f;
constexpr float kPanelFadeW = 116.f;

constexpr float kBarInset = 6.f;
constexpr float kBarY = 6.f;

// shine and ken burns run by hand, not actions: no fight with per-frame child opacity.
constexpr float kShineCycle = 3.6f;
constexpr float kShineSweep = 1.3f;
constexpr float kShineWidth = 56.f;
constexpr float kBurnsCycle = 9.f;

// text offset from rest while the card is still arriving.
constexpr float kRevealSlide = 13.f;

float easeOutCubic(float t) {
    float u = 1.f - t;
    return 1.f - u * u * u;
}

float easeInCubic(float t) { return t * t * t; }

float easeOutExpo(float t) {
    return t >= 1.f ? 1.f : 1.f - std::pow(2.f, -9.f * t);
}

float easeOutBack(float t, float s = 1.6f) {
    float u = t - 1.f;
    return 1.f + u * u * ((s + 1.f) * u + s);
}

float easeInBack(float t) {
    constexpr float s = 1.6f;
    return t * t * ((s + 1.f) * t - s);
}

float easeOutElastic(float t) {
    if (t <= 0.f) return 0.f;
    if (t >= 1.f) return 1.f;
    constexpr float period = 0.36f;
    return std::pow(2.f, -10.f * t) *
        std::sin((t - period / 4.f) * 2.f * static_cast<float>(M_PI) / period) + 1.f;
}

float easeOutBounce(float t) {
    constexpr float n = 7.5625f;
    constexpr float d = 2.75f;
    if (t < 1.f / d) return n * t * t;
    if (t < 2.f / d) { t -= 1.5f / d; return n * t * t + 0.75f; }
    if (t < 2.5f / d) { t -= 2.25f / d; return n * t * t + 0.9375f; }
    t -= 2.625f / d;
    return n * t * t + 0.984375f;
}

// gjdifficultysprite value from the server-stored name.
int difficultyFace(std::string const& name) {
    static std::pair<std::string_view, int> const table[] = {
        {"Auto", -1}, {"Unrated", 0}, {"Easy", 1}, {"Normal", 2}, {"Hard", 3},
        {"Harder", 4}, {"Insane", 5}, {"Demon", 6}, {"Easy Demon", 7},
        {"Medium Demon", 8}, {"Hard Demon", 6}, {"Insane Demon", 9},
        {"Extreme Demon", 10},
    };
    for (auto const& [key, value] : table) {
        if (name == key) return value;
    }
    return 0;
}

GJFeatureState featureState(int tier) {
    switch (tier) {
        case 1: return GJFeatureState::Featured;
        case 2: return GJFeatureState::Epic;
        case 3: return GJFeatureState::Legendary;
        case 4: return GJFeatureState::Mythic;
        default: return GJFeatureState::None;
    }
}

ccColor3B rateColor(int tier) {
    switch (tier) {
        case 1: return {90, 175, 255};
        case 2: return {255, 140, 60};
        case 3: return {255, 80, 80};
        case 4: return {185, 100, 255};
        default: return {255, 214, 122};
    }
}

// dark-thumbnail picks come out near-black; lift until readable over the card.
ccColor3B brighten(ccColor3B color) {
    int const peak = std::max({static_cast<int>(color.r), static_cast<int>(color.g),
                               static_cast<int>(color.b)});
    if (peak == 0) return {255, 214, 122};
    if (peak >= 120) return color;
    float const factor = 120.f / static_cast<float>(peak);
    return {
        static_cast<GLubyte>(std::min(255.f, color.r * factor)),
        static_cast<GLubyte>(std::min(255.f, color.g * factor)),
        static_cast<GLubyte>(std::min(255.f, color.b * factor)),
    };
}

ccColor3B shade(ccColor3B color, float factor) {
    return {
        static_cast<GLubyte>(color.r * factor),
        static_cast<GLubyte>(color.g * factor),
        static_cast<GLubyte>(color.b * factor),
    };
}

// 80x80 frames, 8px corner: default thirds fold in on chip-short heights.
CCScale9Sprite* smallFrame(char const* file) {
    return paimon::SpriteHelper::safeCreateScale9(file, CCRectMake(8.f, 8.f, 64.f, 64.f));
}

std::string shortCount(int value) {
    if (value >= 1000000) return fmt::format("{:.1f}M", value / 1000000.f);
    if (value >= 10000) return fmt::format("{}K", value / 1000);
    return fmt::format("{}", value);
}

bool isSpanish() {
    return Mod::get()->getSettingValue<std::string>("language") == "spanish";
}

char const* tr(char const* english, char const* spanish) {
    return isSpanish() ? spanish : english;
}

CCPoint edgeOffset(Spot spot, CCSize scaled) {
    switch (spot) {
        case Spot::TopLeft:
        case Spot::MidLeft:
        case Spot::BottomLeft:
            return {-(scaled.width + kScreenMargin * 2.f), 0.f};
        case Spot::TopRight:
        case Spot::MidRight:
        case Spot::BottomRight:
            return {scaled.width + kScreenMargin * 2.f, 0.f};
        case Spot::TopCenter:
            return {0.f, scaled.height + kScreenMargin * 2.f};
        default:
            return {0.f, -(scaled.height + kScreenMargin * 2.f)};
    }
}

} // namespace

ThumbAlertCard* ThumbAlertCard::create(NewThumb const& item, Config const& config,
                                       CCTexture2D* thumbnail) {
    auto* ret = new ThumbAlertCard();
    if (ret->init(item, config, thumbnail)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ThumbAlertCard::init(NewThumb const& item, Config const& config, CCTexture2D* thumbnail) {
    if (!CCNodeRGBA::init()) return false;

    m_item = item;
    m_config = config;
    m_accent = rateColor(item.rateTier);
    if (item.levelId > 0) {
        if (auto pair = LevelColors::get().getPair(item.levelId)) {
            m_accent = brighten(pair->a);
        }
    }

    this->setID("thumb-alert"_spr);
    this->setContentSize({kCardW, kCardH});
    this->setAnchorPoint({0.5f, 0.5f});
    this->ignoreAnchorPointForPosition(false);

    this->buildBackground(thumbnail);
    this->buildFrame();
    this->buildBadges();
    this->buildContent();
    if (m_config.click && m_item.levelId > 0) this->buildTouch();

    this->captureFade();
    this->applyAlpha(0.f, 0.f);
    this->applyReveal(0.f, 0.f);
    this->toPhase(Phase::In, std::max(0.05f, m_config.enterTime));
    return true;
}

void ThumbAlertCard::buildBackground(CCTexture2D* thumbnail) {
    // dark ring under the frame: white border alone washes out over bright menus.
    if (auto* shadow = paimon::SpriteHelper::safeCreateScale9("square02b_001.png")) {
        shadow->setContentSize({kCardW + 10.f, kCardH + 10.f});
        shadow->setAnchorPoint({0.f, 0.f});
        shadow->setPosition({-5.f, -6.f});
        shadow->setColor({0, 0, 0});
        shadow->setOpacity(95);
        this->addChild(shadow, -1);
    }

    auto* clipper = CCClippingNode::create();
    clipper->setContentSize({kCardW, kCardH});
    clipper->setAnchorPoint({0.f, 0.f});
    clipper->setPosition({0.f, 0.f});
    clipper->setStencil(paimon::SpriteHelper::createRoundedRectStencil(kCardW, kCardH, kRadius));
    this->addChild(clipper, 0);

    auto* plate = CCLayerColor::create({9, 10, 18, 255});
    plate->setContentSize({kCardW, kCardH});
    clipper->addChild(plate, 0);

    if (thumbnail) {
        m_thumb = CCSprite::createWithTexture(thumbnail);
    }
    if (m_thumb) {
        auto const size = m_thumb->getContentSize();
        // cover crop: fill the card, stencil cuts the overflow.
        m_thumbScale = std::max(kCardW / std::max(size.width, 1.f),
                                kCardH / std::max(size.height, 1.f));
        m_thumbHome = ccp(kCardW / 2.f, kCardH / 2.f);
        m_thumb->setScale(m_thumbScale);
        m_thumb->setPosition(m_thumbHome);
        clipper->addChild(m_thumb, 1);
    }

    auto* dim = CCLayerColor::create({0, 0, 0, static_cast<GLubyte>(std::clamp(m_config.dim, 0, 255))});
    dim->setContentSize({kCardW, kCardH});
    clipper->addChild(dim, 2);

    auto* panel = CCLayerColor::create({6, 7, 14, 228});
    panel->setContentSize({kPanelW, kCardH});
    clipper->addChild(panel, 3);

    auto* panelFade = CCLayerGradient::create({6, 7, 14, 228}, {0, 0, 0, 0}, {1.f, 0.f});
    panelFade->setContentSize({kPanelFadeW, kCardH});
    panelFade->setPosition({kPanelW, 0.f});
    clipper->addChild(panelFade, 3);

    auto* footFade = CCLayerGradient::create({4, 5, 11, 212}, {0, 0, 0, 0}, {0.f, 1.f});
    footFade->setContentSize({kCardW, 34.f});
    footFade->setPosition({0.f, 0.f});
    clipper->addChild(footFade, 3);

    if (m_config.shine) {
        ccBlendFunc additive = {GL_SRC_ALPHA, GL_ONE};
        float const half = kShineWidth / 2.f;
        float const tall = kCardH * 2.4f;

        m_shine = CCNode::create();
        m_shine->setPosition({-kShineWidth, kCardH / 2.f});
        m_shine->setRotation(16.f);
        clipper->addChild(m_shine, 4);

        // two halves, not one quad: a hard-edged bar reads as a seam, not light.
        m_shineLead = CCLayerGradient::create({255, 255, 255, 0}, {255, 255, 255, 0}, {1.f, 0.f});
        m_shineLead->setContentSize({half, tall});
        m_shineLead->setPosition({-half, -tall / 2.f});
        m_shineLead->setBlendFunc(additive);
        m_shine->addChild(m_shineLead);

        m_shineTrail = CCLayerGradient::create({255, 255, 255, 0}, {255, 255, 255, 0}, {1.f, 0.f});
        m_shineTrail->setContentSize({half, tall});
        m_shineTrail->setPosition({0.f, -tall / 2.f});
        m_shineTrail->setBlendFunc(additive);
        m_shine->addChild(m_shineTrail);
    }

    if (m_config.progress) {
        auto* track = CCLayerColor::create({0, 0, 0, 150}, kCardW - kBarInset * 2.f, 3.f);
        track->setPosition({kBarInset, kBarY});
        clipper->addChild(track, 5);

        m_bar = CCLayerColor::create({m_accent.r, m_accent.g, m_accent.b, 245},
                                     kCardW - kBarInset * 2.f, 3.f);
        // left-edge anchor: draining would otherwise eat the bar from both ends.
        m_bar->ignoreAnchorPointForPosition(false);
        m_bar->setAnchorPoint({0.f, 0.5f});
        m_bar->setPosition({kBarInset, kBarY + 1.5f});
        clipper->addChild(m_bar, 6);
    }
}

void ThumbAlertCard::buildFrame() {
    if (auto* border = paimon::SpriteHelper::safeCreateScale9("GJ_square07.png")) {
        border->setContentSize({kCardW, kCardH});
        border->setAnchorPoint({0.f, 0.f});
        border->setColor({255, 255, 255});
        border->setOpacity(235);
        this->addChild(border, 5);
    }

    auto* divider = CCLayerColor::create({255, 255, 255, 45}, 1.f, kCardH - 34.f);
    divider->setPosition({kDividerX, 17.f});
    this->addChild(divider, 6);
}

void ThumbAlertCard::buildBadges() {
    m_badges = CCNode::create();
    m_badges->setPosition({kBadgeX, kCardH / 2.f});
    this->addChild(m_badges, 8);

    if (auto* face = GJDifficultySprite::create(difficultyFace(m_item.difficulty),
                                                GJDifficultyName::Short)) {
        // rate burst belongs to the face: hand-hung coins doubled the glow vs the difficulty.
        face->updateFeatureState(featureState(m_item.rateTier));
        face->setScale(0.86f);
        face->setPosition({0.f, 13.f});
        m_badges->addChild(face);
    }

    if (m_item.stars <= 0) return;

    auto* count = CCLabelBMFont::create(fmt::format("{}", m_item.stars).c_str(), "bigFont.fnt");
    count->setScale(0.38f);
    count->setAnchorPoint({1.f, 0.5f});
    count->setPosition({-2.f, -27.f});
    m_badges->addChild(count);

    char const* frame = m_item.length == "Plat." ? "GJ_moonsIcon_001.png" : "GJ_starsIcon_001.png";
    if (auto* icon = paimon::SpriteHelper::safeCreateWithFrameName(frame)) {
        icon->setScale(0.52f);
        icon->setAnchorPoint({0.f, 0.5f});
        icon->setPosition({0.f, -27.f});
        m_badges->addChild(icon);
    }
}

void ThumbAlertCard::buildContent() {
    m_content = CCNode::create();
    this->addChild(m_content, 9);

    float const textWidth = kCardW - kTextX - kRightPad;

    constexpr float chipH = 20.f;
    constexpr float chipY = kCardH - 16.f;
    auto* tag = CCLabelBMFont::create(tr("NEW THUMBNAIL", "NUEVA MINIATURA"), "bigFont.fnt");
    tag->setAnchorPoint({0.f, 0.5f});
    tag->limitLabelWidth(textWidth - 20.f, 0.3f, 0.2f);

    float const chipW = tag->getScaledContentSize().width + 18.f;
    if (auto* chip = smallFrame("square02b_001.png")) {
        chip->setContentSize({chipW, chipH});
        chip->setAnchorPoint({0.f, 0.5f});
        chip->setPosition({kTextX, chipY});
        chip->setColor(shade(m_accent, 0.3f));
        chip->setOpacity(238);
        m_content->addChild(chip, 0);
    }
    if (auto* chipEdge = smallFrame("GJ_square07.png")) {
        chipEdge->setContentSize({chipW, chipH});
        chipEdge->setAnchorPoint({0.f, 0.5f});
        chipEdge->setPosition({kTextX, chipY});
        chipEdge->setColor(m_accent);
        m_content->addChild(chipEdge, 1);
    }
    tag->setPosition({kTextX + 9.f, chipY});
    m_content->addChild(tag, 2);

    auto const name = m_item.levelName.empty()
        ? fmt::format("ID {}", m_item.levelId)
        : m_item.levelName;
    auto* title = CCLabelBMFont::create(name.c_str(), "bigFont.fnt");
    title->setAnchorPoint({0.f, 0.5f});
    title->limitLabelWidth(textWidth - 26.f, 0.56f, 0.26f);
    title->setPosition({kTextX, 61.f});
    m_content->addChild(title, 2);

    if (!m_item.creator.empty()) {
        auto* author = CCLabelBMFont::create(
            fmt::format("{} {}", tr("by", "de"), m_item.creator).c_str(), "goldFont.fnt");
        author->setAnchorPoint({0.f, 0.5f});
        author->limitLabelWidth(textWidth - 26.f, 0.36f, 0.2f);
        author->setPosition({kTextX, 43.f});
        m_content->addChild(author, 2);
    }

    // stats eat the bottom row from the right: laid out first, credit gets the rest.
    float statsEdge = kCardW - kRightPad;
    if (m_config.stats) {
        auto addStat = [&](char const* frame, std::string const& text) {
            auto* label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
            label->setScale(0.32f);
            label->setAnchorPoint({1.f, 0.5f});
            label->setPosition({statsEdge, 21.f});
            m_content->addChild(label, 2);
            statsEdge -= label->getScaledContentSize().width + 3.f;

            if (auto* icon = paimon::SpriteHelper::safeCreateWithFrameName(frame)) {
                icon->setScale(0.42f);
                icon->setAnchorPoint({1.f, 0.5f});
                icon->setPosition({statsEdge, 21.f});
                m_content->addChild(icon, 2);
                statsEdge -= icon->getScaledContentSize().width + 8.f;
            }
        };

        if (m_item.likes != 0) addStat("GJ_likesIcon_001.png", shortCount(m_item.likes));
        if (m_item.downloads > 0) addStat("GJ_downloadsIcon_001.png", shortCount(m_item.downloads));
        if (m_item.coins > 0) {
            addStat(m_item.verifiedCoins ? "GJ_coinsIcon_001.png" : "GJ_coinsIcon2_001.png",
                    fmt::format("{}", m_item.coins));
        }
    }

    auto const credit = m_item.uploader.empty()
        ? std::string(tr("Thumbnail added", "Miniatura anadida"))
        : fmt::format("{} {}", tr("Thumbnail by", "Miniatura de"), m_item.uploader);
    auto* who = CCLabelBMFont::create(credit.c_str(), "chatFont.fnt");
    who->setAnchorPoint({0.f, 0.5f});
    who->limitLabelWidth(std::max(60.f, statsEdge - 8.f - kTextX), 0.36f, 0.2f);
    who->setColor(m_accent);
    who->setPosition({kTextX, 21.f});
    m_content->addChild(who, 2);
}

void ThumbAlertCard::buildTouch() {
    m_menu = CCMenu::create();
    m_menu->setPosition({0.f, 0.f});
    m_menu->setContentSize({kCardW, kCardH});
    this->addChild(m_menu, 20);

    auto* hit = CCSprite::create();
    if (!hit) return;
    hit->setTextureRect({0.f, 0.f, 1.f, 1.f});
    hit->setScaleX(kCardW);
    hit->setScaleY(kCardH);
    hit->setOpacity(0);

    auto* button = CCMenuItemSpriteExtra::create(
        hit, this, menu_selector(ThumbAlertCard::onOpenLevel));
    if (!button) return;
    button->setPosition({kCardW / 2.f, kCardH / 2.f});
    PaimonButtonHighlighter::registerButton(button);
    m_menu->addChild(button);
}

void ThumbAlertCard::captureFade() {
    m_fade.clear();
    m_fadeContent.clear();
    m_fadeGradients.clear();
    // snapshot opacities first: an already-dimmed container would poison children's bases.
    std::function<void(CCNode*, FadeList&)> walk = [&](CCNode* node, FadeList& into) {
        if (!node || node == m_shine) return;
        if (auto* gradient = typeinfo_cast<CCLayerGradient*>(node)) {
            m_fadeGradients.push_back({gradient, gradient->getStartOpacity(),
                                       gradient->getEndOpacity()});
        } else if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node)) {
            into.emplace_back(node, rgba->getOpacity());
        }
        if (auto* children = node->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) walk(child, into);
        }
    };
    if (auto* children = this->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            bool const late = child == m_content || child == m_badges;
            walk(child, late ? m_fadeContent : m_fade);
        }
    }
}

void ThumbAlertCard::placeAt(CCPoint rest, float scale) {
    m_rest = rest;
    m_baseScale = std::clamp(scale, kMinScale, kMaxScale);
    m_edge = edgeOffset(m_config.spot, {kCardW * m_baseScale, kCardH * m_baseScale});
    this->setPosition(rest);
    this->setScale(m_baseScale);
}

void ThumbAlertCard::onEnter() {
    CCNodeRGBA::onEnter();
    this->schedule(schedule_selector(ThumbAlertCard::tick));

    if (!m_menu || m_priorityQueued) return;
    m_priorityQueued = true;
    // re-register deferred: mid-touch re-register dereferences the pending-add queue.
    WeakRef<ThumbAlertCard> weak = this;
    Loader::get()->queueInMainThread([weak] {
        if (auto self = weak.lock(); self && self->m_menu) {
            self->m_menu->setHandlerPriority(-1000);
        }
    });
}

void ThumbAlertCard::toPhase(Phase phase, float duration) {
    m_phase = phase;
    m_phaseTime = 0.f;
    m_phaseDuration = std::max(0.01f, duration);
}

void ThumbAlertCard::applyIdle(Pose& pose) const {
    if (m_config.idle == Idle::None) return;
    // hold-relative wave: m_elapsed seeding would hand over mid-value from the entry.
    float const wave = std::sin(m_phaseTime * 2.f);
    switch (m_config.idle) {
        case Idle::Float: pose.offset.y += wave * 2.5f; break;
        case Idle::Sway:  pose.offset.x += wave * 3.f; break;
        case Idle::Pulse: {
            float const grow = 1.f + wave * 0.016f;
            pose.scaleX *= grow;
            pose.scaleY *= grow;
            break;
        }
        case Idle::Tilt:  pose.rotation += wave * 1.2f; break;
        default: break;
    }
}

void ThumbAlertCard::tick(float dt) {
    if (paimon::isRuntimeShuttingDown()) {
        this->finish();
        return;
    }

    m_elapsed += dt;
    m_phaseTime += dt;
    float const t = std::clamp(m_phaseTime / m_phaseDuration, 0.f, 1.f);

    Pose pose;
    float content = 1.f;
    float pop = 1.f;
    switch (m_phase) {
        case Phase::In: {
            switch (m_config.enter) {
                case Enter::Slide: {
                    // overshoots past rest and stretches in: lands instead of stopping dead.
                    float const travel = 1.f - easeOutBack(t, 0.9f);
                    float const stretch = 0.09f * std::abs(travel);
                    pose.offset = m_edge * travel;
                    if (m_edge.x != 0.f) {
                        pose.scaleX = 1.f + stretch;
                        pose.scaleY = 1.f - stretch * 0.7f;
                    } else {
                        pose.scaleY = 1.f + stretch;
                        pose.scaleX = 1.f - stretch * 0.7f;
                    }
                    pose.alpha = easeOutCubic(std::min(t * 2.f, 1.f));
                    break;
                }
                case Enter::Fade:
                    pose.scaleX = pose.scaleY = 0.97f + 0.03f * easeOutCubic(t);
                    pose.alpha = easeOutCubic(t);
                    break;
                case Enter::Pop: {
                    float const grow = 0.4f + 0.6f * easeOutBack(t);
                    float const squash = 0.08f * (1.f - easeOutCubic(t));
                    pose.scaleX = grow * (1.f + squash);
                    pose.scaleY = grow * (1.f - squash);
                    pose.alpha = easeOutCubic(std::min(t * 2.5f, 1.f));
                    break;
                }
                case Enter::Drop: {
                    float const bounce = easeOutBounce(t);
                    // squash on floor contact, flattening as it settles.
                    float const hit = std::max(0.f, 1.f - std::abs(1.f - bounce) * 7.f) * (1.f - t);
                    pose.offset.y = (kCardH * 2.4f + 40.f) * (1.f - bounce);
                    pose.scaleX = 1.f + 0.16f * hit;
                    pose.scaleY = 1.f - 0.16f * hit;
                    pose.alpha = easeOutCubic(std::min(t * 3.f, 1.f));
                    break;
                }
                case Enter::Flip:
                    pose.scaleX = std::max(0.02f, easeOutBack(t));
                    pose.alpha = easeOutCubic(std::min(t * 2.5f, 1.f));
                    break;
                case Enter::Zoom:
                    pose.scaleX = pose.scaleY = 1.45f - 0.45f * easeOutExpo(t);
                    pose.alpha = easeOutCubic(std::min(t * 1.8f, 1.f));
                    break;
                case Enter::Elastic:
                    pose.offset = m_edge * (1.f - easeOutElastic(t));
                    pose.alpha = easeOutCubic(std::min(t * 3.f, 1.f));
                    break;
                case Enter::Unfold:
                    pose.scaleY = std::max(0.02f, easeOutBack(t));
                    pose.alpha = easeOutCubic(std::min(t * 2.5f, 1.f));
                    break;
                case Enter::Swing:
                    pose.offset = m_edge * (1.f - easeOutExpo(t));
                    pose.rotation = -11.f * (1.f - easeOutElastic(t));
                    pose.alpha = easeOutCubic(std::min(t * 2.f, 1.f));
                    break;
                case Enter::Spiral:
                    pose.scaleX = pose.scaleY = 0.15f + 0.85f * easeOutCubic(t);
                    pose.rotation = -220.f * (1.f - easeOutCubic(t));
                    pose.alpha = easeOutCubic(std::min(t * 2.f, 1.f));
                    break;
                default: break;
            }
            if (m_config.enter != Enter::None) {
                content = easeOutCubic(std::clamp((t - 0.18f) / 0.82f, 0.f, 1.f));
                pop = easeOutBack(std::clamp((t - 0.24f) / 0.76f, 0.f, 1.f));
            }
            if (t >= 1.f) this->toPhase(Phase::Hold, std::max(0.2f, m_config.hold));
            break;
        }
        case Phase::Hold: {
            if (m_bar) m_bar->setScaleX(1.f - t);
            this->applyIdle(pose);
            if (t >= 1.f) this->toPhase(Phase::Out, std::max(0.05f, m_config.exitTime));
            break;
        }
        case Phase::Out: {
            float const fadeOut = 1.f - easeInCubic(std::min(t * 1.25f, 1.f));
            content = 1.f - easeInCubic(std::min(t * 1.5f, 1.f));
            switch (m_config.exit) {
                case Exit::Slide: {
                    float const travel = easeInBack(t);
                    float const stretch = 0.08f * std::abs(travel);
                    pose.offset = m_edge * travel;
                    if (m_edge.x != 0.f) {
                        pose.scaleX = 1.f + stretch;
                        pose.scaleY = 1.f - stretch * 0.7f;
                    } else {
                        pose.scaleY = 1.f + stretch;
                        pose.scaleX = 1.f - stretch * 0.7f;
                    }
                    pose.alpha = fadeOut;
                    break;
                }
                case Exit::Fade:
                    pose.scaleX = pose.scaleY = 1.f - 0.04f * easeInCubic(t);
                    pose.alpha = 1.f - easeOutCubic(t);
                    break;
                case Exit::Shrink:
                    pose.scaleX = pose.scaleY = std::max(0.02f, 1.f - 0.95f * easeInBack(t));
                    pose.alpha = fadeOut;
                    break;
                case Exit::Fall:
                    pose.offset.y = -(kCardH * 3.f + 60.f) * easeInCubic(t);
                    pose.rotation = 22.f * easeInCubic(t);
                    pose.alpha = fadeOut;
                    break;
                case Exit::Flip:
                    pose.scaleX = std::max(0.02f, 1.f - easeInBack(t));
                    pose.alpha = fadeOut;
                    break;
                case Exit::Zoom:
                    pose.scaleX = pose.scaleY = 1.f + 0.45f * easeInCubic(t);
                    pose.alpha = 1.f - easeOutCubic(t);
                    break;
                case Exit::Spin:
                    pose.scaleX = pose.scaleY = std::max(0.02f, 1.f - 0.95f * easeInCubic(t));
                    pose.rotation = 200.f * easeInCubic(t);
                    pose.alpha = fadeOut;
                    break;
                case Exit::Fold:
                    pose.scaleY = std::max(0.02f, 1.f - easeInBack(t));
                    pose.alpha = fadeOut;
                    break;
                default:
                    break;
            }
            if (t >= 1.f) {
                this->finish();
                return;
            }
            break;
        }
    }

    this->setPosition(m_rest + pose.offset);
    this->setScaleX(m_baseScale * pose.scaleX);
    this->setScaleY(m_baseScale * pose.scaleY);
    this->setRotation(pose.rotation);
    this->applyAlpha(pose.alpha, content);
    this->applyReveal(content, pop);
    this->updateAmbient(pose.alpha);
}

void ThumbAlertCard::applyAlpha(float alpha, float content) {
    float const a = std::clamp(alpha, 0.f, 1.f);
    float const c = a * std::clamp(content, 0.f, 1.f);
    for (auto& [node, base] : m_fade) {
        if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node.data())) {
            rgba->setOpacity(static_cast<GLubyte>(base * a));
        }
    }
    for (auto& [node, base] : m_fadeContent) {
        if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node.data())) {
            rgba->setOpacity(static_cast<GLubyte>(base * c));
        }
    }
    for (auto& entry : m_fadeGradients) {
        if (!entry.node) continue;
        entry.node->setStartOpacity(static_cast<GLubyte>(entry.start * a));
        entry.node->setEndOpacity(static_cast<GLubyte>(entry.end * a));
    }
}

void ThumbAlertCard::applyReveal(float content, float pop) {
    if (m_content) m_content->setPositionX(kRevealSlide * (1.f - std::clamp(content, 0.f, 1.f)));
    if (m_badges) m_badges->setScale(0.62f + 0.38f * pop);
}

void ThumbAlertCard::updateAmbient(float alpha) {
    if (m_thumb && m_config.kenBurns) {
        float const phase = 0.5f - 0.5f * std::cos(m_elapsed * 2.f * static_cast<float>(M_PI) / kBurnsCycle);
        m_thumb->setScale(m_thumbScale * (1.f + 0.11f * phase));
        m_thumb->setPosition({m_thumbHome.x - 6.f * phase, m_thumbHome.y + 3.f * phase});
    }

    if (!m_shineLead) return;
    float const phase = std::fmod(m_elapsed, kShineCycle);
    if (phase > kShineSweep) {
        m_shineLead->setEndOpacity(0);
        m_shineTrail->setStartOpacity(0);
        return;
    }
    float const progress = phase / kShineSweep;
    m_shine->setPositionX(-kShineWidth + (kCardW + kShineWidth * 2.f) * progress);
    // fade along the sweep instead of clipping at edges.
    float const peak = 72.f * std::sin(progress * static_cast<float>(M_PI)) *
        std::clamp(alpha, 0.f, 1.f);
    m_shineLead->setEndOpacity(static_cast<GLubyte>(peak));
    m_shineTrail->setStartOpacity(static_cast<GLubyte>(peak));
}

void ThumbAlertCard::finish() {
    if (m_finished) return;
    m_finished = true;
    this->unschedule(schedule_selector(ThumbAlertCard::tick));
    if (m_menu) m_menu->setEnabled(false);

    auto callback = std::move(m_onFinished);
    m_onFinished = nullptr;
    // deferred removal: tick-time scene leave would pull the node the scheduler walks.
    Ref<ThumbAlertCard> self = this;
    Loader::get()->queueInMainThread([self, callback = std::move(callback)] {
        self->removeFromParent();
        if (callback) callback();
    });
}

void ThumbAlertCard::onOpenLevel(CCObject*) {
    if (m_finished || m_item.levelId <= 0) return;
    if (m_menu) m_menu->setEnabled(false);
    paimon::twitch::openRequestedLevel(m_item.levelId, false);
    // exit plays over the level screen instead of blinking out.
    if (m_phase != Phase::Out) this->toPhase(Phase::Out, 0.25f);
}

} // namespace paimon::thumbalerts
