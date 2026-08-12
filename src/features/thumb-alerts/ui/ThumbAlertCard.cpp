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

constexpr float kCardW = 312.f;
constexpr float kCardH = 96.f;
constexpr float kRadius = 9.f;
constexpr float kFaceX = 36.f;
constexpr float kTextX = 68.f;
constexpr float kRightPad = 12.f;
constexpr float kScreenMargin = 10.f;

// The shine and the Ken Burns drift are driven by hand instead of by actions,
// so they cannot fight the per-frame opacity the card writes on every child.
constexpr float kShineCycle = 3.6f;
constexpr float kShineSweep = 1.3f;
constexpr float kBurnsCycle = 9.f;

float easeOutCubic(float t) {
    float u = 1.f - t;
    return 1.f - u * u * u;
}

float easeInCubic(float t) { return t * t * t; }

float easeOutExpo(float t) {
    return t >= 1.f ? 1.f : 1.f - std::pow(2.f, -9.f * t);
}

float easeOutBack(float t) {
    constexpr float s = 1.6f;
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

// Value GJDifficultySprite expects, from the name the server stores.
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

char const* rateFrame(int tier) {
    switch (tier) {
        case 1: return "GJ_featuredCoin_001.png";
        case 2: return "GJ_epicCoin_001.png";
        case 3: return "GJ_epicCoin2_001.png";
        case 4: return "GJ_epicCoin3_001.png";
        default: return nullptr;
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

// A colour picked off a dark thumbnail can be almost black; lift it until it
// still reads as a border over the card.
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
    this->buildContent();
    if (m_config.click && m_item.levelId > 0) this->buildTouch();

    this->captureFade();
    this->applyAlpha(0.f);
    this->toPhase(Phase::In, std::max(0.05f, m_config.enterTime));
    return true;
}

void ThumbAlertCard::buildBackground(CCTexture2D* thumbnail) {
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
        // Cover crop: fill the card and let the stencil cut the overflow.
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

    // Dark on the left where the text sits, clear on the right so the artwork
    // still shows through.
    auto* sideFade = CCLayerGradient::create({5, 6, 12, 236}, {0, 0, 0, 0}, {1.f, 0.f});
    sideFade->setContentSize({kCardW * 0.66f, kCardH});
    sideFade->setPosition({0.f, 0.f});
    clipper->addChild(sideFade, 3);

    auto* footFade = CCLayerGradient::create({4, 5, 10, 215}, {0, 0, 0, 0}, {0.f, 1.f});
    footFade->setContentSize({kCardW, 34.f});
    footFade->setPosition({0.f, 0.f});
    clipper->addChild(footFade, 3);

    if (m_config.shine) {
        m_shine = CCSprite::create();
        if (m_shine) {
            m_shine->setTextureRect({0.f, 0.f, 1.f, 1.f});
            m_shine->setScaleX(42.f);
            m_shine->setScaleY(kCardH * 2.f);
            m_shine->setRotation(18.f);
            m_shine->setOpacity(0);
            ccBlendFunc additive = {GL_SRC_ALPHA, GL_ONE};
            m_shine->setBlendFunc(additive);
            m_shine->setPosition({-60.f, kCardH / 2.f});
            clipper->addChild(m_shine, 4);
        }
    }

    if (auto* border = paimon::SpriteHelper::safeCreateScale9("GJ_square07.png")) {
        border->setContentSize({kCardW, kCardH});
        border->setAnchorPoint({0.f, 0.f});
        border->setColor(m_accent);
        border->setOpacity(205);
        this->addChild(border, 5);
    }

    if (m_config.progress) {
        auto* track = CCLayerColor::create({0, 0, 0, 120}, kCardW - 20.f, 3.f);
        track->ignoreAnchorPointForPosition(false);
        track->setAnchorPoint({0.f, 0.5f});
        track->setPosition({10.f, 6.f});
        this->addChild(track, 6);

        m_bar = CCLayerColor::create({m_accent.r, m_accent.g, m_accent.b, 235}, kCardW - 20.f, 3.f);
        m_bar->ignoreAnchorPointForPosition(false);
        m_bar->setAnchorPoint({0.f, 0.5f});
        m_bar->setPosition({10.f, 6.f});
        this->addChild(m_bar, 7);
    }
}

void ThumbAlertCard::buildContent() {
    float const textWidth = kCardW - kTextX - kRightPad;

    if (char const* frame = rateFrame(m_item.rateTier)) {
        if (auto* coin = paimon::SpriteHelper::safeCreateWithFrameName(frame)) {
            coin->setScale(0.9f);
            coin->setPosition({kFaceX, kCardH - 40.f});
            this->addChild(coin, 8);
        }
    }

    if (auto* face = GJDifficultySprite::create(difficultyFace(m_item.difficulty),
                                                GJDifficultyName::Short)) {
        face->setScale(0.72f);
        face->setPosition({kFaceX, kCardH - 40.f});
        this->addChild(face, 9);
    }

    if (m_item.stars > 0) {
        auto* count = CCLabelBMFont::create(fmt::format("{}", m_item.stars).c_str(), "bigFont.fnt");
        count->setScale(0.36f);
        count->setAnchorPoint({1.f, 0.5f});
        count->setPosition({kFaceX + 3.f, 24.f});
        this->addChild(count, 9);

        char const* frame = m_item.length == "Plat." ? "GJ_moonsIcon_001.png" : "GJ_starsIcon_001.png";
        if (auto* icon = paimon::SpriteHelper::safeCreateWithFrameName(frame)) {
            icon->setScale(0.5f);
            icon->setAnchorPoint({0.f, 0.5f});
            icon->setPosition({kFaceX + 5.f, 24.f});
            this->addChild(icon, 9);
        }
    }

    auto* tag = CCLabelBMFont::create(tr("NEW THUMBNAIL", "NUEVA MINIATURA"), "goldFont.fnt");
    tag->setAnchorPoint({0.f, 0.5f});
    tag->limitLabelWidth(textWidth, 0.34f, 0.18f);
    tag->setPosition({kTextX, kCardH - 15.f});
    this->addChild(tag, 9);

    auto const name = m_item.levelName.empty()
        ? fmt::format("ID {}", m_item.levelId)
        : m_item.levelName;
    auto* title = CCLabelBMFont::create(name.c_str(), "bigFont.fnt");
    title->setAnchorPoint({0.f, 0.5f});
    title->limitLabelWidth(textWidth, 0.52f, 0.26f);
    title->setPosition({kTextX, kCardH - 38.f});
    this->addChild(title, 9);

    if (!m_item.creator.empty()) {
        auto* author = CCLabelBMFont::create(
            fmt::format("{} {}", tr("by", "de"), m_item.creator).c_str(), "goldFont.fnt");
        author->setAnchorPoint({0.f, 0.5f});
        author->limitLabelWidth(textWidth, 0.36f, 0.2f);
        author->setPosition({kTextX, kCardH - 56.f});
        this->addChild(author, 9);
    }

    // The stats eat into the bottom row from the right, so they are laid out
    // first and the credit line gets whatever is left.
    float statsEdge = kCardW - kRightPad;
    if (m_config.stats) {
        auto addStat = [&](char const* frame, std::string const& text) {
            auto* label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
            label->setScale(0.32f);
            label->setAnchorPoint({1.f, 0.5f});
            label->setPosition({statsEdge, 18.f});
            this->addChild(label, 9);
            statsEdge -= label->getScaledContentSize().width + 3.f;

            if (auto* icon = paimon::SpriteHelper::safeCreateWithFrameName(frame)) {
                icon->setScale(0.42f);
                icon->setAnchorPoint({1.f, 0.5f});
                icon->setPosition({statsEdge, 18.f});
                this->addChild(icon, 9);
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
    who->setPosition({kTextX, 18.f});
    this->addChild(who, 9);
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
    m_fadeGradients.clear();
    // Read every opacity before touching any of them: a container that has
    // already been dimmed would poison the base values of its children.
    std::function<void(CCNode*)> walk = [&](CCNode* node) {
        if (!node) return;
        if (auto* gradient = typeinfo_cast<CCLayerGradient*>(node)) {
            m_fadeGradients.push_back({gradient, gradient->getStartOpacity(),
                                       gradient->getEndOpacity()});
        } else if (node != m_shine) {
            if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node)) {
                m_fade.emplace_back(node, rgba->getOpacity());
            }
        }
        if (auto* children = node->getChildren()) {
            for (auto* child : CCArrayExt<CCNode*>(children)) walk(child);
        }
    };
    if (auto* children = this->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) walk(child);
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
    // Re-registering the handler while the dispatcher is mid-touch would
    // dereference a handler still sitting in the pending-add queue.
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
    float const wave = std::sin(m_elapsed * 2.f);
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
    switch (m_phase) {
        case Phase::In: {
            switch (m_config.enter) {
                case Enter::Slide:
                    pose.offset = m_edge * (1.f - easeOutExpo(t));
                    pose.alpha = easeOutCubic(std::min(t * 2.f, 1.f));
                    break;
                case Enter::Fade:
                    pose.alpha = easeOutCubic(t);
                    break;
                case Enter::Pop:
                    pose.scaleX = pose.scaleY = 0.4f + 0.6f * easeOutBack(t);
                    pose.alpha = easeOutCubic(std::min(t * 2.5f, 1.f));
                    break;
                case Enter::Drop:
                    pose.offset.y = (kCardH * 2.4f + 40.f) * (1.f - easeOutBounce(t));
                    pose.alpha = easeOutCubic(std::min(t * 3.f, 1.f));
                    break;
                case Enter::Flip:
                    pose.scaleX = std::max(0.02f, easeOutBack(t));
                    pose.alpha = easeOutCubic(std::min(t * 2.5f, 1.f));
                    break;
                case Enter::Zoom:
                    pose.scaleX = pose.scaleY = 1.45f - 0.45f * easeOutCubic(t);
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
            switch (m_config.exit) {
                case Exit::Slide:
                    pose.offset = m_edge * easeInBack(t);
                    pose.alpha = fadeOut;
                    break;
                case Exit::Fade:
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
    this->applyAlpha(pose.alpha);
    this->updateThumbMotion(pose.alpha);
}

void ThumbAlertCard::applyAlpha(float alpha) {
    float const a = std::clamp(alpha, 0.f, 1.f);
    for (auto& [node, base] : m_fade) {
        if (auto* rgba = typeinfo_cast<CCRGBAProtocol*>(node.data())) {
            rgba->setOpacity(static_cast<GLubyte>(base * a));
        }
    }
    for (auto& entry : m_fadeGradients) {
        if (!entry.node) continue;
        entry.node->setStartOpacity(static_cast<GLubyte>(entry.start * a));
        entry.node->setEndOpacity(static_cast<GLubyte>(entry.end * a));
    }
}

void ThumbAlertCard::updateThumbMotion(float alpha) {
    if (m_thumb && m_config.kenBurns) {
        float const phase = 0.5f - 0.5f * std::cos(m_elapsed * 2.f * static_cast<float>(M_PI) / kBurnsCycle);
        m_thumb->setScale(m_thumbScale * (1.f + 0.11f * phase));
        m_thumb->setPosition({m_thumbHome.x - 6.f * phase, m_thumbHome.y + 3.f * phase});
    }

    if (!m_shine) return;
    float const phase = std::fmod(m_elapsed, kShineCycle);
    if (phase > kShineSweep) {
        m_shine->setOpacity(0);
        return;
    }
    float const progress = phase / kShineSweep;
    m_shine->setPosition({-60.f + (kCardW + 120.f) * progress, kCardH / 2.f});
    m_shine->setOpacity(static_cast<GLubyte>(55.f * std::clamp(alpha, 0.f, 1.f)));
}

void ThumbAlertCard::finish() {
    if (m_finished) return;
    m_finished = true;
    this->unschedule(schedule_selector(ThumbAlertCard::tick));
    if (m_menu) m_menu->setEnabled(false);

    auto callback = std::move(m_onFinished);
    m_onFinished = nullptr;
    // Leaving the scene from inside the tick would remove the node the
    // scheduler is walking.
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
    this->finish();
}

} // namespace paimon::thumbalerts
