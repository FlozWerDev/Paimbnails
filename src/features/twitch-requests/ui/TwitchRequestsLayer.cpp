#include "TwitchRequestsLayer.hpp"

#include "TwitchFiltersPopup.hpp"
#include "TwitchMessagePopup.hpp"
#include "TwitchNotifyPopup.hpp"
#include "StreamOverlayPopup.hpp"
#include "RequestSourcesPopup.hpp"
#include "../TwitchRequestFilters.hpp"
#include "../TwitchRequestManager.hpp"
#include "../TwitchRequestNotify.hpp"
#include "../TwitchRequestParser.hpp"
#include "../services/TwitchLevelBriefCache.hpp"
#include "../services/TwitchLevelOpen.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/Settings.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/GJDifficultySprite.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/utils/general.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>

using namespace geode::prelude;

namespace kit = paimon::configkit;

namespace paimon::twitch {

namespace {

constexpr float kMargin = 12.f;
constexpr float kSideWidth = 204.f;
constexpr float kRowHeight = 60.f;
constexpr float kRowGap = 6.f;
// two side buttons must fit in half the inner width each.
constexpr float kSideBtnScale = 0.42f;
constexpr float kStatsCardBottom = 8.f;
constexpr float kStatsCardHeight = 28.f;
constexpr float kStatColumnW = (kSideWidth - 28.f) / 4.f;
constexpr float kStatValueScale = 0.36f;
constexpr float kHintScale = 0.34f;

constexpr char const* kQueueOpenBg = "GJ_button_01.png";
constexpr char const* kQueueClosedBg = "GJ_button_06.png";

constexpr int kTintTag = 0x7a1;

constexpr ccColor3B kGold = {255, 205, 61};
constexpr ccColor3B kDesc = {171, 197, 232};

void runEnter(CCNode* node, CCPoint target, float delay, bool bounce) {
    auto* move = CCMoveTo::create(animTime(bounce ? 0.4f : 0.5f), target);
    CCActionInterval* eased = bounce
        ? static_cast<CCActionInterval*>(CCEaseBackOut::create(move))
        : CCEaseExponentialOut::create(move);
    node->runAction(CCSequence::create(CCDelayTime::create(delay), eased, nullptr));
}

void pulse(CCNode* node, float base) {
    if (!node || !motionOn()) return;
    node->stopAllActions();
    node->setScale(base);
    node->runAction(CCSequence::create(
        CCEaseSineOut::create(CCScaleTo::create(animTime(0.08f), base * 1.12f)),
        CCEaseBackOut::create(CCScaleTo::create(animTime(0.16f), base)),
        nullptr
    ));
}

void tintTo(CCSprite* node, ccColor3B color) {
    if (!node) return;
    if (!motionOn()) {
        node->setColor(color);
        return;
    }
    node->stopActionByTag(kTintTag);
    auto* tint = CCTintTo::create(animTime(0.3f), color.r, color.g, color.b);
    tint->setTag(kTintTag);
    node->runAction(tint);
}

void breathe(CCNode* node, float from, float to, float seconds) {
    if (!node || !motionOn()) return;
    node->stopAllActions();
    node->setScale(from);
    node->runAction(CCRepeatForever::create(CCSequence::create(
        CCEaseSineInOut::create(CCScaleTo::create(animTime(seconds), to)),
        CCEaseSineInOut::create(CCScaleTo::create(animTime(seconds), from)),
        nullptr
    )));
}

ccColor3B backdropColor(Platform platform) {
    switch (platform) {
        case Platform::YouTube: return {96, 26, 34};
        case Platform::Kick: return {26, 80, 40};
        case Platform::TikTok: return {24, 62, 90};
        case Platform::Web: return {26, 70, 120};
        default: return {58, 26, 110};
    }
}

ccColor3B currentAccent() {
    return platformAccent(TwitchRequestManager::get().selected());
}

std::string shortPlatform(Platform platform) {
    return platform == Platform::YouTube ? "YT" : platformName(platform);
}

char const* shortState(ConnectionState state) {
    switch (state) {
        case ConnectionState::Connected: return "ok";
        case ConnectionState::Connecting: return "...";
        case ConnectionState::Error: return "error";
        case ConnectionState::Offline: return "pausa";
        default: return "off";
    }
}

std::string chatSummary() {
    auto& manager = TwitchRequestManager::get();
    std::string summary;
    for (int index = 0; index < kSelectableCount; ++index) {
        auto platform = platformFromIndex(index);
        if (!manager.isActive(platform)) continue;
        if (!summary.empty()) summary += ", ";
        summary += shortPlatform(platform);
        summary += ' ';
        summary += shortState(manager.state(platform));
    }
    return summary;
}

int childTouchPrio() {
    return CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2;
}

CCNode* makeWindow(CCSize size) {
    if (auto* s9 = paimon::SpriteHelper::safeCreateScale9("GJ_square01.png")) {
        s9->setContentSize(size);
        s9->setAnchorPoint({0.f, 0.f});
        return s9;
    }
    return paimon::SpriteHelper::createColorPanel(
        size.width, size.height, {14, 24, 52}, 225, 8.f);
}

CCNode* makePlate(float width, float height, GLubyte opacity = 255) {
    if (auto* s9 = paimon::SpriteHelper::safeCreateScale9("GJ_square05.png")) {
        s9->setContentSize({width, height});
        s9->setAnchorPoint({0.f, 0.f});
        s9->setOpacity(opacity);
        return s9;
    }
    auto* fallback = paimon::SpriteHelper::createColorPanel(
        width, height, {40, 58, 96}, opacity, 5.f);
    if (fallback) fallback->setAnchorPoint({0.f, 0.f});
    return fallback;
}

CCLabelBMFont* makeCaption(char const* text) {
    auto* label = CCLabelBMFont::create(text, "goldFont.fnt");
    label->setAnchorPoint({0.f, 0.5f});
    label->setScale(0.4f);
    return label;
}

CCMenuItemSpriteExtra* makeCircleButton(
    char const* frameName,
    CircleBaseColor color,
    float scale,
    std::function<void()> action,
    float rotation = 0.f
) {
    CircleButtonSprite* sprite = nullptr;
    if (rotation == 0.f) {
        sprite = CircleButtonSprite::createWithSpriteFrameName(
            frameName, 1.f, color, CircleBaseSize::Small);
    } else if (auto* icon = paimon::SpriteHelper::safeCreateWithFrameName(frameName)) {
        icon->setRotation(rotation);
        auto* host = CCNode::create();
        auto const iconSize = icon->getContentSize();
        host->setContentSize({iconSize.height, iconSize.width});
        host->setAnchorPoint({0.5f, 0.5f});
        host->ignoreAnchorPointForPosition(false);
        icon->setPosition(host->getContentSize() / 2.f);
        host->addChild(icon);
        sprite = CircleButtonSprite::create(host, color, CircleBaseSize::Small);
    }
    if (!sprite) return nullptr;

    sprite->setScale(scale);
    return CCMenuItemExt::createSpriteExtra(sprite,
        [action = std::move(action)](CCMenuItemSpriteExtra*) {
            if (action) action();
        });
}

CCMenuItemSpriteExtra* makeIconButton(
    char const* frameName,
    float targetHeight,
    std::function<void()> action
) {
    auto* icon = paimon::SpriteHelper::safeCreateWithFrameName(frameName);
    if (!icon) return nullptr;
    icon->setScale(targetHeight / std::max(icon->getContentSize().height, 1.f));
    return CCMenuItemExt::createSpriteExtra(icon,
        [action = std::move(action)](CCMenuItemSpriteExtra*) {
            if (action) action();
        });
}

CCMenuItemSpriteExtra* makeTextButton(
    char const* text,
    char const* texture,
    float scale,
    std::function<void()> action,
    ButtonSprite** outSprite = nullptr
) {
    auto* sprite = ButtonSprite::create(text, "goldFont.fnt", texture, 0.8f);
    if (!sprite) return nullptr;
    sprite->setScale(scale);
    if (outSprite) *outSprite = sprite;
    return CCMenuItemExt::createSpriteExtra(sprite,
        [action = std::move(action)](CCMenuItemSpriteExtra*) {
            if (action) action();
        });
}

// truncate text to its slot; shrinking below gd's readable scale is worse.
bool fitLabel(CCLabelBMFont* label, std::string text, float room, float scale) {
    label->setScale(scale);
    label->setString(text.c_str());
    if (label->getScaledContentSize().width <= room) return true;
    if (room <= 0.f) return false;

    size_t keep = std::min<size_t>(text.size(), static_cast<size_t>(
        static_cast<float>(text.size()) * room
            / std::max(label->getScaledContentSize().width, 1.f)));
    while (keep > 0 && keep < text.size()
        && (static_cast<unsigned char>(text[keep]) & 0xC0) == 0x80) --keep;
    while (keep > 2) {
        label->setString((text.substr(0, keep) + "...").c_str());
        if (label->getScaledContentSize().width <= room) return true;
        --keep;
        while (keep > 0 && (static_cast<unsigned char>(text[keep]) & 0xC0) == 0x80) --keep;
    }
    return false;
}

ccColor3B stateColor(ConnectionState state) {
    switch (state) {
        case ConnectionState::Connected: return {95, 235, 135};
        case ConnectionState::Connecting: return {255, 210, 95};
        case ConnectionState::Error: return {255, 105, 105};
        default: return {155, 160, 175};
    }
}

int levelPercent(int levelID) {
    if (auto* glm = GameLevelManager::get()) {
        if (auto* saved = glm->getSavedLevel(levelID)) {
            return saved->m_normalPercent.value();
        }
    }
    return 0;
}

int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

// compact "hace 3m" style relative time; empty when the stamp is missing.
std::string timeAgo(int64_t receivedAt) {
    if (receivedAt <= 0) return {};
    int64_t secs = nowSeconds() - receivedAt;
    if (secs < 0) secs = 0;
    if (secs < 60) return "hace " + std::to_string(secs) + "s";
    int64_t mins = secs / 60;
    if (mins < 60) return "hace " + std::to_string(mins) + "m";
    int64_t hours = mins / 60;
    if (hours < 24) return "hace " + std::to_string(hours) + "h";
    return "hace " + std::to_string(hours / 24) + "d";
}

std::string waitLabel(double seconds) {
    if (seconds < 1.0) return "0s";
    int64_t s = static_cast<int64_t>(seconds + 0.5);
    if (s < 60) return std::to_string(s) + "s";
    int64_t m = s / 60;
    if (m < 60) return std::to_string(m) + "m " + std::to_string(s % 60) + "s";
    return std::to_string(m / 60) + "h " + std::to_string(m % 60) + "m";
}

// case-insensitive substring, used by the queue search box.
bool matchesQuery(std::string const& haystack, std::string const& needle) {
    if (needle.empty()) return true;
    auto lower = [](std::string value) {
        std::ranges::transform(value, value.begin(),
            [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return value;
    };
    return lower(haystack).find(lower(needle)) != std::string::npos;
}

std::string firstCommand() {
    auto& manager = TwitchRequestManager::get();
    return parseCommands(routedCommands(manager.routing(), manager.selected(), manager.commandsSetting())).front();
}

bool popupOnTop() {
    auto* scene = CCDirector::get()->getRunningScene();
    if (!scene) return false;
    if (auto* children = scene->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(children)) {
            if (typeinfo_cast<FLAlertLayer*>(child)) return true;
        }
    }
    return false;
}

}

TwitchRequestsLayer* TwitchRequestsLayer::create() {
    auto* ret = new TwitchRequestsLayer();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

void TwitchRequestsLayer::open() {
    if (auto* layer = TwitchRequestsLayer::create()) {
        geode::pushSceneWithLayer(layer);
    }
}

bool TwitchRequestsLayer::init() {
    if (!CCLayer::init()) return false;
    setKeypadEnabled(true);
    setID("twitch-requests-layer"_spr);

    buildBackground();
    buildHeader();
    buildSidePanel();
    buildQueuePanel();
    buildFooter();

    applyPlatformSkin();
    refreshStatus();
    refreshStats();
    refreshSourceChips();
    refreshNowPlaying();
    rebuildRows();
    schedule(schedule_selector(TwitchRequestsLayer::tick), 0.4f);
    scheduleUpdate();
    return true;
}

void TwitchRequestsLayer::onEnterTransitionDidFinish() {
    CCLayer::onEnterTransitionDidFinish();
    runIntro();
}

void TwitchRequestsLayer::enterBy(CCNode* node, CCPoint offset, float delay, bool bounce) {
    if (!node || !motionOn()) return;

    auto const target = node->getPosition();
    node->setPosition(target + offset);
    if (m_introDone) {
        runEnter(node, target, delay, bounce);
        return;
    }
    m_intro.push_back({node, target, delay, bounce});
}

void TwitchRequestsLayer::runIntro() {
    m_introDone = true;
    for (auto const& step : m_intro) {
        if (step.node && step.node->getParent()) {
            runEnter(step.node, step.target, step.delay, step.bounce);
        }
    }
    m_intro.clear();
}

void TwitchRequestsLayer::buildBackground() {
    auto tint = backdropColor(TwitchRequestManager::get().selected());
    m_background = paimon::ui::decorateScene(this, tint, true);
}

void TwitchRequestsLayer::buildHeader() {
    auto win = CCDirector::get()->getWinSize();
    float const titleY = win.height - 21.f;

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    addChild(menu, 10);

    if (auto* arrow = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_01_001.png")) {
        arrow->setScale(0.72f);
        auto* back = CCMenuItemExt::createSpriteExtra(arrow,
            [this](CCMenuItemSpriteExtra*) { this->onBack(); });
        back->setPosition({22.f, titleY});
        menu->addChild(back);
    }

    auto* title = CCLabelBMFont::create("Level Requests", "goldFont.fnt");
    title->setScale(0.72f);
    title->setPosition({win.width / 2.f, titleY});
    addChild(title, 5);

    if (auto* chat = paimon::SpriteHelper::safeCreateWithFrameName("GJ_chatBtn_001.png")) {
        chat->setScale(0.42f);
        chat->setColor(currentAccent());
        chat->setPosition({
            win.width / 2.f - title->getScaledContentSize().width / 2.f - 16.f,
            titleY,
        });
        addChild(chat, 5);
        m_titleIcon = chat;
        breathe(chat, 0.42f, 0.46f, 1.4f);
    }

    if (auto* gear = makeCircleButton("GJ_optionsBtn_001.png", CircleBaseColor::Gray, 0.72f,
            [this] { this->onSettings(); })) {
        gear->setPosition({win.width - 24.f, titleY});
        menu->addChild(gear);
    }

    if (auto* alerts = makeCircleButton("GJ_chatBtn_02_001.png", CircleBaseColor::Green, 0.66f,
            [this] { this->onNotify(); })) {
        alerts->setPosition({58.f, titleY});
        menu->addChild(alerts);

        auto* caption = CCLabelBMFont::create("Avisos", "chatFont.fnt");
        caption->setScale(0.34f);
        caption->setColor(kDesc);
        caption->setPosition({58.f, titleY - 19.f});
        addChild(caption, 5);
        enterBy(caption, {0.f, 30.f}, 0.02f, true);
    }

    if (auto* overlay = makeTextButton("OBS", "GJ_button_04.png", 0.46f,
            [this] { this->onStreamOverlay(); })) {
        overlay->setPosition({102.f, titleY});
        menu->addChild(overlay);

        auto* caption = CCLabelBMFont::create("Overlay", "chatFont.fnt");
        caption->setScale(0.31f);
        caption->setColor(kDesc);
        caption->setPosition({102.f, titleY - 19.f});
        addChild(caption, 5);
        enterBy(caption, {0.f, 30.f}, 0.04f, true);
    }

    if (auto* platform = makeTextButton(
            platformName(TwitchRequestManager::get().selected()),
            "GJ_button_02.png", 0.52f,
            [this] { this->onPlatform(); }, &m_platformSprite)) {
        platform->setPosition({win.width - 92.f, titleY});
        menu->addChild(platform);
    }

    float const pillWidth = std::min(320.f, win.width - 120.f);
    float const pillY = win.height - 48.f;
    auto* pill = CCNode::create();
    pill->setContentSize({pillWidth, 20.f});
    pill->setPosition({(win.width - pillWidth) / 2.f, pillY - 10.f});
    addChild(pill, 5);

    if (auto* plate = paimon::ui::makeInset({pillWidth, 20.f}, 175)) {
        pill->addChild(plate, 0);
    }

    m_statusDot = CCNode::create();
    m_statusDot->setContentSize({10.f, 10.f});
    m_statusDot->setPosition({15.f, 10.f});
    pill->addChild(m_statusDot, 1);

    m_statusLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_statusLabel->setAnchorPoint({0.f, 0.5f});
    m_statusLabel->setScale(0.42f);
    m_statusLabel->setPosition({26.f, 10.f});
    pill->addChild(m_statusLabel, 1);

    m_sourceChips = CCNode::create();
    m_sourceChips->setContentSize({pillWidth, 14.f});
    m_sourceChips->setAnchorPoint({0.5f, 0.5f});
    m_sourceChips->setPosition({win.width / 2.f, pillY - 23.f});
    addChild(m_sourceChips, 5);

    float chipX = 0.f;
    for (int index = 0; index < kSelectableCount; ++index) {
        auto const platform = platformFromIndex(index);
        auto* chip = CCNode::create();
        chip->setAnchorPoint({0.f, 0.5f});
        chip->setContentSize({54.f, 14.f});

        auto* dotHost = CCNode::create();
        dotHost->setContentSize({7.f, 7.f});
        dotHost->setPosition({0.f, 7.f});
        chip->addChild(dotHost, 1);
        m_sourceDots[static_cast<size_t>(index)] = dotHost;

        auto* name = CCLabelBMFont::create(shortPlatform(platform).c_str(), "chatFont.fnt");
        name->setAnchorPoint({0.f, 0.5f});
        name->setScale(0.38f);
        name->setColor(platformAccent(platform));
        name->setPosition({11.f, 7.f});
        chip->addChild(name, 1);

        float const w = 11.f + name->getScaledContentSize().width + 10.f;
        chip->setContentSize({w, 14.f});
        chip->setPositionX(chipX);
        chipX += w;
        m_sourceChips->addChild(chip);
    }
    m_sourceChips->setContentSize({chipX, 14.f});
    m_sourceChips->setPositionX(win.width / 2.f - chipX / 2.f);

    enterBy(menu, {0.f, 34.f}, 0.f, true);
    enterBy(title, {0.f, 24.f}, 0.04f, true);
    enterBy(m_titleIcon, {0.f, 24.f}, 0.07f, true);
    enterBy(pill, {0.f, 20.f}, 0.1f);
    enterBy(m_sourceChips, {0.f, 18.f}, 0.12f);
}

void TwitchRequestsLayer::buildSidePanel() {
    auto win = CCDirector::get()->getWinSize();
    float const top = win.height - 86.f;
    float const bottom = 44.f;
    float const height = top - bottom;

    auto* panel = CCNode::create();
    panel->setContentSize({kSideWidth, height});
    panel->setPosition({kMargin, bottom});
    addChild(panel, 5);

    if (auto* window = makeWindow({kSideWidth, height})) {
        window->setPosition({0.f, 0.f});
        panel->addChild(window, -1);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    panel->addChild(menu, 5);

    float const inner = kSideWidth - 28.f;
    float const midX = kSideWidth / 2.f;

    // fixed slots from the top; the stats card owns the bottom strip.
    float const captionY = height - 14.f;
    float const inputY = height - 37.f;
    float const commandCaptionY = height - 61.f;
    float const commandInputY = height - 84.f;
    float const primaryY = height - 110.f;
    float const toggleRowY = height - 133.f;
    float const hintY = (kStatsCardBottom + kStatsCardHeight + toggleRowY - 8.f) / 2.f;

    auto& manager = TwitchRequestManager::get();
    auto const platform = manager.selected();

    m_channelCaption = makeCaption(platformFieldName(platform));
    m_channelCaption->setPosition({14.f, captionY});
    panel->addChild(m_channelCaption, 2);

    m_channelInput = TextInput::create(inner, platformPlaceholder(platform), "chatFont.fnt");
    if (m_channelInput) {
        m_channelInput->setFilter(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.@:/?=");
        m_channelInput->setMaxCharCount(120);
        m_channelInput->setString(manager.channelSetting(platform));
        m_channelInput->setPosition({midX, inputY});
        panel->addChild(m_channelInput, 3);
    }

    m_webUrlLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_webUrlLabel->setScale(0.4f);
    m_webUrlLabel->setColor(kGold);
    m_webUrlLabel->setPosition({midX, inputY});
    m_webUrlLabel->setVisible(false);
    panel->addChild(m_webUrlLabel, 3);

    m_commandCaption = makeCaption("Comandos de esta plataforma");
    m_commandCaption->setPosition({14.f, commandCaptionY});
    panel->addChild(m_commandCaption, 2);

    m_commandInput = TextInput::create(inner, "Vacio: comandos generales", "chatFont.fnt");
    if (m_commandInput) {
        m_commandInput->setMaxCharCount(400);
        m_commandInput->setString(manager.commandsSetting(platform));
        m_commandInput->setPosition({midX, commandInputY});
        panel->addChild(m_commandInput, 3);
    }

    // web has no command field, so its link buttons sit right under the url.
    m_webMenu = CCMenu::create();
    m_webMenu->setContentSize({inner, 26.f});
    m_webMenu->setPosition({midX, (inputY + commandInputY) / 2.f});
    m_webMenu->setTouchPriority(childTouchPrio());
    m_webMenu->setVisible(false);
    panel->addChild(m_webMenu, 5);

    if (auto* copy = makeTextButton("Copiar link", "GJ_button_04.png", 0.44f,
            [this] { this->onCopyWebUrl(); })) {
        m_webMenu->addChild(copy);
    }
    if (auto* open = makeTextButton("Abrir", "GJ_button_02.png", 0.44f,
            [this] { this->onOpenWebUrl(); })) {
        m_webMenu->addChild(open);
    }
    m_webMenu->setLayout(RowLayout::create()
        ->setGap(8.f)
        ->setAutoScale(false)
        ->setAxisAlignment(AxisAlignment::Center));

    if (auto* primary = makeTextButton("Guardar y conectar", "GJ_button_01.png", 0.62f,
            [this] { this->onPrimary(); }, &m_primarySprite)) {
        primary->setPosition({midX, primaryY});
        menu->addChild(primary);
    }

    // queue + order share one row; stacked they ran into the stats card.
    float const slotW = inner / 2.f;
    if (auto* queueToggle = makeTextButton("Cerrar requests",
            manager.isAccepting() ? kQueueOpenBg : kQueueClosedBg, kSideBtnScale,
            [this] { this->onToggleQueue(); }, &m_queueSprite)) {
        queueToggle->setPosition({14.f + slotW / 2.f, toggleRowY});
        menu->addChild(queueToggle);
    }

    if (auto* order = makeTextButton("Orden: aleatorio", "GJ_button_02.png", kSideBtnScale,
            [this] { this->onToggleOrder(); }, &m_orderSprite)) {
        order->setPosition({14.f + slotW * 1.5f, toggleRowY});
        menu->addChild(order);
    }

    m_hintLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_hintLabel->setScale(kHintScale);
    m_hintLabel->setColor(kDesc);
    m_hintLabel->setPosition({midX, hintY});
    panel->addChild(m_hintLabel, 2);

    buildStatsCard(panel, inner);

    enterBy(panel, {-kSideWidth - kMargin - 8.f, 0.f}, 0.06f);
}

void TwitchRequestsLayer::buildStatsCard(CCNode* panel, float width) {
    auto* card = CCNode::create();
    card->setContentSize({width, kStatsCardHeight});
    card->setPosition({14.f, kStatsCardBottom});
    panel->addChild(card, 2);

    if (auto* inset = paimon::ui::makeInset({width, kStatsCardHeight}, 120)) {
        card->addChild(inset, -1);
    }

    float const colW = width / 4.f;
    auto makeStat = [&](char const* caption, int col, ccColor3B color) -> CCLabelBMFont* {
        float const cx = col * colW + colW / 2.f;
        auto* cap = CCLabelBMFont::create(caption, "chatFont.fnt");
        cap->setScale(0.3f);
        cap->setColor(kDesc);
        cap->setPosition({cx, kStatsCardHeight - 8.f});
        card->addChild(cap);

        auto* value = CCLabelBMFont::create("0", "bigFont.fnt");
        value->setScale(kStatValueScale);
        value->setColor(color);
        value->setPosition({cx, 9.f});
        card->addChild(value);
        return value;
    };

    m_statReceived = makeStat("Recibidos", 0, {210, 215, 230});
    m_statPlayed = makeStat("Jugados", 1, {120, 245, 150});
    m_statSkipped = makeStat("Saltados", 2, {255, 150, 150});
    m_statWait = makeStat("Espera", 3, kGold);
}

void TwitchRequestsLayer::refreshStats() {
    auto& manager = TwitchRequestManager::get();
    auto const& stats = manager.sessionStats();
    auto setStat = [](CCLabelBMFont* label, std::string const& text) {
        if (!label) return;
        label->setString(text.c_str());
        label->limitLabelWidth(kStatColumnW - 4.f, kStatValueScale, 0.2f);
    };
    setStat(m_statReceived, std::to_string(stats.received));
    setStat(m_statPlayed, std::to_string(stats.played));
    setStat(m_statSkipped, std::to_string(stats.removed));
    setStat(m_statWait, waitLabel(manager.averageWaitSeconds()));
}

void TwitchRequestsLayer::refreshSourceChips() {
    auto& manager = TwitchRequestManager::get();
    for (int index = 0; index < kSelectableCount; ++index) {
        auto* host = m_sourceDots[static_cast<size_t>(index)];
        if (!host) continue;
        auto const platform = platformFromIndex(index);
        bool const active = manager.isActive(platform);
        auto const color = active ? stateColor(manager.state(platform))
            : ccColor3B{90, 95, 110};
        host->removeAllChildren();
        if (auto* dot = paimon::SpriteHelper::createRoundedRect(
                7.f, 7.f, 3.5f, ccc4FFromccc3B(color))) {
            dot->setPosition({-3.5f, -3.5f});
            dot->setOpacity(active ? 255 : 150);
            host->addChild(dot);
        }
    }
}

void TwitchRequestsLayer::buildQueuePanel() {
    auto win = CCDirector::get()->getWinSize();
    float const top = win.height - 86.f;
    float const bottom = 44.f;
    float const height = top - bottom;
    float const left = kMargin + kSideWidth + 10.f;
    float const width = win.width - left - kMargin;

    auto* panel = CCNode::create();
    panel->setContentSize({width, height});
    panel->setPosition({left, bottom});
    addChild(panel, 5);

    if (auto* window = makeWindow({width, height})) {
        window->setPosition({0.f, 0.f});
        panel->addChild(window, -1);
    }

    m_queueLabel = CCLabelBMFont::create("", "goldFont.fnt");
    m_queueLabel->setAnchorPoint({0.f, 0.5f});
    m_queueLabel->setScale(0.42f);
    m_queueLabel->setPosition({14.f, height - 15.f});
    panel->addChild(m_queueLabel, 2);

    auto* queueMenu = CCMenu::create();
    queueMenu->setPosition({0.f, 0.f});
    queueMenu->setTouchPriority(childTouchPrio());
    panel->addChild(queueMenu, 5);
    if (auto* select = makeTextButton("Cola: Todas", "GJ_button_04.png", .42f,
            [this] { onQueueSelection(); }, &m_queueSelectorSprite)) {
        select->setPosition({width - 65.f, height - 15.f});
        queueMenu->addChild(select);
    }

    float const searchY = height - 40.f;
    float const searchLeft = 34.f;
    if (auto* glass = paimon::SpriteHelper::safeCreateWithFrameName("gj_findBtn_001.png")) {
        glass->setScale(16.f / std::max(glass->getContentSize().height, 1.f));
        glass->setPosition({20.f, searchY});
        panel->addChild(glass, 3);
    }
    // gd's default 30pt input height crowds the header row.
    constexpr float kSearchScale = 0.8f;
    float const searchW = width - searchLeft - 10.f;
    m_searchInput = TextInput::create(searchW / kSearchScale, "Buscar nombre, ID o pedidor", "chatFont.fnt");
    if (m_searchInput) {
        m_searchInput->setMaxCharCount(60);
        m_searchInput->setScale(kSearchScale);
        m_searchInput->setPosition({searchLeft + searchW / 2.f, searchY});
        m_searchInput->setCommonFilter(CommonFilter::Any);
        m_searchInput->setCallback([this](std::string const&) { this->onSearchChanged(); });
        panel->addChild(m_searchInput, 3);
    }

    float const bannerTop = searchY - 16.f;
    m_nowPlaying = CCNode::create();
    m_nowPlaying->setContentSize({width - 20.f, 24.f});
    m_nowPlaying->setAnchorPoint({0.f, 1.f});
    m_nowPlaying->setPosition({10.f, bannerTop});
    m_nowPlaying->setVisible(false);
    panel->addChild(m_nowPlaying, 2);

    if (auto* inset = paimon::ui::makeInset({width - 20.f, 24.f}, 150, {28, 60, 30})) {
        m_nowPlaying->addChild(inset, -1);
    }
    if (auto* icon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_playBtn2_001.png")) {
        icon->setScale(16.f / std::max(icon->getContentSize().height, 1.f));
        icon->setPosition({14.f, 12.f});
        m_nowPlaying->addChild(icon, 1);
    }
    m_nowPlayingLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_nowPlayingLabel->setAnchorPoint({0.f, 0.5f});
    m_nowPlayingLabel->setScale(0.4f);
    m_nowPlayingLabel->setPosition({26.f, 12.f});
    m_nowPlaying->addChild(m_nowPlayingLabel, 1);

    m_listWidth = width - 20.f;
    m_listHeight = bannerTop - 24.f - 10.f;

    m_rowsHost = CCNode::create();
    m_rowsHost->setContentSize({m_listWidth, m_listHeight});
    m_rowsHost->setPosition({10.f, 10.f});
    panel->addChild(m_rowsHost, 1);

    enterBy(panel, {win.width - left + 8.f, 0.f}, 0.1f);
}

void TwitchRequestsLayer::refreshNowPlaying() {
    if (!m_nowPlaying || !m_nowPlayingLabel) return;

    auto& manager = TwitchRequestManager::get();
    auto requests = manager.requests();

    // the current level on stream: the newest played request, if any.
    int level = 0;
    std::string name;
    std::string requester;
    for (auto const& request : requests) {
        if (!request.played) continue;
        level = request.levelID;
        requester = request.requester;
        if (auto const* brief = TwitchLevelBriefCache::get().peek(request.levelID);
            brief && brief->found) {
            name = brief->name;
        }
    }

    if (level == 0) {
        if (m_nowPlaying->isVisible()) m_nowPlaying->setVisible(false);
        m_nowPlayingLevel = 0;
        return;
    }

    if (level == m_nowPlayingLevel && m_nowPlaying->isVisible()) return;
    m_nowPlayingLevel = level;
    m_nowPlaying->setVisible(true);

    auto const text = fmt::format("En pantalla: {}  @{}",
        name.empty() ? fmt::format("ID {}", level) : shorten(name, 24),
        shorten(requester, 14));
    m_nowPlayingLabel->setString(text.c_str());
    m_nowPlayingLabel->limitLabelWidth(m_nowPlaying->getContentSize().width - 34.f, 0.4f, 0.24f);
    pulse(m_nowPlaying, 1.f);
}

void TwitchRequestsLayer::onSearchChanged() {
    if (!m_searchInput) return;
    auto query = std::string(m_searchInput->getString());
    if (query == m_searchQuery) return;
    m_searchQuery = std::move(query);
    rebuildRows();
}

void TwitchRequestsLayer::buildFooter() {
    auto win = CCDirector::get()->getWinSize();

    float const dockWidth = win.width - 2.f * kMargin;
    auto* dock = CCNode::create();
    dock->setContentSize({dockWidth, 38.f});
    dock->setAnchorPoint({0.5f, 0.5f});
    dock->setPosition({win.width / 2.f, 22.f});
    addChild(dock, 10);

    if (auto* backing = paimon::ui::makeInset({dockWidth, 38.f}, 110)) {
        backing->setPosition({-dockWidth / 2.f, -19.f});
        dock->addChild(backing, -1);
    }

    auto* menu = CCMenu::create();
    menu->setContentSize({dockWidth - 16.f, 34.f});
    menu->setPosition({dockWidth / 2.f, 19.f});
    dock->addChild(menu);

    if (auto* next = makeTextButton("Jugar siguiente", "GJ_button_01.png", 0.56f,
            [this] { this->onPlayNext(); })) {
        menu->addChild(next);
    }
    if (auto* filters = makeTextButton("Filtros", "GJ_button_02.png", 0.5f,
            [this] { this->onFilters(); })) {
        menu->addChild(filters);
    }
    if (auto* sources = makeTextButton("Origenes", "GJ_button_04.png", .5f,
            [this] { onSources(); })) menu->addChild(sources);
    if (auto* reconnect = makeTextButton("Reconectar", "GJ_button_05.png", 0.5f,
            [this] {
                TwitchRequestManager::get().restart();
                this->refreshStatus();
            })) {
        menu->addChild(reconnect);
    }
    if (auto* clear = makeTextButton("Vaciar requests", "GJ_button_06.png", 0.5f,
            [this] { this->onClearQueue(); })) {
        menu->addChild(clear);
    }

    menu->setLayout(RowLayout::create()
        ->setGap(8.f)
        ->setAutoScale(false)
        ->setAxisAlignment(AxisAlignment::Center));

    enterBy(dock, {0.f, -52.f}, 0.16f, true);
}

void TwitchRequestsLayer::tick(float) {
    TwitchLevelBriefCache::get().tick();
    refreshPercents();
    refreshStatus();
    refreshStats();
    refreshSourceChips();
    refreshNowPlaying();
    if (m_popupOnTop) return;

    auto& manager = TwitchRequestManager::get();
    if (m_lastQueueRevision != manager.queueRevision()
        || m_lastBriefRevision != TwitchLevelBriefCache::get().revision()) {
        rebuildRows();
    }
}

void TwitchRequestsLayer::refreshStatus() {
    auto& manager = TwitchRequestManager::get();
    auto const platform = manager.selected();

    if (m_statusLabel && m_lastStatus != manager.statusText(platform)) {
        m_lastStatus = manager.statusText(platform);
        float const labelWidth = m_statusLabel->getParent()
            ? m_statusLabel->getParent()->getContentSize().width - 36.f
            : 260.f;
        m_statusLabel->setString(m_lastStatus.c_str());
        m_statusLabel->limitLabelWidth(std::max(60.f, labelWidth), 0.42f, 0.24f);

        if (m_statusDot) {
            m_statusDot->removeAllChildren();
            if (auto* dot = paimon::SpriteHelper::createRoundedRect(
                    10.f, 10.f, 5.f, ccc4FFromccc3B(stateColor(manager.state(platform))))) {
                dot->setPosition({-5.f, -5.f});
                m_statusDot->addChild(dot);
            }
        }
    }

    bool const connected = manager.state(platform) == ConnectionState::Connected;
    if (m_statusDot && connected != m_dotPulsing) {
        m_dotPulsing = connected;
        if (connected) {
            breathe(m_statusDot, 1.f, 1.2f, 0.7f);
        } else {
            m_statusDot->stopAllActions();
            m_statusDot->setScale(1.f);
        }
    }

    if (m_queueLabel) {
        auto text = fmt::format(
            "Requests: {}  -  {} sin revisar  ({}/{} total)",
            manager.selectedRequestCount(),
            manager.pendingCount(), manager.requestCount(), manager.maxQueueSize()
        );
        if (auto summary = filterSummary(manager.filters()); !summary.empty()) {
            text += "  -  " + summary;
            if (manager.filters().hasLevelFilters()) {
                text += fmt::format(" ({} ocultos)", manager.filteredCount());
            }
        }
        if (manager.activeCount() > 1) {
            text += "  -  " + chatSummary();
        }
        if (m_lastQueueText != text) {
            m_lastQueueText = text;
            m_queueLabel->stopAllActions();
            m_queueLabel->setString(text.c_str());
            m_queueLabel->limitLabelWidth(std::max(80.f, m_listWidth - 135.f), 0.42f, 0.2f);
            pulse(m_queueLabel, m_queueLabel->getScale());
        }
    }

    if (m_queueSelectorSprite) {
        auto name = manager.selectedQueue().empty() ? "Todas" : manager.selectedQueue();
        m_queueSelectorSprite->setString(("Cola: " + shorten(name, 14)).c_str());
    }

    if (m_platformSprite) {
        m_platformSprite->setString(fmt::format("{}: {}",
            platformName(platform), shortState(manager.state(platform))).c_str());
    }

    if (m_queueSprite && m_lastAccepting != manager.isAccepting()) {
        bool const accepting = manager.isAccepting();
        m_lastAccepting = accepting;
        m_queueSprite->setString(accepting ? "Cerrar requests" : "Abrir requests");
        m_queueSprite->updateBGImage(accepting ? kQueueOpenBg : kQueueClosedBg);
        pulse(m_queueSprite, kSideBtnScale);
    }

    if (m_orderSprite && m_lastRandomOrder != manager.isRandomOrder()) {
        m_lastRandomOrder = manager.isRandomOrder();
        m_orderSprite->setString(
            *m_lastRandomOrder ? "Orden: aleatorio" : "Orden: llegada");
        pulse(m_orderSprite, kSideBtnScale);
    }

    bool const web = platform == Platform::Web;
    bool const dirty = inputsDiffer();
    if (m_primarySprite) {
        char const* label = "Conectar";
        if (web) {
            label = manager.webEnabled() ? "Apagar pagina" : "Activar pagina";
        } else if (dirty) {
            label = "Guardar y conectar";
        } else if (manager.connectedCount() > 0) {
            label = "Pausar";
        } else if (manager.state(platform) == ConnectionState::Connecting) {
            label = "Conectando...";
        }
        m_primarySprite->setString(label);
    }

    if (m_webUrlLabel && web) {
        auto const url = manager.webUrl();
        m_webUrlLabel->setString(url.empty() ? "flozwer.org/request/<tu usuario>" : url.c_str());
        m_webUrlLabel->limitLabelWidth(kSideWidth - 28.f, 0.4f, 0.22f);
        m_webUrlLabel->setColor(url.empty() ? kDesc : kGold);
    }

    if (m_hintLabel) {
        std::string hint;
        if (web) {
            hint = manager.webEnabled()
                ? "Comparte tu link: los niveles entran en esta cola"
                : "Activa la pagina para recibir niveles desde la web";
        } else if (manager.isActive(platform)) {
            auto const& config = manager.routing().platforms[static_cast<size_t>(platform)];
            hint = config.commandsEnabled ? fmt::format("{} 12345 descripcion", firstCommand())
                : "Comandos apagados; configura canjes en Origenes";
        } else {
            hint = fmt::format("Vacio: {} no se lee", platformName(platform));
        }
        m_hintLabel->setString(hint.c_str());
        m_hintLabel->limitLabelWidth(kSideWidth - 28.f, kHintScale, 0.22f);
    }
}

void TwitchRequestsLayer::scheduleRebuild() {
    Ref<TwitchRequestsLayer> self = this;
    Loader::get()->queueInMainThread([self] {
        if (paimon::isRuntimeShuttingDown() || !self) return;
        if (self->getParent()) self->rebuildRows();
    });
}

void TwitchRequestsLayer::rebuildRows() {
    if (!m_rowsHost) return;

    auto& manager = TwitchRequestManager::get();
    bool const animate = m_lastQueueRevision != manager.queueRevision();
    m_lastQueueRevision = manager.queueRevision();
    m_lastBriefRevision = TwitchLevelBriefCache::get().revision();

    float const savedScroll = m_scroll && m_scroll->m_contentLayer
        ? m_scroll->m_contentLayer->getPositionY()
        : 0.f;
    bool const hadScroll = m_scroll != nullptr;

    m_rowsHost->removeAllChildren();
    m_scroll = nullptr;
    m_wheelTargetSet = false;

    auto requests = manager.requests();

    std::vector<size_t> visible;
    for (size_t index = 0; index < requests.size(); ++index) {
        if (!manager.inSelectedQueue(requests[index])) continue;
        auto passes = requestPasses(requests[index].levelID, !requests[index].videoUrl.empty());
        if (passes && !*passes) continue;
        if (!m_searchQuery.empty()) {
            auto const& request = requests[index];
            std::string name;
            if (auto const* brief = TwitchLevelBriefCache::get().peek(request.levelID);
                brief && brief->found) {
                name = brief->name;
            }
            bool const hit = matchesQuery(name, m_searchQuery)
                || matchesQuery(request.requester, m_searchQuery)
                || matchesQuery(std::to_string(request.levelID), m_searchQuery);
            if (!hit) continue;
        }
        visible.push_back(index);
    }

    if (visible.empty()) {
        bool const searching = !m_searchQuery.empty();
        bool const allFiltered = !searching && manager.selectedRequestCount() > 0;

        char const* decoFrame = searching ? "GJ_searchBtn_001.png"
            : (allFiltered ? "GJ_filterIcon_001.png" : "GJ_chatBtn_001.png");
        if (auto* deco = paimon::SpriteHelper::safeCreateWithFrameName(decoFrame)) {
            deco->setScale(0.9f);
            deco->setOpacity(120);
            deco->setColor(currentAccent());
            deco->setPosition({m_listWidth / 2.f, m_listHeight / 2.f + 34.f});
            m_rowsHost->addChild(deco);
            breathe(deco, 0.86f, 0.96f, 1.3f);
        }

        char const* emptyText = searching ? "Sin coincidencias"
            : (allFiltered ? "Nada pasa el filtro" : "Todavia no hay requests");
        auto* empty = CCLabelBMFont::create(emptyText, "bigFont.fnt");
        empty->setScale(0.5f);
        empty->setColor({210, 215, 230});
        empty->setPosition({m_listWidth / 2.f, m_listHeight / 2.f + 2.f});
        m_rowsHost->addChild(empty);
        pulse(empty, 0.5f);

        std::string hintText;
        if (searching) {
            hintText = fmt::format("Borra la busqueda \"{}\" para ver la cola", shorten(m_searchQuery, 18));
        } else if (allFiltered) {
            hintText = fmt::format("{} pedidos ocultos; toca Filtros para cambiarlos", manager.selectedRequestCount());
        } else {
            hintText = fmt::format("Escribe {} y una ID en tu chat", firstCommand());
        }
        auto* hint = CCLabelBMFont::create(hintText.c_str(), "chatFont.fnt");
        hint->setScale(0.4f);
        hint->setColor(kDesc);
        hint->setPosition({m_listWidth / 2.f, m_listHeight / 2.f - 18.f});
        m_rowsHost->addChild(hint);
        return;
    }

    m_scroll = ScrollLayer::create({m_listWidth, m_listHeight});
    if (!m_scroll) return;
    m_scroll->setPosition({0.f, 0.f});
    m_rowsHost->addChild(m_scroll);

    float const contentHeight = std::max(
        m_listHeight,
        static_cast<float>(visible.size()) * (kRowHeight + kRowGap) - kRowGap + 4.f);
    auto* content = m_scroll->m_contentLayer;
    content->setContentSize({m_listWidth, contentHeight});

    for (size_t slot = 0; slot < visible.size(); ++slot) {
        size_t const index = visible[slot];
        auto* row = buildRow(index, slot + 1, requests[index], m_listWidth);
        if (!row) continue;
        row->setPosition({
            0.f,
            contentHeight - static_cast<float>(slot + 1) * kRowHeight
                - static_cast<float>(slot) * kRowGap,
        });
        content->addChild(row);
        if (animate) {
            enterBy(row, {26.f, 0.f}, std::min(0.035f * static_cast<float>(slot), 0.3f), true);
        }
    }

    m_scroll->moveToTop();
    if (hadScroll && savedScroll != 0.f) {
        float const minY = std::min(0.f, m_listHeight - contentHeight);
        content->setPositionY(std::clamp(savedScroll, minY, 0.f));
    }
}

CCNode* TwitchRequestsLayer::buildRow(
    size_t index,
    size_t position,
    LevelRequest const& request,
    float width
) {
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, kRowHeight});

    if (auto* plate = makePlate(width, kRowHeight, request.played ? 140 : 240)) {
        plate->setPosition({0.f, 0.f});
        row->addChild(plate, -1);
    }

    if (request.played) {
        auto* check = paimon::SpriteHelper::safeCreateWithFrameName("GJ_checkOn_001.png");
        if (!check) check = paimon::SpriteHelper::safeCreateWithFrameName("GJ_completesIcon_001.png");
        if (check) {
            check->setScale(16.f / std::max(check->getContentSize().height, 1.f));
            check->setPosition({16.f, kRowHeight / 2.f + 8.f});
            row->addChild(check, 2);
        }

        auto* percent = CCLabelBMFont::create(
            fmt::format("{}%", request.percent).c_str(), "bigFont.fnt");
        percent->setScale(0.3f);
        percent->setColor({120, 245, 150});
        percent->setPosition({16.f, kRowHeight / 2.f - 9.f});
        row->addChild(percent, 2);
    } else {
        auto* label = CCLabelBMFont::create(fmt::format("{}", position).c_str(), "goldFont.fnt");
        label->setScale(0.34f);
        label->setPosition({16.f, kRowHeight / 2.f});
        row->addChild(label, 2);
    }

    auto const* brief = TwitchLevelBriefCache::get().peek(request.levelID);
    if (!brief) TwitchLevelBriefCache::get().request(request.levelID);

    if (auto* face = GJDifficultySprite::create(
            brief ? brief->difficulty : 0, GJDifficultyName::Short)) {
        face->setScale(0.52f);
        face->setPosition({38.f, kRowHeight / 2.f});
        row->addChild(face, 2);
    }

    float const textLeft = 58.f;
    auto const note = requestNote(request);
    int const buttonCount = 4 + (index > 0 ? 1 : 0)
        + (request.videoUrl.empty() ? 0 : 1)
        + (note.empty() ? 0 : 1);
    float const textWidth = width - textLeft - static_cast<float>(buttonCount) * 28.f - 8.f;

    std::string name = brief && brief->found
        ? brief->name
        : (brief ? "Nivel no encontrado" : "Cargando...");
    auto* nameLabel = CCLabelBMFont::create(shorten(name, 30).c_str(), "bigFont.fnt");
    nameLabel->setAnchorPoint({0.f, 0.5f});
    nameLabel->limitLabelWidth(textWidth, 0.48f, 0.26f);
    nameLabel->setPosition({textLeft, kRowHeight - 15.f});
    row->addChild(nameLabel, 2);

    float const starX = textLeft + nameLabel->getScaledContentSize().width + 6.f;
    if (brief && brief->stars > 0 && starX + 24.f < textLeft + textWidth) {
        auto* stars = CCLabelBMFont::create(
            fmt::format("{}", brief->stars).c_str(), "bigFont.fnt");
        stars->setAnchorPoint({0.f, 0.5f});
        stars->setScale(0.32f);
        stars->setColor(kGold);
        stars->setPosition({starX, kRowHeight - 15.f});
        row->addChild(stars, 2);

        if (auto* icon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png")) {
            icon->setScale(0.45f);
            icon->setAnchorPoint({0.f, 0.5f});
            icon->setPosition({starX + stars->getScaledContentSize().width + 2.f,
                               kRowHeight - 15.f});
            row->addChild(icon, 2);
        }
    }

    float const metaY = 27.f;

    auto requesterText = "@" + shorten(
        request.requester, request.platform == Platform::Web ? 11 : 16);
    if (request.platform == Platform::Web) {
        requesterText += request.requesterVerified ? " [V]" : " [NV]";
    }
    auto* requesterLabel = CCLabelBMFont::create(requesterText.c_str(), "bigFont.fnt");
    requesterLabel->setAnchorPoint({1.f, 0.5f});
    requesterLabel->setColor(platformAccent(request.platform));
    requesterLabel->limitLabelWidth(textWidth * 0.4f, 0.3f, 0.2f);
    requesterLabel->setPosition({textLeft + textWidth, metaY});
    row->addChild(requesterLabel, 2);

    auto* idLabel = CCLabelBMFont::create(
        fmt::format("ID {}", request.levelID).c_str(), "bigFont.fnt");
    idLabel->setAnchorPoint({0.f, 0.5f});
    idLabel->setColor(kDesc);
    idLabel->limitLabelWidth(textWidth * 0.38f, 0.26f, 0.16f);
    idLabel->setPosition({textLeft, metaY});
    row->addChild(idLabel, 2);

    float const authorX = textLeft + idLabel->getScaledContentSize().width + 7.f;
    float const authorRoom = textLeft + textWidth
        - requesterLabel->getScaledContentSize().width - 7.f - authorX;
    if (brief && !brief->author.empty()) {
        auto* authorLabel = CCLabelBMFont::create("", "goldFont.fnt");
        authorLabel->setAnchorPoint({0.f, 0.5f});
        if (fitLabel(authorLabel, brief->author, authorRoom, 0.32f)) {
            authorLabel->setPosition({authorX, metaY});
            row->addChild(authorLabel, 2);
        }
    }

    auto details = request.queue + " / " + (request.sourceName.empty() ? shortPlatform(request.platform) : request.sourceName);
    if (auto ago = timeAgo(request.receivedAt); !ago.empty()) details += " - " + ago;
    if (!note.empty()) details += " - " + note;
    auto* detailsLabel = CCLabelBMFont::create("", "chatFont.fnt");
    detailsLabel->setAnchorPoint({0.f, .5f});
    detailsLabel->setColor(kDesc);
    if (fitLabel(detailsLabel, details, textWidth, .31f)) {
        detailsLabel->setPosition({textLeft, 10.f});
        row->addChild(detailsLabel, 2);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    row->addChild(menu, 5);

    float buttonX = width - 22.f;
    auto place = [&](CCMenuItemSpriteExtra* button) {
        if (!button) return;
        button->setPosition({buttonX, kRowHeight / 2.f});
        menu->addChild(button);
        buttonX -= 28.f;
    };

    place(makeIconButton("GJ_playBtn2_001.png", 30.f,
        [this, index] { this->playRequest(index); }));

    if (!note.empty()) {
        place(makeCircleButton("GJ_infoIcon_001.png", CircleBaseColor::Blue, 0.62f,
            [this, index] { this->openMessage(index); }));
    }

    if (index > 0) {
        place(makeCircleButton("GJ_arrow_01_001.png", CircleBaseColor::Cyan, 0.62f,
            [this, index] {
                TwitchRequestManager::get().moveToFront(index);
                this->scheduleRebuild();
            }, 90.f));
    }

    if (!request.videoUrl.empty()) {
        place(makeIconButton("gj_ytIcon_001.png", 22.f,
            [this, url = request.videoUrl] { this->openVideo(url); }));
    }

    place(makeCircleButton("GJ_duplicateBtn_001.png", CircleBaseColor::Gray, 0.56f,
        [this, levelID = request.levelID] { this->copyLevelId(levelID); }));

    place(makeCircleButton(
        request.played ? "GJ_undoBtn_001.png" : "GJ_completesIcon_001.png",
        request.played ? CircleBaseColor::Gray : CircleBaseColor::Green, 0.56f,
        [this, index] { this->toggleReviewed(index); }));

    place(makeIconButton("GJ_trashBtn_001.png", 26.f,
        [this, index] {
            TwitchRequestManager::get().remove(index);
            this->scheduleRebuild();
        }));

    return row;
}

// web mode has no editable channel fields.
bool TwitchRequestsLayer::inputsDiffer() const {
    auto& manager = TwitchRequestManager::get();
    if (manager.selected() == Platform::Web) return false;
    if (m_channelInput
        && std::string(m_channelInput->getString()) != manager.channelSetting(manager.selected())) {
        return true;
    }
    if (m_commandInput && std::string(m_commandInput->getString()) != manager.commandsSetting(manager.selected())) {
        return true;
    }
    return false;
}

void TwitchRequestsLayer::applyInputs() {
    auto& manager = TwitchRequestManager::get();
    auto const platform = manager.selected();
    if (platform == Platform::Web) return;
    if (m_commandInput) manager.setCommandsSetting(platform, std::string(m_commandInput->getString()));
    if (m_channelInput) {
        manager.setChannelSetting(platform, std::string(m_channelInput->getString()));
        m_channelInput->setString(manager.channelSetting(platform));
    }
    if (m_commandInput) m_commandInput->setString(manager.commandsSetting(platform));
}

void TwitchRequestsLayer::onPrimary() {
    auto& manager = TwitchRequestManager::get();
    auto const platform = manager.selected();
    if (platform == Platform::Web) {
        onToggleWebPage();
        return;
    }

    bool const dirty = inputsDiffer();
    auto const missing = fmt::format("Escribe tu canal de {}", platformName(platform));

    if (dirty) {
        applyInputs();
        if (manager.activeCount() == 0) {
            PaimonNotify::create(missing, NotificationIcon::Warning)->show();
            refreshStatus();
            return;
        }
        manager.setLive(true);
        manager.restart();
        PaimonNotify::create(
            fmt::format("Conectando {} chat(s)...", manager.activeCount()),
            NotificationIcon::Info)->show();
        refreshStatus();
        return;
    }

    if (manager.connectedCount() > 0 || manager.state(platform) == ConnectionState::Connecting) {
        manager.setLive(false);
        PaimonNotify::create("Chats pausados", NotificationIcon::Info)->show();
        refreshStatus();
        return;
    }

    if (manager.activeCount() == 0) {
        PaimonNotify::create(missing, NotificationIcon::Warning)->show();
        return;
    }
    manager.setLive(true);
    manager.restart();
    refreshStatus();
}

void TwitchRequestsLayer::onPlatform() {
    auto& manager = TwitchRequestManager::get();
    if (inputsDiffer()) applyInputs();

    manager.select(platformFromIndex(
        (static_cast<int>(manager.selected()) + 1) % kSelectableCount));
    applyPlatformSkin();
    refreshStatus();
}

void TwitchRequestsLayer::applyPlatformSkin() {
    auto& manager = TwitchRequestManager::get();
    auto const platform = manager.selected();
    bool const web = platform == Platform::Web;

    if (m_background) tintTo(m_background, backdropColor(platform));
    if (m_titleIcon) tintTo(m_titleIcon, platformAccent(platform));
    if (m_channelCaption) m_channelCaption->setString(platformFieldName(platform));

    if (m_channelInput) {
        m_channelInput->setPlaceholder(platformPlaceholder(platform));
        m_channelInput->setString(manager.channelSetting(platform));
        m_channelInput->setVisible(!web);
        m_channelInput->setEnabled(!web);
    }
    if (m_commandInput) {
        m_commandInput->setString(manager.commandsSetting(platform));
        m_commandInput->setVisible(!web);
        m_commandInput->setEnabled(!web);
    }
    if (m_commandCaption) m_commandCaption->setVisible(!web);
    if (m_webUrlLabel) m_webUrlLabel->setVisible(web);
    if (m_webMenu) m_webMenu->setVisible(web);
}

void TwitchRequestsLayer::onToggleQueue() {
    auto& manager = TwitchRequestManager::get();
    manager.setAccepting(!manager.isAccepting());
    refreshStatus();
}

void TwitchRequestsLayer::onToggleOrder() {
    auto& manager = TwitchRequestManager::get();
    manager.setRandomOrder(!manager.isRandomOrder());
    refreshStatus();
}

void TwitchRequestsLayer::onToggleWebPage() {
    auto& manager = TwitchRequestManager::get();
    bool const enable = !manager.webEnabled();
    if (enable) manager.setLive(true);
    manager.setWebEnabled(enable);
    PaimonNotify::create(
        enable ? "Abriendo tu pagina de requests..." : "Pagina de requests apagada",
        enable ? NotificationIcon::Info : NotificationIcon::Warning)->show();
    refreshStatus();
}

void TwitchRequestsLayer::onCopyWebUrl() {
    auto const url = TwitchRequestManager::get().webUrl();
    if (url.empty()) {
        PaimonNotify::create(
            "Enciende la pagina para tener tu link", NotificationIcon::Warning)->show();
        return;
    }
    geode::utils::clipboard::write("https://" + url);
    PaimonNotify::create("Link copiado: " + url, NotificationIcon::Success)->show();
}

void TwitchRequestsLayer::onOpenWebUrl() {
    auto const url = TwitchRequestManager::get().webUrl();
    if (url.empty()) {
        PaimonNotify::create(
            "Enciende la pagina para tener tu link", NotificationIcon::Warning)->show();
        return;
    }
    web::openLinkInBrowser("https://" + url);
}

void TwitchRequestsLayer::onFilters() {
    if (auto* popup = TwitchFiltersPopup::create()) popup->show();
}

void TwitchRequestsLayer::onNotify() {
    if (auto* popup = TwitchNotifyPopup::create()) popup->show();
}

void TwitchRequestsLayer::onStreamOverlay() {
    if (auto* popup = StreamOverlayPopup::create()) popup->show();
}

void TwitchRequestsLayer::openMessage(size_t index) {
    auto requests = TwitchRequestManager::get().requests();
    if (index >= requests.size()) return;

    auto const& request = requests[index];
    if (requestNote(request).empty()) return;

    std::string name;
    std::string author;
    if (auto const* brief = TwitchLevelBriefCache::get().peek(request.levelID);
        brief && brief->found) {
        name = brief->name;
        author = brief->author;
    }

    if (auto* popup = TwitchMessagePopup::create(
            request, std::move(name), std::move(author))) {
        popup->show();
    }
}

void TwitchRequestsLayer::openVideo(std::string url) {
    if (url.empty()) return;
    if (isYouTubeUrl(url)) {
        web::openLinkInBrowser(url);
        return;
    }

    geode::createQuickPopup(
        "Abrir enlace",
        fmt::format("Del chat, sin verificar:\n<cy>{}</c>", url),
        "Cancelar", "Abrir",
        [url](auto, bool confirmed) {
            if (confirmed) web::openLinkInBrowser(url);
        }
    );
}

void TwitchRequestsLayer::onPlayNext() {
    auto& manager = TwitchRequestManager::get();
    auto index = manager.nextPendingIndex();
    if (!index) {
        char const* reason = "No quedan pedidos sin revisar";
        if (manager.selectedRequestCount() == 0) {
            reason = "No hay requests";
        } else if (manager.filteredCount() > 0) {
            reason = "Lo que queda esta fuera del filtro";
        }
        PaimonNotify::create(reason, NotificationIcon::Info)->show();
        return;
    }
    playRequest(*index);
}

void TwitchRequestsLayer::playRequest(size_t index) {
    playRequestAt(index, false);
}

void TwitchRequestsLayer::refreshPercents() {
    auto& manager = TwitchRequestManager::get();
    auto requests = manager.requests();
    for (size_t index = 0; index < requests.size(); ++index) {
        if (!requests[index].played) continue;
        int const percent = levelPercent(requests[index].levelID);
        if (percent > requests[index].percent) manager.setPercent(index, percent);
    }
}

void TwitchRequestsLayer::onClearQueue() {
    auto& manager = TwitchRequestManager::get();
    if (manager.selectedRequestCount() == 0) return;
    Ref<TwitchRequestsLayer> self = this;
    geode::createQuickPopup(
        "Vaciar requests",
        manager.selectedQueue().empty() ? "Eliminar <cr>todos</c> los niveles de todas las colas?"
            : fmt::format("Eliminar los niveles de la cola <cy>{}</c>?", manager.selectedQueue()),
        "Cancelar", "Vaciar",
        [self](auto, bool confirmed) {
            if (!confirmed) return;
            TwitchRequestManager::get().clear();
            if (!self || !self->getParent()) return;
            self->scheduleRebuild();
            self->refreshStatus();
        }
    );
}

void TwitchRequestsLayer::onSources() {
    if (inputsDiffer()) applyInputs();
    if (auto* popup = RequestSourcesPopup::create()) popup->show();
}

void TwitchRequestsLayer::onQueueSelection() {
    if (auto* popup = createRequestQueueSelector()) popup->show();
}

void TwitchRequestsLayer::onSettings() {
    geode::openSettingsPopup(Mod::get(), false);
}

void TwitchRequestsLayer::update(float dt) {
    if (!m_introDone) {
        m_introWait += dt;
        if (m_introWait > 0.9f) runIntro();
    }

    m_popupOnTop = popupOnTop();
    if (m_scroll) m_scroll->enableScrollWheel(!m_popupOnTop);
    kit::stepWheelScroll(m_scroll, m_wheelTargetY, m_wheelTargetSet, dt);
}

void TwitchRequestsLayer::scrollWheel(float x, float y) {
    if (m_popupOnTop) return;
    if (kit::queueWheelScroll(m_scroll, x, y, m_wheelTargetY, m_wheelTargetSet)) return;
    CCLayer::scrollWheel(x, y);
}

TwitchRequestsLayer::~TwitchRequestsLayer() {
    if (m_searchInput) m_searchInput->setCallback(nullptr);
    paimon::ui::detachGeodeTextInput(m_channelInput);
    paimon::ui::detachGeodeTextInput(m_commandInput);
    paimon::ui::detachGeodeTextInput(m_searchInput);
}

void TwitchRequestsLayer::keyBackClicked() {
    onBack();
}

// desktop shortcut: enter plays the next pending request while the scene is on
// top and nothing is being typed into the queue search.
void TwitchRequestsLayer::keyDown(enumKeyCodes key, double timestamp) {
    bool const searching = m_searchInput
        && !std::string(m_searchInput->getString()).empty();
    if ((key == KEY_Enter || key == KEY_NumEnter) && !m_popupOnTop && !searching) {
        onPlayNext();
        return;
    }
    CCLayer::keyDown(key, timestamp);
}

void TwitchRequestsLayer::onBack() {
    if (inputsDiffer()) applyInputs();
    CCDirector::get()->popSceneWithTransition(0.4f, PopTransition::kPopTransitionFade);
}

void TwitchRequestsLayer::copyLevelId(int levelID) {
    geode::utils::clipboard::write(std::to_string(levelID));
    PaimonNotify::create(
        fmt::format("ID {} copiado", levelID), NotificationIcon::Success)->show();
}

void TwitchRequestsLayer::toggleReviewed(size_t index) {
    auto& manager = TwitchRequestManager::get();
    auto requests = manager.requests();
    if (index >= requests.size()) return;
    bool const next = !requests[index].played;
    manager.setReviewed(index, next);
    PaimonNotify::create(
        next ? "Marcado como revisado" : "Marcado como pendiente",
        NotificationIcon::Info)->show();
    scheduleRebuild();
}

}
