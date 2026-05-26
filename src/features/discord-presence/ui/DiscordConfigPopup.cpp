#include "DiscordConfigPopup.hpp"

#include "../services/DiscordPresenceManager.hpp"
#include "../../../core/Settings.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../settings-panel/ui/SettingsControls.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/ScrollLayer.hpp>

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::discord {

namespace {

constexpr float kPopupW = 440.f;
constexpr float kPopupH = 310.f;

template<typename T>
T gset(char const* key) {
    if (Mod::get()->hasSetting(key)) {
        return Mod::get()->getSettingValue<T>(key);
    }
    return Mod::get()->getSavedValue<T>(key, T{});
}

template<typename T>
void sset(char const* key, T val) {
    if (Mod::get()->hasSetting(key)) {
        Mod::get()->setSettingValue<T>(key, val);
    } else {
        Mod::get()->setSavedValue(key, val);
    }
}

inline void kick() {
    DiscordPresenceManager::get().refreshSoon();
}

} // namespace

// ─────────────────────────────────────────────────────────────────
// create / init
// ─────────────────────────────────────────────────────────────────

DiscordConfigPopup* DiscordConfigPopup::create() {
    auto ret = new DiscordConfigPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool DiscordConfigPopup::init() {
    if (!Popup::init(kPopupW, kPopupH)) return false;

    this->setTitle("Discord Rich Presence");
    this->setMouseEnabled(true);

    auto content = m_mainLayer->getContentSize();
    float w = content.width - 36.f;

    using namespace paimon::settings_ui;

    std::vector<CCNode*> rows;

    rows.push_back(createSectionHeader("Core", w));
    rows.push_back(createToggleRow("Enable Rich Presence",
        gset<bool>("discord-rpc-enabled"),
        [](bool v) { sset<bool>("discord-rpc-enabled", v); kick(); }, w));
    rows.push_back(createToggleRow("Private Mode",
        gset<bool>("discord-rpc-private-mode"),
        [](bool v) { sset<bool>("discord-rpc-private-mode", v); kick(); }, w));
    rows.push_back(createToggleRow("Idle When Unfocused",
        gset<bool>("discord-rpc-idle-when-unfocused"),
        [](bool v) { sset<bool>("discord-rpc-idle-when-unfocused", v); kick(); }, w));
    rows.push_back(createToggleRow("Show Elapsed Time",
        gset<bool>("discord-rpc-show-timestamp"),
        [](bool v) { sset<bool>("discord-rpc-show-timestamp", v); kick(); }, w));
    rows.push_back(createToggleRow("Show Level Progress",
        gset<bool>("discord-rpc-show-progress"),
        [](bool v) { sset<bool>("discord-rpc-show-progress", v); kick(); }, w));
    rows.push_back(createToggleRow("Include Paimbnails Features",
        gset<bool>("discord-rpc-include-paimbnails-features"),
        [](bool v) { sset<bool>("discord-rpc-include-paimbnails-features", v); kick(); }, w));

    rows.push_back(createSectionHeader("Activity Type", w));
    rows.push_back(createDropdownRow("Discord Label",
        gset<std::string>("discord-rpc-activity-type"),
        {"Playing", "Listening", "Watching", "Competing"},
        [](std::string const& v) { sset<std::string>("discord-rpc-activity-type", v); kick(); }, w));

    rows.push_back(createSectionHeader("Custom Text", w));
    rows.push_back(createToggleRow("Override Details Line",
        gset<bool>("discord-rpc-override-details"),
        [](bool v) { sset<bool>("discord-rpc-override-details", v); kick(); }, w));

    {
        auto row = CCNode::create();
        row->setContentSize({w, 38.f});
        auto lbl = CCLabelBMFont::create("Details:", "chatFont.fnt");
        lbl->setScale(0.5f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setColor({200, 200, 200});
        lbl->setPosition({8.f, 19.f});
        row->addChild(lbl);

        m_detailsInput = TextInput::create(w - 110.f, "Playing my own way", "chatFont.fnt");
        m_detailsInput->setCommonFilter(CommonFilter::Any);
        m_detailsInput->setMaxCharCount(128);
        m_detailsInput->setString(gset<std::string>("discord-rpc-custom-details"));
        m_detailsInput->setPosition({w / 2.f + 30.f, 19.f});
        m_detailsInput->setScale(0.80f);
        m_detailsInput->setCallback([](std::string const& v) {
            sset<std::string>("discord-rpc-custom-details", v); kick();
        });
        row->addChild(m_detailsInput);
        rows.push_back(row);
    }

    rows.push_back(createToggleRow("Override State Line",
        gset<bool>("discord-rpc-override-state"),
        [](bool v) { sset<bool>("discord-rpc-override-state", v); kick(); }, w));

    {
        auto row = CCNode::create();
        row->setContentSize({w, 38.f});
        auto lbl = CCLabelBMFont::create("State:", "chatFont.fnt");
        lbl->setScale(0.5f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setColor({200, 200, 200});
        lbl->setPosition({8.f, 19.f});
        row->addChild(lbl);

        m_stateInput = TextInput::create(w - 110.f, "With Paimon by my side", "chatFont.fnt");
        m_stateInput->setCommonFilter(CommonFilter::Any);
        m_stateInput->setMaxCharCount(128);
        m_stateInput->setString(gset<std::string>("discord-rpc-custom-state"));
        m_stateInput->setPosition({w / 2.f + 30.f, 19.f});
        m_stateInput->setScale(0.80f);
        m_stateInput->setCallback([](std::string const& v) {
            sset<std::string>("discord-rpc-custom-state", v); kick();
        });
        row->addChild(m_stateInput);
        rows.push_back(row);
    }

    rows.push_back(createSectionHeader("Banner Tooltip", w));
    {
        auto row = CCNode::create();
        row->setContentSize({w, 38.f});
        auto lbl = CCLabelBMFont::create("Hover text:", "chatFont.fnt");
        lbl->setScale(0.5f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setColor({200, 200, 200});
        lbl->setPosition({8.f, 19.f});
        row->addChild(lbl);

        m_largeTextInput = TextInput::create(w - 130.f, "Paimbnails Rich Presence", "chatFont.fnt");
        m_largeTextInput->setCommonFilter(CommonFilter::Any);
        m_largeTextInput->setMaxCharCount(128);
        m_largeTextInput->setString(gset<std::string>("discord-rpc-large-text"));
        m_largeTextInput->setPosition({w / 2.f + 30.f, 19.f});
        m_largeTextInput->setScale(0.80f);
        m_largeTextInput->setCallback([](std::string const& v) {
            sset<std::string>("discord-rpc-large-text", v); kick();
        });
        row->addChild(m_largeTextInput);
        rows.push_back(row);
    }

    rows.push_back(createSectionHeader("Discord Asset Keys", w));
    {
        auto row = CCNode::create();
        row->setContentSize({w, 38.f});
        auto lbl = CCLabelBMFont::create("Large:", "chatFont.fnt");
        lbl->setScale(0.5f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setColor({200, 200, 200});
        lbl->setPosition({8.f, 19.f});
        row->addChild(lbl);

        m_largeImageKeyInput = TextInput::create(w - 110.f, "paimbnails", "chatFont.fnt");
        m_largeImageKeyInput->setCommonFilter(CommonFilter::Any);
        m_largeImageKeyInput->setMaxCharCount(128);
        m_largeImageKeyInput->setString(gset<std::string>("discord-rpc-large-image-key"));
        m_largeImageKeyInput->setPosition({w / 2.f + 30.f, 19.f});
        m_largeImageKeyInput->setScale(0.80f);
        m_largeImageKeyInput->setCallback([](std::string const& v) {
            sset<std::string>("discord-rpc-large-image-key", v); kick();
        });
        row->addChild(m_largeImageKeyInput);
        rows.push_back(row);
    }

    {
        auto row = CCNode::create();
        row->setContentSize({w, 38.f});
        auto lbl = CCLabelBMFont::create("Small:", "chatFont.fnt");
        lbl->setScale(0.5f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setColor({200, 200, 200});
        lbl->setPosition({8.f, 19.f});
        row->addChild(lbl);

        m_smallImageKeyInput = TextInput::create(w - 110.f, "paimbnails", "chatFont.fnt");
        m_smallImageKeyInput->setCommonFilter(CommonFilter::Any);
        m_smallImageKeyInput->setMaxCharCount(128);
        m_smallImageKeyInput->setString(gset<std::string>("discord-rpc-small-image-key"));
        m_smallImageKeyInput->setPosition({w / 2.f + 30.f, 19.f});
        m_smallImageKeyInput->setScale(0.80f);
        m_smallImageKeyInput->setCallback([](std::string const& v) {
            sset<std::string>("discord-rpc-small-image-key", v); kick();
        });
        row->addChild(m_smallImageKeyInput);
        rows.push_back(row);
    }

    // ── Scroll ──
    auto scrollSize = CCSize{w, content.height - 80.f};
    auto scroll = ScrollLayer::create(scrollSize);
    scroll->setPosition({18.f, 42.f});
    m_mainLayer->addChild(scroll, 5);

    auto contentLayer = scroll->m_contentLayer;
    float totalH = 0.f;
    for (auto* r : rows) totalH += r->getContentSize().height + 2.f;
    totalH = std::max(totalH, scrollSize.height);
    contentLayer->setContentSize({w, totalH});

    float y = totalH;
    for (auto* r : rows) {
        float h = r->getContentSize().height;
        y -= h;
        r->setPosition({0.f, y});
        contentLayer->addChild(r);
        y -= 2.f;
    }
    scroll->scrollToTop();

    // ── Footer buttons (Reset / Refresh / Geode) ──
    {
        auto footer = CCMenu::create();
        footer->setPosition({0.f, 0.f});
        footer->setContentSize(content);
        m_mainLayer->addChild(footer, 20);

        auto resetSpr = ButtonSprite::create("Reset", "bigFont.fnt", "GJ_button_06.png", 0.8f);
        resetSpr->setScale(0.42f);
        auto resetBtn = CCMenuItemSpriteExtra::create(
            resetSpr, this, menu_selector(DiscordConfigPopup::onResetDefaults));
        resetBtn->setPosition({42.f, 18.f});
        footer->addChild(resetBtn);

        auto refreshSpr = ButtonSprite::create("Refresh", "bigFont.fnt", "GJ_button_05.png", 0.8f);
        refreshSpr->setScale(0.42f);
        auto refreshBtn = CCMenuItemSpriteExtra::create(
            refreshSpr, this, menu_selector(DiscordConfigPopup::onRefreshPresence));
        refreshBtn->setPosition({content.width / 2.f, 18.f});
        footer->addChild(refreshBtn);

        auto geodeSpr = ButtonSprite::create("Geode", "bigFont.fnt", "GJ_button_04.png", 0.8f);
        geodeSpr->setScale(0.42f);
        auto geodeBtn = CCMenuItemSpriteExtra::create(
            geodeSpr, this, menu_selector(DiscordConfigPopup::onOpenGeodeSettings));
        geodeBtn->setPosition({content.width - 42.f, 18.f});
        footer->addChild(geodeBtn);
    }

    paimon::markDynamicPopup(this);
    return true;
}

void DiscordConfigPopup::onExit() {
    DiscordPresenceManager::get().refreshSoon();
    Popup::onExit();
}

// ─────────────────────────────────────────────────────────────────
// Actions
// ─────────────────────────────────────────────────────────────────

void DiscordConfigPopup::onOpenGeodeSettings(CCObject*) {
    geode::openSettingsPopup(Mod::get(), false);
}

void DiscordConfigPopup::onRefreshPresence(CCObject*) {
    DiscordPresenceManager::get().refreshSoon();
    PaimonNotify::create("Rich Presence refreshed.", NotificationIcon::Success)->show();
}

void DiscordConfigPopup::onResetDefaults(CCObject*) {
    createQuickPopup("Reset Discord RPC",
        "Reset all Discord RPC settings to their defaults?",
        "Cancel", "Reset",
        [](auto, bool btn2) {
            if (!btn2) return;
            if (auto setting = Mod::get()->getSetting("discord-rpc-enabled")) setting->reset();

            auto* mod = Mod::get();
            mod->setSavedValue("discord-rpc-private-mode", false);
            mod->setSavedValue("discord-rpc-idle-when-unfocused", true);
            mod->setSavedValue("discord-rpc-show-progress", true);
            mod->setSavedValue("discord-rpc-include-paimbnails-features", true);
            mod->setSavedValue<std::string>("discord-rpc-large-text", "");
            mod->setSavedValue<std::string>("discord-rpc-large-image-key", "");
            mod->setSavedValue<std::string>("discord-rpc-small-image-key", "");
            mod->setSavedValue<std::string>("discord-rpc-activity-type", "Playing");
            mod->setSavedValue("discord-rpc-show-timestamp", true);
            mod->setSavedValue("discord-rpc-override-details", false);
            mod->setSavedValue<std::string>("discord-rpc-custom-details", "");
            mod->setSavedValue("discord-rpc-override-state", false);
            mod->setSavedValue<std::string>("discord-rpc-custom-state", "");

            DiscordPresenceManager::get().refreshSoon();
            PaimonNotify::create("Discord RPC reset to defaults.", NotificationIcon::Success)->show();
        });
}

} // namespace paimon::discord
