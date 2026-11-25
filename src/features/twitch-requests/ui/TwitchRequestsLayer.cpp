#include "TwitchRequestsLayer.hpp"

#include "RequestWidgets.hpp"
#include "TwitchFiltersPopup.hpp"
#include "TwitchMessagePopup.hpp"
#include "TwitchNotifyPopup.hpp"
#include "StreamOverlayPopup.hpp"
#include "RequestSourcesPopup.hpp"
#include "RequestQueueToolsPopup.hpp"
#include "../../../core/ModAuthFlow.hpp"
#include "WebAccountVerificationPopup.hpp"
#include "../TwitchRequestFilters.hpp"
#include "../TwitchRequestManager.hpp"
#include "../TwitchRequestNotify.hpp"
#include "../TwitchRequestParser.hpp"
#include "../services/TwitchLevelBriefCache.hpp"
#include "../services/TwitchLevelOpen.hpp"
#include "../services/WebAccountVerification.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../core/Settings.hpp"
#include "../../../ui/PaiConfigKit.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/binding/GJDifficultySprite.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GameLevelManager.hpp>
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

// local shorthand; fully qualified paimon::ui:: keeps meaning the shared ui kit.
namespace ui = widgets;

namespace {

constexpr float kMargin = 12.f;
constexpr float kSideWidth = 204.f;
constexpr float kSidePad = 12.f;
constexpr float kPanelTopGap = 80.f;
constexpr float kPanelBottom = 44.f;
constexpr float kRowHeight = 52.f;
constexpr float kRowGap = 5.f;
constexpr float kStatsCardBottom = 6.f;
constexpr float kStatsCardHeight = 30.f;
constexpr float kStatColumnW = (kSideWidth - 2.f * kSidePad) / 4.f;
constexpr float kStatValueScale = 0.36f;
constexpr float kHintScale = 0.3f;
constexpr float kStripHeight = 20.f;
constexpr float kQueueHeaderHeight = 56.f;
constexpr float kUndoSeconds = 6.f;

constexpr int kTintTag = 0x7a1;
constexpr int kNowPlayingIconTag = 0x7a2;
constexpr char const* kRequestViewKey = "twitch-requests-list-view";

constexpr ccColor3B kGold = {255, 205, 61};
constexpr ccColor3B kDesc = {171, 197, 232};
constexpr ccColor3B kNextBar = {110, 235, 140};

enum class PrimaryTone { Green, Amber, Cyan, Red };

struct PrimaryAction {
    char const* label;
    PrimaryTone tone;
};

// what the main button in the side panel does right now.
PrimaryAction primaryAction(
    bool web, bool webEnabled, bool inputsDirty, size_t connected, ConnectionState state) {
    if (web) {
        return webEnabled ? PrimaryAction{"Apagar pagina", PrimaryTone::Red}
                          : PrimaryAction{"Activar pagina", PrimaryTone::Green};
    }
    if (inputsDirty) return {"Guardar y conectar", PrimaryTone::Green};
    if (connected > 0) return {"Pausar", PrimaryTone::Amber};
    if (state == ConnectionState::Connecting) return {"Conectando...", PrimaryTone::Cyan};
    return {"Conectar", PrimaryTone::Green};
}

ui::Tone primaryTone(PrimaryTone tone) {
    switch (tone) {
        case PrimaryTone::Amber: return ui::tone::blue;
        case PrimaryTone::Cyan: return ui::tone::cyan;
        case PrimaryTone::Red: return ui::tone::red;
        default: return ui::tone::green;
    }
}

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

// scales a control in or out; the caller tracks the logical state.
void popVisible(CCNode* node, bool visible) {
    if (!node) return;
    node->stopAllActions();
    if (!motionOn()) {
        node->setVisible(visible);
        node->setScale(1.f);
        return;
    }
    if (visible) {
        node->setVisible(true);
        node->setScale(0.f);
        node->runAction(CCEaseBackOut::create(CCScaleTo::create(animTime(0.22f), 1.f)));
        return;
    }
    node->runAction(CCSequence::create(
        CCEaseSineIn::create(CCScaleTo::create(animTime(0.12f), 0.f)),
        CCHide::create(),
        nullptr));
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

CCMenuItemSpriteExtra* makeFrameButton(
    char const* frameName, float targetHeight, std::function<void()> action) {
    auto* icon = paimon::SpriteHelper::safeCreateWithFrameName(frameName);
    if (!icon) return nullptr;
    icon->setScale(targetHeight / std::max(icon->getContentSize().height, 1.f));
    return CCMenuItemExt::createSpriteExtra(icon,
        [action = std::move(action)](CCMenuItemSpriteExtra*) {
            if (action) action();
        });
}

// stock circle button with an icon on top; a rotated icon gets a host so it stays centred.
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

std::string cachedLevelName(int levelID) {
    if (auto const* brief = TwitchLevelBriefCache::get().peek(levelID); brief && brief->found) {
        return brief->name;
    }
    return {};
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
    m_requestView = static_cast<RequestView>(std::clamp<int64_t>(
        Mod::get()->getSavedValue<int64_t>(kRequestViewKey, 0), 0, 2));

    buildBackground();
    buildHeader();
    buildSidePanel();
    buildQueuePanel();
    buildFooter();

    applyPlatformSkin();
    refreshStatus();
    refreshStats();
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

void TwitchRequestsLayer::slideIn(CCNode* node, float dx, float delay) {
    if (!node) return;
    auto const slot = std::ranges::find_if(m_restSlots,
        [node](auto const& entry) { return entry.first == node; });
    if (slot == m_restSlots.end()) return;

    node->stopAllActions();
    node->setPosition(slot->second);
    if (!motionOn()) return;
    node->setPosition(slot->second + CCPoint{dx, 0.f});
    runEnter(node, slot->second, delay, false);
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

    if (auto* back = makeFrameButton("GJ_arrow_01_001.png", 24.f, [this] { this->onBack(); })) {
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

    if (auto* gear = makeFrameButton("GJ_optionsBtn_001.png", 28.f,
            [this] { this->onSettings(); })) {
        gear->setPosition({win.width - 24.f, titleY});
        menu->addChild(gear);
    }

    ui::ButtonSpec alerts;
    alerts.label = "Avisos";
    alerts.icon = "GJ_chatBtn_02_001.png";
    alerts.width = 62.f;
    alerts.fontScale = 0.34f;
    alerts.tone = ui::tone::cyan;
    if (auto* button = ui::makeButton(alerts, [this] { this->onNotify(); })) {
        button->setID("request-alerts");
        button->setPosition({74.f, titleY});
        menu->addChild(button);
    }

#if defined(GEODE_IS_DESKTOP)
    ui::ButtonSpec overlay;
    overlay.label = "Overlay";
    overlay.width = 58.f;
    overlay.fontScale = 0.34f;
    overlay.tone = ui::tone::violet;
    if (auto* button = ui::makeButton(overlay, [this] { this->onStreamOverlay(); })) {
        button->setID("request-overlay");
        button->setPosition({136.f, titleY});
        menu->addChild(button);
    }
#endif

    ui::ButtonSpec reconnect;
    reconnect.label = "Reconectar";
    reconnect.icon = "GJ_updateBtn_001.png";
    reconnect.width = 84.f;
    reconnect.fontScale = 0.34f;
    reconnect.tone = ui::tone::blue;
    if (auto* button = ui::makeButton(reconnect, [this] {
            TwitchRequestManager::get().restart();
            this->refreshStatus();
        })) {
        button->setID("request-reconnect");
        button->setPosition({win.width - 90.f, titleY});
        menu->addChild(button);
    }

    // one tab per platform: pick it and see its connection light at once.
    float const tabWidth = std::min(330.f, win.width - 150.f);
    float const tabY = win.height - 47.f;
    std::vector<ui::SegmentBar::Item> tabs;
    for (int index = 0; index < kSelectableCount; ++index) {
        tabs.push_back({platformName(platformFromIndex(index)), true});
    }
    m_platformTabs = ui::SegmentBar::create(std::move(tabs), {tabWidth, 22.f},
        kCCMenuHandlerPriority, [this](int index) { this->onSelectPlatform(index); },
        ui::tone::green);
    if (m_platformTabs) {
        m_platformTabs->setPosition({(win.width - tabWidth) / 2.f, tabY - 11.f});
        m_platformTabs->select(static_cast<int>(TwitchRequestManager::get().selected()), false);
        addChild(m_platformTabs, 6);
    }

    float const pillWidth = std::min(330.f, win.width - 120.f);
    float const pillY = win.height - 69.f;
    auto* pill = CCNode::create();
    pill->setContentSize({pillWidth, 16.f});
    pill->setPosition({(win.width - pillWidth) / 2.f, pillY - 8.f});
    addChild(pill, 5);

    if (auto* plate = paimon::ui::makeInset({pillWidth, 16.f}, 160)) {
        pill->addChild(plate, 0);
    }

    m_statusDot = CCNode::create();
    m_statusDot->setContentSize({8.f, 8.f});
    m_statusDot->setPosition({12.f, 8.f});
    pill->addChild(m_statusDot, 1);

    m_statusLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_statusLabel->setAnchorPoint({0.f, 0.5f});
    m_statusLabel->setScale(0.4f);
    m_statusLabel->setPosition({22.f, 8.f});
    pill->addChild(m_statusLabel, 1);

    enterBy(menu, {0.f, 34.f}, 0.f, true);
    enterBy(title, {0.f, 24.f}, 0.04f, true);
    enterBy(m_titleIcon, {0.f, 24.f}, 0.07f, true);
    enterBy(m_platformTabs, {0.f, 22.f}, 0.1f, true);
    enterBy(pill, {0.f, 20.f}, 0.13f);
}

void TwitchRequestsLayer::buildSidePanel() {
    auto win = CCDirector::get()->getWinSize();
    float const height = win.height - kPanelTopGap - kPanelBottom;

    auto* panel = CCNode::create();
    panel->setContentSize({kSideWidth, height});
    panel->setPosition({kMargin, kPanelBottom});
    addChild(panel, 5);

    if (auto* window = makeWindow({kSideWidth, height})) {
        window->setPosition({0.f, 0.f});
        panel->addChild(window, -1);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    panel->addChild(menu, 5);

    float const inner = kSideWidth - 2.f * kSidePad;
    float const midX = kSideWidth / 2.f;

    // fixed slots from the top; the stats card owns the bottom strip.
    float const captionY = height - 13.f;
    float const inputY = height - 34.f;
    float const commandCaptionY = height - 58.f;
    float const commandInputY = height - 79.f;
    float const primaryY = height - 110.f;
    float const toggleY = height - 135.f;
    float const hintY = (kStatsCardBottom + kStatsCardHeight + toggleY - 10.f) / 2.f;

    auto& manager = TwitchRequestManager::get();
    auto const platform = manager.selected();

    m_channelCaption = makeCaption(platformFieldName(platform));
    m_channelCaption->setPosition({kSidePad, captionY});
    panel->addChild(m_channelCaption, 2);
    m_restSlots.emplace_back(m_channelCaption, m_channelCaption->getPosition());

    m_channelInput = TextInput::create(inner, platformPlaceholder(platform), "chatFont.fnt");
    if (m_channelInput) {
        m_channelInput->setFilter(
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.@:/?=");
        m_channelInput->setMaxCharCount(120);
        m_channelInput->setString(manager.channelSetting(platform));
        m_channelInput->setPosition({midX, inputY});
        panel->addChild(m_channelInput, 3);
        m_restSlots.emplace_back(m_channelInput, m_channelInput->getPosition());
    }

    // web mode: the public address sits in a read-only field.
    m_webUrlPlate = CCNode::create();
    m_webUrlPlate->setContentSize({inner, 24.f});
    m_webUrlPlate->setAnchorPoint({0.5f, 0.5f});
    m_webUrlPlate->ignoreAnchorPointForPosition(false);
    m_webUrlPlate->setPosition({midX, inputY});
    m_webUrlPlate->setVisible(false);
    panel->addChild(m_webUrlPlate, 3);
    if (auto* field = paimon::ui::makeInset({inner, 24.f}, 170, {8, 12, 28})) {
        m_webUrlPlate->addChild(field, 0);
    }
    m_webUrlLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_webUrlLabel->setScale(0.4f);
    m_webUrlLabel->setColor(kGold);
    m_webUrlLabel->setPosition({inner / 2.f, 12.f});
    m_webUrlPlate->addChild(m_webUrlLabel, 1);
    m_restSlots.emplace_back(m_webUrlPlate, m_webUrlPlate->getPosition());

    m_commandCaption = makeCaption("Comandos de esta plataforma");
    m_commandCaption->setPosition({kSidePad, commandCaptionY});
    panel->addChild(m_commandCaption, 2);
    m_restSlots.emplace_back(m_commandCaption, m_commandCaption->getPosition());

    m_commandInput = TextInput::create(inner, "Vacio: comandos generales", "chatFont.fnt");
    if (m_commandInput) {
        m_commandInput->setMaxCharCount(400);
        m_commandInput->setString(manager.commandsSetting(platform));
        m_commandInput->setPosition({midX, commandInputY});
        panel->addChild(m_commandInput, 3);
        m_restSlots.emplace_back(m_commandInput, m_commandInput->getPosition());
    }

    // web has no command field, so its link buttons take that room.
    float const halfW = (inner - 6.f) / 2.f;
    m_webMenu = CCMenu::create();
    m_webMenu->setContentSize({inner, 22.f});
    m_webMenu->setPosition({midX, height - 62.f});
    m_webMenu->setTouchPriority(childTouchPrio());
    m_webMenu->setVisible(false);
    panel->addChild(m_webMenu, 5);

    ui::ButtonSpec copy;
    copy.label = "Copiar link";
    copy.width = halfW;
    copy.height = 22.f;
    copy.fontScale = 0.36f;
    copy.tone = ui::tone::slate;
    if (auto* button = ui::makeButton(copy, [this] { this->onCopyWebUrl(); })) {
        m_webMenu->addChild(button);
    }
    ui::ButtonSpec open;
    open.label = "Abrir";
    open.width = halfW;
    open.height = 22.f;
    open.fontScale = 0.36f;
    open.tone = ui::tone::cyan;
    if (auto* button = ui::makeButton(open, [this] { this->onOpenWebUrl(); })) {
        m_webMenu->addChild(button);
    }
    m_webMenu->setLayout(RowLayout::create()
        ->setGap(6.f)
        ->setAutoScale(false)
        ->setAxisAlignment(AxisAlignment::Center));
    m_restSlots.emplace_back(m_webMenu, m_webMenu->getPosition());

    m_webVerificationMenu = CCMenu::create();
    m_webVerificationMenu->setPosition({midX, height - 86.f});
    m_webVerificationMenu->setContentSize({inner, 20.f});
    m_webVerificationMenu->setTouchPriority(childTouchPrio());
    m_webVerificationMenu->setVisible(false);
    panel->addChild(m_webVerificationMenu, 5);
    ui::ButtonSpec verify;
    verify.label = "Verificar cuenta";
    verify.width = inner - 26.f;
    verify.height = 20.f;
    verify.fontScale = 0.36f;
    verify.tone = ui::tone::violet;
    if (auto* button = ui::makeButton(verify, [this] { onVerifyWebAccount(); })) {
        button->setID("web-account-verification-button"_spr);
        m_webVerificationMenu->addChild(button);
    }
    if (auto* info = paimon::ui::makeInfoButton(
            "Como funciona la verificacion", webAccountVerificationInfo(), .5f)) {
        info->setID("web-account-verification-info"_spr);
        m_webVerificationMenu->addChild(info);
    }
    m_webVerificationMenu->setLayout(RowLayout::create()->setGap(6.f)->setAutoScale(false)
        ->setAxisAlignment(AxisAlignment::Center));
    m_restSlots.emplace_back(m_webVerificationMenu, m_webVerificationMenu->getPosition());

    ui::ButtonSpec primary;
    primary.label = "Conectar";
    primary.width = inner;
    primary.height = 24.f;
    primary.fontScale = 0.42f;
    primary.tone = ui::tone::green;
    m_primaryBtn = ui::makeButton(primary, [this] { this->onPrimary(); });
    if (m_primaryBtn) {
        m_primaryBtn->setID("request-primary-action");
        m_primaryBtn->setPosition({midX, primaryY});
        menu->addChild(m_primaryBtn);
    }

    // queue + order share one row; stacked they ran into the stats card.
    ui::ButtonSpec queueToggle;
    queueToggle.label = manager.isAccepting() ? "Abiertos" : "Cerrados";
    queueToggle.icon = manager.isAccepting() ? "GJ_completesIcon_001.png" : "GJ_lock_001.png";
    queueToggle.width = halfW;
    queueToggle.height = 20.f;
    queueToggle.fontScale = 0.34f;
    queueToggle.tone = manager.isAccepting() ? ui::tone::green : ui::tone::red;
    m_queueBtn = ui::makeButton(queueToggle, [this] { this->onToggleQueue(); });
    if (m_queueBtn) {
        m_queueBtn->setID("request-toggle-accepting");
        m_queueBtn->setPosition({kSidePad + halfW / 2.f, toggleY});
        menu->addChild(m_queueBtn);
    }

    ui::ButtonSpec order;
    order.label = manager.isRandomOrder() ? "Orden: azar" : "Orden: llegada";
    order.icon = "GJ_sortIcon_001.png";
    order.width = halfW;
    order.height = 20.f;
    order.fontScale = 0.34f;
    order.tone = manager.isRandomOrder() ? ui::tone::violet : ui::tone::cyan;
    m_orderBtn = ui::makeButton(order, [this] { this->onToggleOrder(); });
    if (m_orderBtn) {
        m_orderBtn->setID("request-toggle-order");
        m_orderBtn->setPosition({kSidePad + halfW * 1.5f + 6.f, toggleY});
        menu->addChild(m_orderBtn);
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
    card->setPosition({kSidePad, kStatsCardBottom});
    panel->addChild(card, 2);

    if (auto* inset = paimon::ui::makeInset({width, kStatsCardHeight}, 130)) {
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
        value->setPosition({cx, 10.f});
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
        if (!label || text == label->getString()) return;
        label->setString(text.c_str());
        label->limitLabelWidth(kStatColumnW - 4.f, kStatValueScale, 0.2f);
        pulse(label, label->getScale());
    };
    setStat(m_statReceived, std::to_string(stats.received));
    setStat(m_statPlayed, std::to_string(stats.played));
    setStat(m_statSkipped, std::to_string(stats.removed));
    setStat(m_statWait, waitLabel(manager.averageWaitSeconds()));
}

void TwitchRequestsLayer::refreshTabs() {
    if (!m_platformTabs) return;
    auto& manager = TwitchRequestManager::get();
    for (int index = 0; index < kSelectableCount; ++index) {
        auto const platform = platformFromIndex(index);
        bool const active = manager.isActive(platform);
        m_platformTabs->setDot(index,
            active ? stateColor(manager.state(platform)) : ccColor3B{90, 95, 110}, active);
    }
    m_platformTabs->select(static_cast<int>(manager.selected()));
}

void TwitchRequestsLayer::buildQueuePanel() {
    auto win = CCDirector::get()->getWinSize();
    float const height = win.height - kPanelTopGap - kPanelBottom;
    float const left = kMargin + kSideWidth + 10.f;
    float const width = win.width - left - kMargin;

    auto* panel = CCNode::create();
    panel->setContentSize({width, height});
    panel->setPosition({left, kPanelBottom});
    addChild(panel, 5);
    m_queuePanel = panel;

    if (auto* window = makeWindow({width, height})) {
        window->setPosition({0.f, 0.f});
        panel->addChild(window, -1);
    }

    auto* queueMenu = CCMenu::create();
    queueMenu->setPosition({0.f, 0.f});
    queueMenu->setTouchPriority(childTouchPrio());
    panel->addChild(queueMenu, 6);

    // list filter as a sliding selector with live counts.
    float const tabsWidth = std::min(216.f, width - 128.f);
    m_viewTabs = ui::SegmentBar::create(
        {{"Todos"}, {"Pendientes"}, {"Revisados"}}, {tabsWidth, 20.f}, childTouchPrio(),
        [this](int index) { this->onSelectView(index); }, ui::tone::cyan);
    if (m_viewTabs) {
        m_viewTabs->setPosition({10.f, height - 26.f});
        m_viewTabs->select(static_cast<int>(m_requestView), false);
        panel->addChild(m_viewTabs, 5);
    }

    ui::ButtonSpec selector;
    selector.label = "Cola: Todas";
    selector.width = 104.f;
    selector.height = 20.f;
    selector.fontScale = 0.34f;
    selector.tone = ui::tone::slate;
    m_queueSelectBtn = ui::makeButton(selector, [this] { onQueueSelection(); });
    if (m_queueSelectBtn) {
        m_queueSelectBtn->setID("request-queue-selector");
        m_queueSelectBtn->setPosition({width - 10.f - 52.f, height - 16.f});
        queueMenu->addChild(m_queueSelectBtn);
    }

    float const searchY = height - 42.f;
    float const searchLeft = 32.f;
    float const sweepWidth = 94.f;
    float const searchRight = width - 10.f - sweepWidth - 6.f - 22.f;
    if (auto* glass = paimon::SpriteHelper::safeCreateWithFrameName("gj_findBtn_001.png")) {
        glass->setScale(16.f / std::max(glass->getContentSize().height, 1.f));
        glass->setPosition({20.f, searchY});
        panel->addChild(glass, 3);
    }
    // gd's default 30pt input height crowds the header row.
    constexpr float kSearchScale = 0.8f;
    float const searchW = searchRight - searchLeft;
    m_searchInput = TextInput::create(searchW / kSearchScale, "Buscar nombre, ID o pedidor", "chatFont.fnt");
    if (m_searchInput) {
        m_searchInput->setMaxCharCount(60);
        m_searchInput->setScale(kSearchScale);
        m_searchInput->setPosition({searchLeft + searchW / 2.f, searchY});
        m_searchInput->setCommonFilter(CommonFilter::Text);
        m_searchInput->setCallback([this](std::string const&) { this->onSearchChanged(); });
        panel->addChild(m_searchInput, 3);
    }

    // native round close button; it appears only while there is text to clear.
    m_clearSearchBtn = makeFrameButton("GJ_closeBtn_001.png", 18.f,
        [this] { this->onClearSearch(); });
    if (m_clearSearchBtn) {
        m_clearSearchBtn->setID("request-search-clear");
        m_clearSearchBtn->setPosition({searchRight + 12.f, searchY});
        m_clearSearchBtn->setVisible(false);
        queueMenu->addChild(m_clearSearchBtn);
    }

    ui::ButtonSpec sweep;
    sweep.label = "Limpiar";
    sweep.width = sweepWidth;
    sweep.height = 20.f;
    sweep.fontScale = 0.34f;
    sweep.tone = ui::tone::slate;
    m_sweepBtn = ui::makeButton(sweep, [this] { this->onSweepReviewed(); });
    if (m_sweepBtn) {
        m_sweepBtn->setID("request-sweep-reviewed");
        m_sweepBtn->setPosition({width - 10.f - sweepWidth / 2.f, searchY});
        queueMenu->addChild(m_sweepBtn);
    }

    // footer strip: queue counters on the left, the level on stream on the right.
    float const stripY = 4.f;
    if (auto* strip = paimon::ui::makeInset({width - 20.f, kStripHeight}, 110)) {
        strip->setPosition({10.f, stripY});
        panel->addChild(strip, 1);
    }
    m_queueLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_queueLabel->setAnchorPoint({0.f, 0.5f});
    m_queueLabel->setColor(kDesc);
    m_queueLabel->setScale(0.34f);
    m_queueLabel->setPosition({18.f, stripY + kStripHeight / 2.f});
    panel->addChild(m_queueLabel, 2);

    m_nowPlaying = CCNode::create();
    m_nowPlaying->setPosition({width - 18.f, stripY + kStripHeight / 2.f});
    m_nowPlaying->setVisible(false);
    panel->addChild(m_nowPlaying, 2);
    if (auto* icon = paimon::SpriteHelper::safeCreateWithFrameName("edit_upBtn_001.png")) {
        icon->setRotation(90.f);
        icon->setScale(9.f / std::max(icon->getContentSize().height, 1.f));
        icon->setColor({120, 245, 150});
        icon->setTag(kNowPlayingIconTag);
        m_nowPlaying->addChild(icon, 1);
    }
    m_nowPlayingLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_nowPlayingLabel->setAnchorPoint({1.f, 0.5f});
    m_nowPlayingLabel->setColor({150, 255, 170});
    m_nowPlayingLabel->setScale(0.3f);
    m_nowPlaying->addChild(m_nowPlayingLabel, 1);

    float const listBottom = stripY + kStripHeight + 4.f;
    m_listWidth = width - 20.f;
    m_listHeight = height - kQueueHeaderHeight - listBottom;

    m_rowsHost = CCNode::create();
    m_rowsHost->setContentSize({m_listWidth, m_listHeight});
    m_rowsHost->setPosition({10.f, listBottom});
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
        name = cachedLevelName(request.levelID);
    }

    if (level == 0) {
        if (m_nowPlaying->isVisible()) {
            m_nowPlaying->setVisible(false);
            m_lastQueueText.clear();
        }
        m_nowPlayingLevel = 0;
        return;
    }

    if (level == m_nowPlayingLevel && m_nowPlaying->isVisible()) return;
    m_nowPlayingLevel = level;
    m_nowPlaying->setVisible(true);
    m_lastQueueText.clear();

    auto const text = fmt::format("En pantalla: {}  @{}",
        name.empty() ? fmt::format("ID {}", level) : shorten(name, 22),
        shorten(requester, 12));
    m_nowPlayingLabel->setString(text.c_str());
    m_nowPlayingLabel->limitLabelWidth(m_listWidth * 0.46f, 0.3f, 0.2f);
    if (auto* icon = m_nowPlaying->getChildByTag(kNowPlayingIconTag)) {
        icon->setPosition({-m_nowPlayingLabel->getScaledContentSize().width - 8.f, 0.f});
    }
    pulse(m_nowPlaying, 1.f);
}

void TwitchRequestsLayer::onSearchChanged() {
    if (!m_searchInput) return;
    auto query = std::string(m_searchInput->getString());
    bool const showClear = !query.empty();
    if (m_clearSearchBtn && showClear != m_clearShown) {
        m_clearShown = showClear;
        popVisible(m_clearSearchBtn, showClear);
    }
    if (query == m_searchQuery) return;
    m_searchQuery = std::move(query);
    rebuildRows();
}

void TwitchRequestsLayer::onClearSearch() {
    if (m_searchInput) m_searchInput->setString("");
    onSearchChanged();
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
        dock->addChild(backing, -1);
    }

    // children are placed from the dock's bottom-left corner.
    auto* menu = CCMenu::create();
    menu->setContentSize({dockWidth, 38.f});
    menu->setPosition({0.f, 0.f});
    dock->addChild(menu);

    struct Entry {
        ui::ButtonSpec spec;
        std::function<void()> action;
        char const* id;
    };
    std::vector<Entry> entries;

    ui::ButtonSpec next;
    next.label = "Jugar siguiente";
    next.icon = "edit_upBtn_001.png";
    next.iconRotation = 90.f;
    next.badge = "Enter";
    next.width = 172.f;
    next.height = 28.f;
    next.fontScale = 0.44f;
    next.tone = ui::tone::green;
    entries.push_back({next, [this] { this->onPlayNext(); }, "request-play-next"});

    ui::ButtonSpec manage;
    manage.label = "Gestionar";
    manage.icon = "GJ_hammerIcon_001.png";
    manage.width = 96.f;
    manage.height = 24.f;
    manage.fontScale = 0.38f;
    manage.tone = ui::tone::slate;
    entries.push_back({manage, [this] { this->onQueueTools(); }, "request-queue-tools"});

    ui::ButtonSpec filters;
    filters.label = "Filtros";
    filters.icon = "GJ_filterIcon_001.png";
    filters.width = 84.f;
    filters.height = 24.f;
    filters.fontScale = 0.38f;
    filters.tone = ui::tone::blue;
    entries.push_back({filters, [this] { this->onFilters(); }, "request-filters"});

    ui::ButtonSpec sources;
    sources.label = "Origenes";
    sources.icon = "GJ_menuBtn_001.png";
    sources.width = 90.f;
    sources.height = 24.f;
    sources.fontScale = 0.38f;
    sources.tone = ui::tone::violet;
    entries.push_back({sources, [this] { onSources(); }, "request-sources"});

    constexpr float kGap = 10.f;
    float total = kGap * static_cast<float>(entries.size() - 1);
    for (auto const& entry : entries) total += entry.spec.width;
    float const fit = std::min(1.f, (dockWidth - 16.f) / total);

    float cursor = (dockWidth - total * fit) / 2.f;
    for (auto& entry : entries) {
        entry.spec.width *= fit;
        if (auto* button = ui::makeButton(entry.spec, entry.action)) {
            button->setID(entry.id);
            button->setPosition({cursor + entry.spec.width / 2.f, 19.f});
            menu->addChild(button);
        }
        cursor += entry.spec.width + kGap * fit;
    }

    enterBy(dock, {0.f, -52.f}, 0.16f, true);
}

void TwitchRequestsLayer::tick(float) {
    TwitchLevelBriefCache::get().tick();
    refreshPercents();
    refreshStatus();
    refreshStats();
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
            ? m_statusLabel->getParent()->getContentSize().width - 32.f
            : 260.f;
        m_statusLabel->setString(m_lastStatus.c_str());
        m_statusLabel->limitLabelWidth(std::max(60.f, labelWidth), 0.4f, 0.24f);

        if (m_statusDot) {
            m_statusDot->removeAllChildren();
            if (auto* dot = paimon::SpriteHelper::createRoundedRect(
                    8.f, 8.f, 4.f, ccc4FFromccc3B(stateColor(manager.state(platform))))) {
                dot->setPosition({-4.f, -4.f});
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

    refreshTabs();
    refreshQueueHeader();
    refreshSideButtons();
    bool const web = platform == Platform::Web;

    if (m_webUrlLabel && web) {
        auto const url = manager.webUrl();
        m_webUrlLabel->setString(url.empty() ? "flozwer.org/request/<tu usuario>" : url.c_str());
        m_webUrlLabel->limitLabelWidth(kSideWidth - 2.f * kSidePad - 10.f, 0.4f, 0.22f);
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
        m_hintLabel->limitLabelWidth(kSideWidth - 2.f * kSidePad, kHintScale, 0.22f);
    }
}

void TwitchRequestsLayer::refreshQueueHeader() {
    auto& manager = TwitchRequestManager::get();
    size_t const total = manager.selectedRequestCount();
    size_t const pending = manager.pendingCount();
    size_t const reviewed = manager.reviewedCount();

    if (m_viewTabs) {
        std::array<std::string, 3> const labels{
            fmt::format("Todos {}", total),
            fmt::format("Pendientes {}", pending),
            fmt::format("Revisados {}", reviewed),
        };
        for (int index = 0; index < 3; ++index) {
            auto& last = m_lastViewLabels[static_cast<size_t>(index)];
            if (last == labels[static_cast<size_t>(index)]) continue;
            last = labels[static_cast<size_t>(index)];
            m_viewTabs->setLabel(index, last);
        }
    }

    if (m_queueLabel) {
        auto text = fmt::format("{} sin revisar  -  {}/{} en cola",
            pending, manager.requestCount(), manager.maxQueueSize());
        if (auto summary = filterSummary(manager.filters()); !summary.empty()) {
            text += "  -  " + summary;
            if (manager.filters().hasLevelFilters()) {
                text += fmt::format(" ({} ocultos)", manager.filteredCount());
            }
        }
        bool const sharing = m_nowPlaying && m_nowPlaying->isVisible();
        auto const key = text + (sharing ? "|1" : "|0");
        if (m_lastQueueText != key) {
            bool const first = m_lastQueueText.empty();
            m_lastQueueText = key;
            float const room = sharing ? m_listWidth * 0.5f - 12.f : m_listWidth - 16.f;
            fitLabel(m_queueLabel, text, room, 0.34f);
            if (!first) pulse(m_queueLabel, m_queueLabel->getScale());
        }
    }

    if (m_queueSelectBtn) {
        auto name = manager.selectedQueue().empty() ? "Todas" : manager.selectedQueue();
        ui::setLabel(m_queueSelectBtn, "Cola: " + shorten(name, 14));
    }

    if (m_sweepBtn && m_lastReviewed != reviewed) {
        m_lastReviewed = reviewed;
        ui::setLabel(m_sweepBtn, reviewed > 0 ? fmt::format("Limpiar ({})", reviewed) : "Limpiar");
        ui::setMuted(m_sweepBtn, reviewed == 0);
        ui::setTone(m_sweepBtn, reviewed > 0 ? ui::tone::red : ui::tone::slate);
    }
}

void TwitchRequestsLayer::refreshSideButtons() {
    auto& manager = TwitchRequestManager::get();
    auto const platform = manager.selected();

    if (m_queueBtn && m_lastAccepting != manager.isAccepting()) {
        bool const accepting = manager.isAccepting();
        bool const first = !m_lastAccepting.has_value();
        m_lastAccepting = accepting;
        ui::setLabel(m_queueBtn, accepting ? "Abiertos" : "Cerrados");
        ui::setIcon(m_queueBtn, accepting ? "GJ_completesIcon_001.png" : "GJ_lock_001.png");
        ui::setTone(m_queueBtn, accepting ? ui::tone::green : ui::tone::red);
        if (!first) ui::bounce(m_queueBtn);
    }

    if (m_orderBtn && m_lastRandomOrder != manager.isRandomOrder()) {
        bool const first = !m_lastRandomOrder.has_value();
        m_lastRandomOrder = manager.isRandomOrder();
        ui::setLabel(m_orderBtn, *m_lastRandomOrder ? "Orden: azar" : "Orden: llegada");
        ui::setTone(m_orderBtn, *m_lastRandomOrder ? ui::tone::violet : ui::tone::cyan);
        if (!first) ui::bounce(m_orderBtn);
    }

    if (m_primaryBtn) {
        auto const action = primaryAction(platform == Platform::Web, manager.webEnabled(),
            inputsDiffer(), manager.connectedCount(), manager.state(platform));
        if (m_lastPrimaryLabel != action.label) {
            bool const first = m_lastPrimaryLabel.empty();
            m_lastPrimaryLabel = action.label;
            ui::setLabel(m_primaryBtn, action.label);
            ui::setTone(m_primaryBtn, primaryTone(action.tone));
            if (!first) ui::bounce(m_primaryBtn);
        }
    }
}

void TwitchRequestsLayer::scheduleRebuild() {
    Ref<TwitchRequestsLayer> self = this;
    Loader::get()->queueInMainThread([self] {
        if (paimon::isRuntimeShuttingDown() || !self) return;
        if (self->getParent()) self->rebuildRows();
    });
}

void TwitchRequestsLayer::buildEmptyState(bool searching, bool allFiltered, bool emptyView) {
    float const cx = m_listWidth / 2.f;
    float const cy = m_listHeight / 2.f;

    char const* decoFrame = searching ? "GJ_searchBtn_001.png"
        : (allFiltered ? "GJ_filterIcon_001.png" : "GJ_chatBtn_001.png");
    if (auto* deco = paimon::SpriteHelper::safeCreateWithFrameName(decoFrame)) {
        deco->setScale(0.7f);
        deco->setOpacity(120);
        deco->setColor(currentAccent());
        deco->setPosition({cx, cy + 34.f});
        m_rowsHost->addChild(deco);
        breathe(deco, 0.66f, 0.74f, 1.3f);
    }

    char const* emptyText = searching ? "Sin coincidencias"
        : (allFiltered ? "Nada pasa el filtro"
            : (emptyView ? (m_requestView == RequestView::Pending ? "Sin pendientes" : "Sin revisados")
                : "Todavia no hay requests"));
    auto* empty = CCLabelBMFont::create(emptyText, "bigFont.fnt");
    empty->setScale(0.44f);
    empty->setColor({210, 215, 230});
    empty->setPosition({cx, cy + 4.f});
    m_rowsHost->addChild(empty);
    pulse(empty, 0.44f);

    auto& manager = TwitchRequestManager::get();
    std::string hintText;
    if (searching) {
        hintText = fmt::format("Nada coincide con \"{}\"", shorten(m_searchQuery, 18));
    } else if (allFiltered) {
        hintText = fmt::format("{} pedidos ocultos por los filtros", manager.filteredCount());
    } else if (emptyView) {
        hintText = "Cambia de pestana para ver el resto";
    } else {
        hintText = fmt::format("Escribe {} y una ID en tu chat", firstCommand());
    }
    auto* hint = CCLabelBMFont::create(hintText.c_str(), "chatFont.fnt");
    hint->setScale(0.38f);
    hint->setColor(kDesc);
    hint->setPosition({cx, cy - 12.f});
    m_rowsHost->addChild(hint);

    // one clear way out of every empty state.
    ui::ButtonSpec spec;
    spec.width = 130.f;
    spec.height = 20.f;
    spec.fontScale = 0.36f;
    std::function<void()> action;
    if (searching) {
        spec.label = "Limpiar busqueda";
        spec.tone = ui::tone::slate;
        action = [this] { this->onClearSearch(); };
    } else if (allFiltered) {
        spec.label = "Ver filtros";
        spec.tone = ui::tone::blue;
        action = [this] { this->onFilters(); };
    } else if (emptyView) {
        spec.label = "Ver todos";
        spec.tone = ui::tone::cyan;
        action = [this] { this->onSelectView(0); };
    } else {
        spec.label = "Agregar a mano";
        spec.icon = "GJ_plusBtn_001.png";
        spec.tone = ui::tone::green;
        action = [this] { this->onQueueTools(); };
    }
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    m_rowsHost->addChild(menu, 2);
    if (auto* button = ui::makeButton(spec, std::move(action))) {
        button->setPosition({cx, cy - 36.f});
        menu->addChild(button);
        enterBy(button, {0.f, -14.f}, 0.08f, true);
    }
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
    size_t matchingView = 0;
    for (size_t index = 0; index < requests.size(); ++index) {
        if (!matchesRequestView(requests[index], manager.selectedQueue(), m_requestView)) continue;
        ++matchingView;
        auto passes = requestPasses(requests[index].levelID, !requests[index].videoUrl.empty());
        if (passes && !*passes) continue;
        if (!m_searchQuery.empty()) {
            auto const& request = requests[index];
            bool const hit = matchesQuery(cachedLevelName(request.levelID), m_searchQuery)
                || matchesQuery(request.requester, m_searchQuery)
                || matchesQuery(std::to_string(request.levelID), m_searchQuery);
            if (!hit) continue;
        }
        visible.push_back(index);
    }

    if (visible.empty()) {
        bool const searching = !m_searchQuery.empty();
        bool const allFiltered = !searching && matchingView > 0;
        bool const emptyView = !searching && !allFiltered && manager.selectedRequestCount() > 0;
        buildEmptyState(searching, allFiltered, emptyView);
        return;
    }

    m_scroll = ScrollLayer::create({m_listWidth, m_listHeight});
    if (!m_scroll) return;
    m_scroll->setAnchorPoint({0.f, 0.f});
    m_scroll->setPosition({0.f, 0.f});
    m_rowsHost->addChild(m_scroll);

    float const contentHeight = std::max(
        m_listHeight,
        static_cast<float>(visible.size()) * (kRowHeight + kRowGap) - kRowGap + 4.f);
    auto* content = m_scroll->m_contentLayer;
    content->setContentSize({m_listWidth, contentHeight});

    // the request "play next" will open, so the streamer sees what comes up.
    std::optional<size_t> upNext;
    if (!manager.isRandomOrder()) upNext = manager.nextPendingIndex();

    for (size_t slot = 0; slot < visible.size(); ++slot) {
        size_t const index = visible[slot];
        auto* row = buildRow(index, slot + 1, requests[index], m_listWidth, upNext == index);
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
    float width,
    bool isNext
) {
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, kRowHeight});

    if (auto* plate = makePlate(width, kRowHeight, request.played ? 140 : 240)) {
        plate->setPosition({0.f, 0.f});
        if (isNext) {
            if (auto* tinted = typeinfo_cast<CCNodeRGBA*>(plate)) tinted->setColor({190, 255, 205});
        }
        row->addChild(plate, -1);
    }
    if (isNext) {
        if (auto* bar = paimon::SpriteHelper::createRoundedRect(
                3.f, kRowHeight - 14.f, 1.5f, ccc4FFromccc3B(kNextBar))) {
            bar->setPosition({4.f, 7.f});
            row->addChild(bar, 3);
        }
    }

    float const midY = kRowHeight / 2.f;
    if (request.played) {
        auto* check = paimon::SpriteHelper::safeCreateWithFrameName("GJ_checkOn_001.png");
        if (!check) check = paimon::SpriteHelper::safeCreateWithFrameName("GJ_completesIcon_001.png");
        if (check) {
            check->setScale(14.f / std::max(check->getContentSize().height, 1.f));
            check->setPosition({15.f, midY + 7.f});
            row->addChild(check, 2);
        }

        auto* percent = CCLabelBMFont::create(
            fmt::format("{}%", request.percent).c_str(), "bigFont.fnt");
        percent->setScale(0.28f);
        percent->setColor({120, 245, 150});
        percent->setPosition({15.f, midY - 8.f});
        row->addChild(percent, 2);
    } else {
        auto* label = CCLabelBMFont::create(fmt::format("{}", position).c_str(), "goldFont.fnt");
        label->setScale(0.34f);
        label->setPosition({15.f, midY});
        row->addChild(label, 2);
    }

    auto const* brief = TwitchLevelBriefCache::get().peek(request.levelID);
    if (!brief) TwitchLevelBriefCache::get().request(request.levelID);

    if (auto* face = GJDifficultySprite::create(
            brief ? brief->difficulty : 0, GJDifficultyName::Short)) {
        face->setScale(0.5f);
        face->setPosition({34.f, midY});
        row->addChild(face, 2);
    }

    // action area on the right: play, then a fixed 3x2 grid of small tools.
    constexpr float kPlayW = 38.f;
    constexpr float kTileW = 22.f;
    constexpr float kTilePitch = 24.f;
    float const playX = width - 6.f - kPlayW / 2.f;
    float const gridRight = width - 6.f - kPlayW - 5.f - kTileW / 2.f;
    float const gridLeft = gridRight - 2.f * kTilePitch - kTileW / 2.f;

    float const textLeft = 50.f;
    float const textWidth = gridLeft - 6.f - textLeft;
    auto const note = requestNote(request);

    std::string name = brief && brief->found
        ? brief->name
        : (brief ? "Nivel no encontrado" : "Cargando...");
    float const nameY = kRowHeight - 13.f;
    auto* nameLabel = CCLabelBMFont::create(shorten(name, 30).c_str(), "bigFont.fnt");
    nameLabel->setAnchorPoint({0.f, 0.5f});
    nameLabel->limitLabelWidth(textWidth, 0.46f, 0.24f);
    nameLabel->setPosition({textLeft, nameY});
    row->addChild(nameLabel, 2);

    float const starX = textLeft + nameLabel->getScaledContentSize().width + 6.f;
    if (brief && brief->stars > 0 && starX + 24.f < textLeft + textWidth) {
        auto* stars = CCLabelBMFont::create(
            fmt::format("{}", brief->stars).c_str(), "bigFont.fnt");
        stars->setAnchorPoint({0.f, 0.5f});
        stars->setScale(0.3f);
        stars->setColor(kGold);
        stars->setPosition({starX, nameY});
        row->addChild(stars, 2);

        if (auto* icon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_starsIcon_001.png")) {
            icon->setScale(0.42f);
            icon->setAnchorPoint({0.f, 0.5f});
            icon->setPosition({starX + stars->getScaledContentSize().width + 2.f, nameY});
            row->addChild(icon, 2);
        }
    }

    float const metaY = 25.f;
    auto* idLabel = CCLabelBMFont::create(
        fmt::format("ID {}", request.levelID).c_str(), "bigFont.fnt");
    idLabel->setAnchorPoint({0.f, 0.5f});
    idLabel->setColor(kDesc);
    idLabel->limitLabelWidth(textWidth * 0.5f, 0.26f, 0.16f);
    idLabel->setPosition({textLeft, metaY});
    row->addChild(idLabel, 2);

    float const authorX = textLeft + idLabel->getScaledContentSize().width + 7.f;
    if (brief && !brief->author.empty()) {
        auto* authorLabel = CCLabelBMFont::create("", "goldFont.fnt");
        authorLabel->setAnchorPoint({0.f, 0.5f});
        if (fitLabel(authorLabel, "por " + brief->author,
                textLeft + textWidth - authorX, 0.3f)) {
            authorLabel->setPosition({authorX, metaY});
            row->addChild(authorLabel, 2);
        }
    }

    // requester first, then where and when the request came from.
    float const detailY = 11.f;
    auto requesterText = "@" + shorten(
        request.requester, request.platform == Platform::Web ? 11 : 14);
    if (request.platform == Platform::Web) {
        requesterText += request.requesterVerified ? " [V]" : " [NV]";
    }
    auto* requesterLabel = CCLabelBMFont::create(requesterText.c_str(), "bigFont.fnt");
    requesterLabel->setAnchorPoint({0.f, 0.5f});
    requesterLabel->setColor(platformAccent(request.platform));
    requesterLabel->limitLabelWidth(textWidth * 0.55f, 0.28f, 0.18f);
    requesterLabel->setPosition({textLeft, detailY});
    row->addChild(requesterLabel, 2);

    auto const source = request.sourceName.empty() ? shortPlatform(request.platform) : request.sourceName;
    std::string details = TwitchRequestManager::get().selectedQueue().empty() && request.queue != "General"
        ? request.queue + " / " + source
        : source;
    if (auto ago = timeAgo(request.receivedAt); !ago.empty()) details += " - " + ago;
    if (!note.empty()) details += " - " + note;
    float const detailX = textLeft + requesterLabel->getScaledContentSize().width + 6.f;
    auto* detailsLabel = CCLabelBMFont::create("", "chatFont.fnt");
    detailsLabel->setAnchorPoint({0.f, .5f});
    detailsLabel->setColor(kDesc);
    if (fitLabel(detailsLabel, details, textLeft + textWidth - detailX, .3f)) {
        detailsLabel->setPosition({detailX, detailY});
        row->addChild(detailsLabel, 2);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    row->addChild(menu, 5);

    auto const entryID = request.entryID;

    auto place = [&](CCMenuItemSpriteExtra* button, float x, float y) {
        if (!button) return;
        button->setPosition({x, y});
        menu->addChild(button);
    };

    place(makeFrameButton("GJ_playBtn2_001.png", 34.f,
        [this, entryID] { this->playRequest(entryID); }), playX, midY);

    // fixed slots so Delete and Video never move between rows.
    float const columnX[3] = {gridLeft + kTileW / 2.f, gridLeft + kTileW / 2.f + kTilePitch, gridRight};
    float const rowY[2] = {midY + 11.f, midY - 11.f};
    constexpr float kCircle = 0.5f;

    if (index > 0) {
        place(makeCircleButton("GJ_arrow_01_001.png", CircleBaseColor::Cyan, kCircle,
            [this, entryID] { this->moveRequestToFront(entryID); }, 90.f), columnX[0], rowY[0]);
    }
    if (!note.empty()) {
        place(makeCircleButton("GJ_infoIcon_001.png", CircleBaseColor::Blue, kCircle,
            [this, entryID] { this->openMessage(entryID); }), columnX[1], rowY[0]);
    }
    if (!request.videoUrl.empty()) {
        place(makeFrameButton("gj_ytIcon_001.png", 17.f,
            [this, url = request.videoUrl] { this->openVideo(url); }), columnX[2], rowY[0]);
    }
    place(makeCircleButton(
        request.played ? "GJ_undoBtn_001.png" : "GJ_completesIcon_001.png",
        request.played ? CircleBaseColor::Gray : CircleBaseColor::Green, kCircle,
        [this, entryID] { this->toggleReviewed(entryID); }), columnX[0], rowY[1]);
    place(makeCircleButton("GJ_duplicateBtn_001.png", CircleBaseColor::Gray, kCircle,
        [this, levelID = request.levelID] { this->copyLevelId(levelID); }), columnX[1], rowY[1]);
    place(makeFrameButton("GJ_trashBtn_001.png", 21.f,
        [this, entryID] { this->removeRequest(entryID); }), columnX[2], rowY[1]);

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
            PaimonNotify::show(missing, NotificationIcon::Warning);
            refreshStatus();
            return;
        }
        manager.setLive(true);
        manager.restart();
        PaimonNotify::show(
            fmt::format("Conectando {} chat(s)...", manager.activeCount()),
            NotificationIcon::Info);
        refreshStatus();
        return;
    }

    if (manager.connectedCount() > 0 || manager.state(platform) == ConnectionState::Connecting) {
        manager.setLive(false);
        PaimonNotify::show("Chats pausados", NotificationIcon::Info);
        refreshStatus();
        return;
    }

    if (manager.activeCount() == 0) {
        PaimonNotify::show(missing, NotificationIcon::Warning);
        return;
    }
    manager.setLive(true);
    manager.restart();
    refreshStatus();
}

void TwitchRequestsLayer::onSelectPlatform(int index) {
    auto& manager = TwitchRequestManager::get();
    if (inputsDiffer()) applyInputs();

    manager.select(platformFromIndex(std::clamp(index, 0, kSelectableCount - 1)));
    applyPlatformSkin(true);
    refreshStatus();
}

void TwitchRequestsLayer::applyPlatformSkin(bool animate) {
    auto& manager = TwitchRequestManager::get();
    auto const platform = manager.selected();
    bool const web = platform == Platform::Web;

    if (m_background) tintTo(m_background, backdropColor(platform));
    if (m_titleIcon) tintTo(m_titleIcon, platformAccent(platform));
    if (m_platformTabs) m_platformTabs->select(static_cast<int>(platform), animate);
    if (m_channelCaption) {
        m_channelCaption->setString(web ? "Tu pagina publica" : platformFieldName(platform));
    }

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
    if (m_webUrlPlate) m_webUrlPlate->setVisible(web);
    if (m_webMenu) m_webMenu->setVisible(web);
    if (m_webVerificationMenu) m_webVerificationMenu->setVisible(web);

    if (!animate) return;
    // the controls that changed glide in from the side, one after another.
    float const dx = web ? 18.f : -18.f;
    slideIn(m_channelCaption, dx, 0.f);
    if (web) {
        slideIn(m_webUrlPlate, dx, 0.03f);
        slideIn(m_webMenu, dx, 0.06f);
        slideIn(m_webVerificationMenu, dx, 0.09f);
    } else {
        slideIn(m_channelInput, dx, 0.03f);
        slideIn(m_commandCaption, dx, 0.06f);
        slideIn(m_commandInput, dx, 0.09f);
    }
    ui::bounce(m_primaryBtn);
}

void TwitchRequestsLayer::onVerifyWebAccount() {
    paimon::modauth::showPanel();
}

void TwitchRequestsLayer::onToggleQueue() {
    auto& manager = TwitchRequestManager::get();
    manager.setAccepting(!manager.isAccepting());
    PaimonNotify::show(
        manager.isAccepting() ? "Requests abiertos: se reciben niveles" : "Requests cerrados: no entran niveles nuevos",
        manager.isAccepting() ? NotificationIcon::Success : NotificationIcon::Warning);
    refreshStatus();
}

void TwitchRequestsLayer::onToggleOrder() {
    auto& manager = TwitchRequestManager::get();
    manager.setRandomOrder(!manager.isRandomOrder());
    PaimonNotify::show(
        manager.isRandomOrder() ? "Siguiente nivel: al azar" : "Siguiente nivel: por orden de llegada",
        NotificationIcon::Info);
    scheduleRebuild();
    refreshStatus();
}

void TwitchRequestsLayer::onToggleWebPage() {
    auto& manager = TwitchRequestManager::get();
    bool const enable = !manager.webEnabled();
    if (enable) manager.setLive(true);
    manager.setWebEnabled(enable);
    PaimonNotify::show(
        enable ? "Abriendo tu pagina de requests..." : "Pagina de requests apagada",
        enable ? NotificationIcon::Info : NotificationIcon::Warning);
    refreshStatus();
}

void TwitchRequestsLayer::onCopyWebUrl() {
    auto const url = TwitchRequestManager::get().webUrl();
    if (url.empty()) {
        PaimonNotify::show(
            "Enciende la pagina para tener tu link", NotificationIcon::Warning);
        return;
    }
    geode::utils::clipboard::write("https://" + url);
    PaimonNotify::show("Link copiado: " + url, NotificationIcon::Success);
}

void TwitchRequestsLayer::onOpenWebUrl() {
    auto const url = TwitchRequestManager::get().webUrl();
    if (url.empty()) {
        PaimonNotify::show(
            "Enciende la pagina para tener tu link", NotificationIcon::Warning);
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

void TwitchRequestsLayer::openMessage(int64_t entryID) {
    auto& manager = TwitchRequestManager::get();
    auto index = manager.indexForEntry(entryID);
    if (!index) return;
    auto requests = manager.requests();

    auto const& request = requests[*index];
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
        PaimonNotify::show(reason, NotificationIcon::Info);
        return;
    }
    playRequestAt(*index, false);
}

void TwitchRequestsLayer::playRequest(int64_t entryID) {
    if (auto index = TwitchRequestManager::get().indexForEntry(entryID)) playRequestAt(*index, false);
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

void TwitchRequestsLayer::onQueueTools() {
    if (auto* popup = RequestQueueToolsPopup::create()) popup->show();
}

void TwitchRequestsLayer::onSelectView(int index) {
    m_requestView = static_cast<RequestView>(std::clamp(index, 0, 2));
    Mod::get()->setSavedValue(kRequestViewKey, static_cast<int64_t>(m_requestView));
    paimon::requestDeferredModSave();
    if (m_viewTabs) m_viewTabs->select(static_cast<int>(m_requestView));
    rebuildRows();
}

void TwitchRequestsLayer::onSweepReviewed() {
    auto& manager = TwitchRequestManager::get();
    auto const count = manager.reviewedCount();
    if (count == 0) {
        PaimonNotify::show("No hay requests revisados para limpiar", NotificationIcon::Info);
        return;
    }
    Ref<TwitchRequestsLayer> self = this;
    geode::createQuickPopup("Limpiar revisados",
        fmt::format("Eliminar <cy>{}</c> requests revisados?\nLos pendientes se conservan.", count),
        "Cancelar", "Limpiar", [self](auto, bool confirmed) {
            if (!confirmed || !self->getParent()) return;
            auto const removed = TwitchRequestManager::get().removeReviewed();
            PaimonNotify::show(
                fmt::format("{} requests revisados eliminados", removed), NotificationIcon::Success);
            self->scheduleRebuild();
        });
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

bool TwitchRequestsLayer::scrollWheel(float x, float y) {
    if (m_popupOnTop) return false;
    if (kit::queueWheelScroll(m_scroll, x, y, m_wheelTargetY, m_wheelTargetSet)) return true;
    return CCLayer::scrollWheel(x, y);

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
    PaimonNotify::show(
        fmt::format("ID {} copiado", levelID), NotificationIcon::Success);
}

void TwitchRequestsLayer::toggleReviewed(int64_t entryID) {
    auto& manager = TwitchRequestManager::get();
    auto index = manager.indexForEntry(entryID);
    if (!index) return;
    auto requests = manager.requests();
    bool const next = !requests[*index].played;
    manager.setReviewed(*index, next);
    PaimonNotify::show(
        next ? "Marcado como revisado" : "Marcado como pendiente",
        NotificationIcon::Info);
    scheduleRebuild();
}

void TwitchRequestsLayer::moveRequestToFront(int64_t entryID) {
    auto& manager = TwitchRequestManager::get();
    if (auto current = manager.indexForEntry(entryID)) manager.moveToFront(*current);
    scheduleRebuild();
}

void TwitchRequestsLayer::removeRequest(int64_t entryID) {
    auto& manager = TwitchRequestManager::get();
    auto index = manager.indexForEntry(entryID);
    if (!index) return;

    auto requests = manager.requests();
    auto removed = requests[*index];
    auto name = cachedLevelName(removed.levelID);
    if (name.empty()) name = fmt::format("ID {}", removed.levelID);
    manager.remove(*index);
    showUndo(std::move(removed), *index, std::move(name));
    scheduleRebuild();
}

void TwitchRequestsLayer::showUndo(LevelRequest request, size_t index, std::string name) {
    unschedule(schedule_selector(TwitchRequestsLayer::hideUndo));
    if (m_undoToast) {
        m_undoToast->removeFromParent();
        m_undoToast = nullptr;
    }
    m_undo = UndoEntry{std::move(request), index};
    if (!m_queuePanel) return;

    float const width = std::min(300.f, m_listWidth - 8.f);
    float const height = 26.f;
    float const restY = kStripHeight + 10.f;

    auto* toast = CCNode::create();
    toast->setContentSize({width, height});
    toast->setAnchorPoint({0.5f, 0.f});
    toast->ignoreAnchorPointForPosition(false);
    toast->setPosition({m_queuePanel->getContentSize().width / 2.f, restY});

    if (auto* edge = paimon::ui::makeInset({width, height}, 255, {255, 205, 90})) {
        toast->addChild(edge, 0);
    }
    if (auto* fill = paimon::ui::makeInset({width - 2.f, height - 2.f}, 255, {16, 22, 44})) {
        fill->setPosition({1.f, 1.f});
        toast->addChild(fill, 1);
    }

    auto* label = CCLabelBMFont::create(
        fmt::format("Eliminado: {}", shorten(name, 22)).c_str(), "bigFont.fnt");
    label->setAnchorPoint({0.f, 0.5f});
    label->limitLabelWidth(width - 104.f, 0.32f, 0.2f);
    label->setPosition({10.f, height / 2.f + 0.5f});
    toast->addChild(label, 2);
    m_undoLabel = label;

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    toast->addChild(menu, 3);

    ui::ButtonSpec undo;
    undo.label = "Deshacer";
    undo.icon = "GJ_undoBtn_001.png";
    undo.width = 80.f;
    undo.height = 20.f;
    undo.fontScale = 0.34f;
    undo.tone = ui::tone::cyan;
    if (auto* button = ui::makeButton(undo, [this] { this->onUndo(); })) {
        button->setID("request-undo-remove");
        button->setPosition({width - 44.f, height / 2.f});
        menu->addChild(button);
    }

    m_queuePanel->addChild(toast, 20);
    m_undoToast = toast;
    if (motionOn()) {
        toast->setPositionY(restY - 30.f);
        toast->runAction(CCEaseBackOut::create(CCMoveTo::create(
            animTime(0.32f), {toast->getPositionX(), restY})));
    }
    scheduleOnce(schedule_selector(TwitchRequestsLayer::hideUndo), kUndoSeconds);
}

void TwitchRequestsLayer::hideUndo(float) {
    unschedule(schedule_selector(TwitchRequestsLayer::hideUndo));
    m_undo.reset();
    m_undoLabel = nullptr;
    auto* toast = m_undoToast;
    m_undoToast = nullptr;
    if (!toast) return;
    if (!motionOn()) {
        toast->removeFromParent();
        return;
    }
    toast->runAction(CCSequence::create(
        CCEaseSineIn::create(CCMoveBy::create(animTime(0.22f), {0.f, -34.f})),
        CCRemoveSelf::create(),
        nullptr));
}

void TwitchRequestsLayer::onUndo() {
    if (!m_undo) return;
    auto entry = std::move(*m_undo);
    hideUndo();
    if (TwitchRequestManager::get().restore(std::move(entry.request), entry.index)) {
        PaimonNotify::show("Request restaurado", NotificationIcon::Success);
        scheduleRebuild();
        return;
    }
    PaimonNotify::show("No se pudo restaurar: cola llena o nivel repetido", NotificationIcon::Warning);
}

}
