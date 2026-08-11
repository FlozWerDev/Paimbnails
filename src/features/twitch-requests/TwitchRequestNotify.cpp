#include "TwitchRequestNotify.hpp"

#include "TwitchRequestManager.hpp"
#include "services/TwitchLevelBriefCache.hpp"
#include "../../core/RuntimeLifecycle.hpp"
#include "../../core/Settings.hpp"
#include "../../utils/MainThreadDelay.hpp"
#include "../../utils/SpriteHelper.hpp"

#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/ui/OverlayManager.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <vector>

using namespace geode::prelude;

namespace paimon::twitch {

namespace {

constexpr char const* kEnabledKey = "twitch-notify-enabled";
constexpr char const* kSpotKey = "twitch-notify-spot";
constexpr char const* kOffsetXKey = "twitch-notify-offset-x";
constexpr char const* kOffsetYKey = "twitch-notify-offset-y";
constexpr char const* kScaleKey = "twitch-notify-scale";
constexpr char const* kSecondsKey = "twitch-notify-seconds";
constexpr char const* kEnterKey = "twitch-notify-enter";
constexpr char const* kExitKey = "twitch-notify-exit";
constexpr char const* kSoundKey = "twitch-notify-sound";
constexpr char const* kShowLevelKey = "twitch-notify-show-level";
constexpr char const* kShowRequesterKey = "twitch-notify-show-requester";
constexpr char const* kOverLayerKey = "twitch-notify-over-layer";

// Fixed width keeps previews and live cards aligned.
constexpr float kCardWidth = 226.f;
constexpr float kCardPad = 8.f;
constexpr float kIconZone = 40.f;
constexpr float kLineGap = 3.f;
constexpr float kScreenMargin = 10.f;
constexpr float kStackGap = 6.f;
// Keep at most three stacked notices; evict the oldest without animation.
constexpr int kMaxLive = 3;

constexpr ccColor3B kDescColor = {171, 197, 232};

NotifyConfig g_config;

bool motionOn() {
    return !paimon::settings::smoothui::reducedMotion();
}

float animTime(float seconds) {
    auto const speed = std::clamp(paimon::settings::smoothui::globalSpeed(), 0.35, 2.5);
    return std::max(0.05f, seconds / static_cast<float>(speed));
}

std::string shorten(std::string text, size_t limit) {
    if (text.size() <= limit) return text;
    text.resize(limit > 3 ? limit - 3 : limit);
    text += "...";
    return text;
}

char const* soundFile(NotifySound sound) {
    switch (sound) {
        case NotifySound::Soft: return "chestClick.ogg";
        case NotifySound::Coin: return "gold01.ogg";
        case NotifySound::Crystal: return "crystal01.ogg";
        case NotifySound::Achievement: return "achievement_01.ogg";
        default: return nullptr;
    }
}

int savedIndex(char const* key, int fallback, int count) {
    auto value = Mod::get()->getSavedValue<int64_t>(key, fallback);
    return static_cast<int>(std::clamp<int64_t>(value, 0, count - 1));
}

float savedFloat(char const* key, float fallback, float minV, float maxV) {
    auto value = Mod::get()->getSavedValue<double>(key, fallback);
    return std::clamp(static_cast<float>(value), minV, maxV);
}

// Live notices in display order. OverlayManager owns the pointers; cards release
// their slot on destruction, and the list is filtered before teardown can race it.
std::vector<CCNode*>& liveCards() {
    static auto* cards = new std::vector<CCNode*>();
    return *cards;
}

void releaseSlot(CCNode* card) {
    auto& cards = liveCards();
    for (auto& entry : cards) {
        if (entry == card) entry = nullptr;
    }
    while (!cards.empty() && !cards.back()) cards.pop_back();
}

int claimSlot(CCNode* card) {
    auto& cards = liveCards();
    for (size_t index = 0; index < cards.size(); ++index) {
        if (!cards[index]) {
            cards[index] = card;
            return static_cast<int>(index);
        }
    }
    cards.push_back(card);
    return static_cast<int>(cards.size() - 1);
}

// Notice card that releases its stack slot on destruction.
class NotifyCard : public CCNodeRGBA {
public:
    static NotifyCard* create() {
        auto* ret = new NotifyCard();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    ~NotifyCard() override { releaseSlot(this); }
};

// Entry/exit direction follows the card's side of the screen.
CCPoint edgeOffset(NotifySpot spot, CCSize scaled) {
    int const index = static_cast<int>(spot);
    int const column = index % 3;
    int const row = index / 3;

    if (column == 0) return {-(scaled.width + kScreenMargin * 2.f), 0.f};
    if (column == 2) return {scaled.width + kScreenMargin * 2.f, 0.f};
    float const vertical = scaled.height + kScreenMargin * 2.f;
    return {0.f, row == 2 ? -vertical : vertical};
}

CCSize scaledSize(CCNodeRGBA* card) {
    auto const size = card->getContentSize();
    float const scale = card->getScale();
    return {size.width * scale, size.height * scale};
}

void addLine(CCNodeRGBA* card, CCLabelBMFont* label, float x, float y) {
    label->setAnchorPoint({0.f, 0.5f});
    label->setPosition({x, y});
    card->addChild(label, 2);
}

// Add a card to the overlay and schedule its exit; copy config for stability.
void presentCard(CCNodeRGBA* card, NotifyConfig config) {
    auto* overlay = OverlayManager::get();
    if (!card || !overlay || paimon::isRuntimeShuttingDown()) return;

    int alive = 0;
    CCNode* oldest = nullptr;
    for (auto* entry : liveCards()) {
        if (!entry) continue;
        ++alive;
        if (!oldest) oldest = entry;
    }
// Evict the oldest immediately when the stack is full.
    if (alive >= kMaxLive && oldest) {
        releaseSlot(oldest);
        oldest->removeFromParent();
    }

    int const slot = claimSlot(card);
    auto const rest = notifyRestPoint(config, card->getContentSize(), slot);

    card->setScale(std::clamp(config.scale, kNotifyMinScale, kNotifyMaxScale));
    card->setPosition(rest);
    overlay->addChild(card, 100);

    runNotifyEnter(card, config, rest);
    playNotifySound(config);

    float const hold = std::clamp(config.seconds, kNotifyMinSeconds, kNotifyMaxSeconds);
    Ref<CCNodeRGBA> ref = card;
    paimon::scheduleMainThreadDelay(notifyEnterSeconds(config) + hold, [ref, config, rest] {
        if (!ref || !ref->getParent()) return;
        runNotifyExit(ref, config, rest, [ref] {
// Run outside the action callback so the active node is not removed mid-walk.
            Loader::get()->queueInMainThread([ref] {
                if (!ref) return;
                releaseSlot(ref);
                ref->removeFromParent();
            });
        });
    });
}

bool requestsLayerOnScreen() {
    auto* scene = CCDirector::get()->getRunningScene();
    return scene && scene->getChildByID("twitch-requests-layer"_spr);
}

}

std::vector<std::string> notifySpotNames() {
    return {
        "Arriba izq.", "Arriba centro", "Arriba der.",
        "Medio izq.", "Centro", "Medio der.",
        "Abajo izq.", "Abajo centro", "Abajo der.",
    };
}

std::vector<std::string> notifyEnterNames() {
    return {"Sin animacion", "Deslizar", "Aparecer", "Estallido", "Caer", "Girar"};
}

std::vector<std::string> notifyExitNames() {
    return {"Sin animacion", "Deslizar", "Desvanecer", "Encoger", "Subir", "Girar"};
}

std::vector<std::string> notifySoundNames() {
    return {"Sin sonido", "Suave", "Moneda", "Cristal", "Logro"};
}

ccColor3B platformAccent(Platform platform) {
    switch (platform) {
        case Platform::YouTube: return {255, 90, 90};
        case Platform::Kick: return {110, 235, 110};
        case Platform::TikTok: return {80, 225, 235};
        case Platform::Web: return {90, 175, 255};
        default: return {145, 90, 255};
    }
}

NotifyConfig const& notifyConfig() {
    return g_config;
}

void loadNotifyConfig() {
    NotifyConfig config;
    config.enabled = Mod::get()->getSavedValue<bool>(kEnabledKey, false);
    config.spot = static_cast<NotifySpot>(savedIndex(
        kSpotKey, static_cast<int>(NotifySpot::TopRight), kNotifySpotCount));
    config.offsetX = savedFloat(kOffsetXKey, 0.f, -kNotifyMaxOffsetX, kNotifyMaxOffsetX);
    config.offsetY = savedFloat(kOffsetYKey, 0.f, -kNotifyMaxOffsetY, kNotifyMaxOffsetY);
    config.scale = savedFloat(kScaleKey, 1.f, kNotifyMinScale, kNotifyMaxScale);
    config.seconds = savedFloat(kSecondsKey, 3.f, kNotifyMinSeconds, kNotifyMaxSeconds);
    config.enter = static_cast<NotifyEnter>(savedIndex(
        kEnterKey, static_cast<int>(NotifyEnter::Slide), kNotifyEnterCount));
    config.exit = static_cast<NotifyExit>(savedIndex(
        kExitKey, static_cast<int>(NotifyExit::Fade), kNotifyExitCount));
    config.sound = static_cast<NotifySound>(savedIndex(
        kSoundKey, static_cast<int>(NotifySound::Soft), kNotifySoundCount));
    config.showLevel = Mod::get()->getSavedValue<bool>(kShowLevelKey, true);
    config.showRequester = Mod::get()->getSavedValue<bool>(kShowRequesterKey, true);
    config.overLayer = Mod::get()->getSavedValue<bool>(kOverLayerKey, false);
    g_config = config;
}

void setNotifyConfig(NotifyConfig config) {
    config.offsetX = std::clamp(config.offsetX, -kNotifyMaxOffsetX, kNotifyMaxOffsetX);
    config.offsetY = std::clamp(config.offsetY, -kNotifyMaxOffsetY, kNotifyMaxOffsetY);
    config.scale = std::clamp(config.scale, kNotifyMinScale, kNotifyMaxScale);
    config.seconds = std::clamp(config.seconds, kNotifyMinSeconds, kNotifyMaxSeconds);
    g_config = config;

    Mod::get()->setSavedValue<bool>(kEnabledKey, config.enabled);
    Mod::get()->setSavedValue<int64_t>(kSpotKey, static_cast<int64_t>(config.spot));
    Mod::get()->setSavedValue<double>(kOffsetXKey, config.offsetX);
    Mod::get()->setSavedValue<double>(kOffsetYKey, config.offsetY);
    Mod::get()->setSavedValue<double>(kScaleKey, config.scale);
    Mod::get()->setSavedValue<double>(kSecondsKey, config.seconds);
    Mod::get()->setSavedValue<int64_t>(kEnterKey, static_cast<int64_t>(config.enter));
    Mod::get()->setSavedValue<int64_t>(kExitKey, static_cast<int64_t>(config.exit));
    Mod::get()->setSavedValue<int64_t>(kSoundKey, static_cast<int64_t>(config.sound));
    Mod::get()->setSavedValue<bool>(kShowLevelKey, config.showLevel);
    Mod::get()->setSavedValue<bool>(kShowRequesterKey, config.showRequester);
    Mod::get()->setSavedValue<bool>(kOverLayerKey, config.overLayer);
    paimon::requestDeferredModSave();
}

CCNodeRGBA* buildNotifyCard(
    NotifyConfig const& config,
    Platform platform,
    std::string levelName,
    int levelID,
    std::string requester
) {
    auto* card = NotifyCard::create();
    if (!card) return nullptr;
    card->setAnchorPoint({0.5f, 0.5f});
    card->ignoreAnchorPointForPosition(false);
// Fade the whole card contents together.
    card->setCascadeOpacityEnabled(true);

    auto const accent = platformAccent(platform);
    float const textLeft = kIconZone;
    float const textWidth = kCardWidth - textLeft - kCardPad;

    std::vector<CCLabelBMFont*> lines;

// Keep the source chat ID when multiple chats are active.
    auto const heading = TwitchRequestManager::get().activeCount() > 1
        ? fmt::format("Nuevo request - {}", platformName(platform))
        : std::string("Nuevo request");
    auto* title = CCLabelBMFont::create(heading.c_str(), "goldFont.fnt");
    title->limitLabelWidth(textWidth, 0.44f, 0.24f);
    lines.push_back(title);

    if (config.showLevel) {
        auto const text = levelName.empty() ? fmt::format("ID {}", levelID) : levelName;
        auto* level = CCLabelBMFont::create(shorten(text, 28).c_str(), "bigFont.fnt");
        level->limitLabelWidth(textWidth, 0.42f, 0.22f);
        lines.push_back(level);
    }

    if (config.showRequester) {
        auto meta = "@" + shorten(requester, 18);
// The ID is needed only when the level name is shown.
        if (config.showLevel && !levelName.empty()) meta += fmt::format("  -  ID {}", levelID);
        auto* who = CCLabelBMFont::create(meta.c_str(), "chatFont.fnt");
        who->limitLabelWidth(textWidth, 0.36f, 0.2f);
        who->setColor(accent);
        lines.push_back(who);
    }

    float textHeight = 0.f;
    for (auto* line : lines) textHeight += line->getScaledContentSize().height;
    textHeight += kLineGap * static_cast<float>(std::max<int>(
        static_cast<int>(lines.size()) - 1, 0));

    float const height = std::max(38.f, textHeight + kCardPad * 2.f);
    card->setContentSize({kCardWidth, height});

    CCNode* plate = nullptr;
    if (auto* s9 = paimon::SpriteHelper::safeCreateScale9("GJ_square05.png")) {
        s9->setContentSize({kCardWidth, height});
        s9->setAnchorPoint({0.f, 0.f});
        s9->setOpacity(248);
        plate = s9;
    } else if (auto* panel = paimon::SpriteHelper::createColorPanel(
            kCardWidth, height, {12, 20, 44}, 235, 8.f)) {
        panel->setAnchorPoint({0.f, 0.f});
        plate = panel;
    }
    if (plate) {
        plate->setPosition({0.f, 0.f});
        card->addChild(plate, -1);
    }

    if (auto* icon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_chatBtn_001.png")) {
        icon->setScale(26.f / std::max(icon->getContentSize().height, 1.f));
        icon->setColor(accent);
        icon->setPosition({kCardPad + 13.f, height / 2.f});
        card->addChild(icon, 2);
    }

    float y = height - kCardPad;
    for (auto* line : lines) {
        float const lineHeight = line->getScaledContentSize().height;
        y -= lineHeight / 2.f;
        addLine(card, line, textLeft, y);
        y -= lineHeight / 2.f + kLineGap;
    }

    return card;
}

CCPoint notifyRestPoint(NotifyConfig const& config, CCSize card, int slot) {
    auto const win = CCDirector::get()->getWinSize();
    float const scale = std::clamp(config.scale, kNotifyMinScale, kNotifyMaxScale);
    float const halfW = card.width * scale / 2.f;
    float const halfH = card.height * scale / 2.f;

    int const index = static_cast<int>(config.spot);
    int const column = index % 3;
    int const row = index / 3;

    float x = column == 0 ? kScreenMargin + halfW
        : column == 1 ? win.width / 2.f
        : win.width - kScreenMargin - halfW;
    float y = row == 0 ? win.height - kScreenMargin - halfH
        : row == 1 ? win.height / 2.f
        : kScreenMargin + halfH;

// Fine positioning must keep the card on screen.
    x = std::clamp(x + config.offsetX, halfW, std::max(halfW, win.width - halfW));
    y = std::clamp(y + config.offsetY, halfH, std::max(halfH, win.height - halfH));

    float const step = card.height * scale + kStackGap;
    y += row == 0 ? -step * static_cast<float>(slot) : step * static_cast<float>(slot);
    return {x, y};
}

float notifyEnterSeconds(NotifyConfig const& config) {
    if (!motionOn()) return 0.f;
    switch (config.enter) {
        case NotifyEnter::Slide: return animTime(0.5f);
        case NotifyEnter::Fade: return animTime(0.35f);
        case NotifyEnter::Pop: return animTime(0.45f);
        case NotifyEnter::Drop: return animTime(0.75f);
        case NotifyEnter::Spin: return animTime(0.5f);
        default: return 0.f;
    }
}

void runNotifyEnter(CCNodeRGBA* card, NotifyConfig const& config, CCPoint rest) {
    if (!card) return;

    float const scale = card->getScale();
    auto const scaled = scaledSize(card);

    card->stopAllActions();
    card->setPosition(rest);
    card->setScale(scale);
    card->setRotation(0.f);
    card->setOpacity(255);
    if (!motionOn()) return;

    switch (config.enter) {
        case NotifyEnter::Slide: {
            card->setPosition(rest + edgeOffset(config.spot, scaled));
            card->runAction(CCEaseExponentialOut::create(
                CCMoveTo::create(animTime(0.5f), rest)));
            break;
        }
        case NotifyEnter::Fade: {
            card->setOpacity(0);
            card->runAction(CCFadeTo::create(animTime(0.35f), 255));
            break;
        }
        case NotifyEnter::Pop: {
            card->setScale(scale * 0.35f);
            card->setOpacity(0);
            card->runAction(CCSpawn::create(
                CCEaseBackOut::create(CCScaleTo::create(animTime(0.45f), scale)),
                CCFadeTo::create(animTime(0.2f), 255),
                nullptr));
            break;
        }
        case NotifyEnter::Drop: {
            card->setPosition({rest.x, rest.y + scaled.height * 2.f + 40.f});
            card->runAction(CCEaseBounceOut::create(
                CCMoveTo::create(animTime(0.75f), rest)));
            break;
        }
        case NotifyEnter::Spin: {
            card->setScale(scale * 0.2f);
            card->setRotation(-160.f);
            card->runAction(CCSpawn::create(
                CCEaseBackOut::create(CCScaleTo::create(animTime(0.5f), scale)),
                CCEaseSineOut::create(CCRotateTo::create(animTime(0.5f), 0.f)),
                nullptr));
            break;
        }
        default: break;
    }
}

void runNotifyExit(
    CCNodeRGBA* card,
    NotifyConfig const& config,
    CCPoint rest,
    std::function<void()> onDone
) {
    if (!card) {
        if (onDone) onDone();
        return;
    }

    float const scale = card->getScale();
    auto const scaled = scaledSize(card);
    card->stopAllActions();
    card->setPosition(rest);
    card->setScale(scale);

    auto* finish = CallFuncExt::create([onDone = std::move(onDone)] {
        if (onDone) onDone();
    });
    if (!motionOn() || config.exit == NotifyExit::None) {
        card->runAction(finish);
        return;
    }

    CCFiniteTimeAction* leave = nullptr;
    switch (config.exit) {
        case NotifyExit::Slide: {
            leave = CCEaseBackIn::create(CCMoveTo::create(
                animTime(0.45f), rest + edgeOffset(config.spot, scaled)));
            break;
        }
        case NotifyExit::Shrink: {
            leave = CCSpawn::create(
                CCEaseBackIn::create(CCScaleTo::create(animTime(0.35f), scale * 0.1f)),
                CCFadeTo::create(animTime(0.35f), 0),
                nullptr);
            break;
        }
        case NotifyExit::Rise: {
            leave = CCSpawn::create(
                CCEaseSineIn::create(CCMoveTo::create(
                    animTime(0.4f), {rest.x, rest.y + scaled.height})),
                CCFadeTo::create(animTime(0.4f), 0),
                nullptr);
            break;
        }
        case NotifyExit::Spin: {
            leave = CCSpawn::create(
                CCEaseBackIn::create(CCScaleTo::create(animTime(0.45f), scale * 0.1f)),
                CCRotateTo::create(animTime(0.45f), 160.f),
                nullptr);
            break;
        }
        default: {
            leave = CCFadeTo::create(animTime(0.4f), 0);
            break;
        }
    }

    card->runAction(CCSequence::create(leave, finish, nullptr));
}

void playNotifySound(NotifyConfig const& config) {
    char const* file = soundFile(config.sound);
    if (!file) return;
    if (auto* engine = FMODAudioEngine::sharedEngine()) engine->playEffect(file);
}

void showRequestNotify(LevelRequest const& request) {
    auto const& config = notifyConfig();
    if (!config.enabled || paimon::isRuntimeShuttingDown()) return;
// The notice covers only the existing list.
    if (!config.overLayer && requestsLayerOnScreen()) return;

// Use cached data; fetching here would steal GameLevelManager's delegate.
    std::string name;
    if (auto const* brief = TwitchLevelBriefCache::get().peek(request.levelID);
        brief && brief->found) {
        name = brief->name;
    }

    presentCard(
        buildNotifyCard(config, request.platform, name, request.levelID, request.requester),
        config);
}

void showNotifyDemo() {
    auto const& config = notifyConfig();
    presentCard(
        buildNotifyCard(config, TwitchRequestManager::get().selected(),
            "Nivel de ejemplo", 12345, "tu_chat"),
        config);
}

}
