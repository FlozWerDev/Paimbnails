#include "DiscordConfigPopup.hpp"

#include "../services/DiscordPresenceManager.hpp"
#include "../../../core/Settings.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/PopupManager.hpp>

#include <cctype>

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::discord {

namespace {

constexpr float kPopupW = 440.f;
constexpr float kPopupH = 310.f;

// Row metrics (compact)
constexpr float kToggleRowH = 27.f;
constexpr float kInputRowH  = 26.f;
constexpr float kCardHeaderH = 17.f;
constexpr float kCardPadX = 8.f;
constexpr float kRowGap = 3.f;

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

int childTouchPrio() {
    return CCDirector::get()->getTouchDispatcher()->getTargetPrio() - 2;
}

class ToggleCallback : public CCObject {
public:
    std::function<void(bool)> m_callback;
    CCMenuItemToggler* m_toggler = nullptr;

    static ToggleCallback* create(std::function<void(bool)> cb) {
        auto ret = new ToggleCallback();
        ret->m_callback = std::move(cb);
        ret->autorelease();
        return ret;
    }

    void onToggle(CCObject*) {
        if (m_callback && m_toggler) {
            m_callback(!m_toggler->isToggled());
        }
    }
};

class CycleCallback : public CCObject {
public:
    std::function<void(std::string const&)> m_callback;
    std::vector<std::string> m_options;
    int m_currentIndex = 0;
    CCLabelBMFont* m_valueLabel = nullptr;

    static CycleCallback* create(std::function<void(std::string const&)> cb,
                                 std::vector<std::string> opts, int initIdx) {
        auto ret = new CycleCallback();
        ret->m_callback = std::move(cb);
        ret->m_options = std::move(opts);
        ret->m_currentIndex = initIdx;
        ret->autorelease();
        return ret;
    }

    void step(int dir) {
        if (m_options.empty()) return;
        int n = static_cast<int>(m_options.size());
        m_currentIndex = (m_currentIndex + dir + n) % n;
        if (m_valueLabel) m_valueLabel->setString(m_options[m_currentIndex].c_str());
        if (m_callback) m_callback(m_options[m_currentIndex]);
    }
    void onNext(CCObject*) { step(1); }
    void onPrev(CCObject*) { step(-1); }
};

// Title + small gray description + toggle on the right
CCNode* makeToggleRow(const char* title, const char* desc, bool value,
                      std::function<void(bool)> onChange, float width) {
    auto row = CCNode::create();
    row->setContentSize({width, kToggleRowH});
    row->setAnchorPoint({0.f, 0.f});

    auto lbl = CCLabelBMFont::create(title, "chatFont.fnt");
    lbl->setScale(0.5f);
    lbl->setColor({240, 240, 240});
    lbl->setAnchorPoint({0.f, 0.5f});
    lbl->setPosition({2.f, kToggleRowH - 8.f});
    row->addChild(lbl);

    auto sub = CCLabelBMFont::create(desc, "chatFont.fnt");
    sub->setScale(0.34f);
    sub->setColor({155, 163, 178});
    sub->setAnchorPoint({0.f, 0.5f});
    sub->setPosition({2.f, 7.f});
    row->addChild(sub);

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    row->addChild(menu);

    auto cb = ToggleCallback::create(std::move(onChange));
    auto toggler = CCMenuItemToggler::createWithStandardSprites(
        cb, menu_selector(ToggleCallback::onToggle), 0.48f);
    cb->m_toggler = toggler;
    toggler->toggle(value);
    toggler->setPosition({width - 14.f, kToggleRowH / 2.f});
    toggler->setUserObject(cb);
    menu->addChild(toggler);

    return row;
}

// Title + description + < value > selector on the right
CCNode* makeCycleRow(const char* title, const char* desc,
                     std::string const& initialValue,
                     std::vector<std::string> const& options,
                     std::function<void(std::string const&)> onChange,
                     float width) {
    auto row = CCNode::create();
    row->setContentSize({width, kToggleRowH});
    row->setAnchorPoint({0.f, 0.f});

    auto lbl = CCLabelBMFont::create(title, "chatFont.fnt");
    lbl->setScale(0.5f);
    lbl->setColor({240, 240, 240});
    lbl->setAnchorPoint({0.f, 0.5f});
    lbl->setPosition({2.f, kToggleRowH - 8.f});
    row->addChild(lbl);

    auto sub = CCLabelBMFont::create(desc, "chatFont.fnt");
    sub->setScale(0.34f);
    sub->setColor({155, 163, 178});
    sub->setAnchorPoint({0.f, 0.5f});
    sub->setPosition({2.f, 7.f});
    row->addChild(sub);

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    row->addChild(menu);

    int initIdx = 0;
    for (size_t i = 0; i < options.size(); i++) {
        if (options[i] == initialValue) { initIdx = static_cast<int>(i); break; }
    }
    auto cb = CycleCallback::create(std::move(onChange), options, initIdx);

    float rightEdge = width - 8.f;
    float valueW = 74.f;

    auto leftSpr = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
    if (!leftSpr) leftSpr = CCSprite::create();
    leftSpr->setScale(0.26f);
    leftSpr->setFlipX(true);
    auto leftBtn = CCMenuItemSpriteExtra::create(leftSpr, cb, menu_selector(CycleCallback::onPrev));
    leftBtn->setPosition({rightEdge - valueW - 12.f, kToggleRowH / 2.f});
    leftBtn->setUserObject(cb);
    menu->addChild(leftBtn);

    auto valLabel = CCLabelBMFont::create(
        options.empty() ? "" : options[initIdx].c_str(), "bigFont.fnt");
    valLabel->setScale(0.3f);
    valLabel->setAnchorPoint({0.5f, 0.5f});
    valLabel->setPosition({rightEdge - valueW / 2.f, kToggleRowH / 2.f});
    row->addChild(valLabel);
    cb->m_valueLabel = valLabel;

    auto rightSpr = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
    if (!rightSpr) rightSpr = CCSprite::create();
    rightSpr->setScale(0.26f);
    auto rightBtn = CCMenuItemSpriteExtra::create(rightSpr, cb, menu_selector(CycleCallback::onNext));
    rightBtn->setPosition({rightEdge + 4.f, kToggleRowH / 2.f});
    menu->addChild(rightBtn);

    return row;
}

// Small label on the left + text input filling the right side
CCNode* makeInputRow(const char* title, const char* placeholder,
                     std::string const& value, int maxChars,
                     std::function<void(std::string const&)> onChange,
                     float width, TextInput** outInput) {
    auto row = CCNode::create();
    row->setContentSize({width, kInputRowH});
    row->setAnchorPoint({0.f, 0.f});

    auto lbl = CCLabelBMFont::create(title, "chatFont.fnt");
    lbl->setScale(0.44f);
    lbl->setColor({200, 206, 216});
    lbl->setAnchorPoint({0.f, 0.5f});
    lbl->setPosition({2.f, kInputRowH / 2.f});
    row->addChild(lbl);

    float inputW = width * 0.62f;
    auto input = TextInput::create(inputW / 0.72f, placeholder, "chatFont.fnt");
    input->setCommonFilter(CommonFilter::Any);
    input->setMaxCharCount(maxChars);
    input->setString(value);
    input->setScale(0.72f);
    input->setPosition({width - inputW / 2.f - 2.f, kInputRowH / 2.f});
    input->setCallback(std::move(onChange));
    row->addChild(input);

    if (outInput) *outInput = input;
    return row;
}

// Section card: dark rounded background + gold header + separator + rows
CCNode* makeCard(const char* title, std::vector<CCNode*> const& rows, float width) {
    float innerH = 0.f;
    for (auto* r : rows) innerH += r->getContentSize().height + kRowGap;
    float totalH = kCardHeaderH + innerH + 6.f;

    auto card = CCNode::create();
    card->setContentSize({width, totalH});
    card->setAnchorPoint({0.f, 0.f});

    auto bg = CCScale9Sprite::create("square02b_001.png");
    bg->setContentSize({width, totalH});
    bg->setColor({0, 0, 0});
    bg->setOpacity(70);
    bg->setPosition({width / 2.f, totalH / 2.f});
    card->addChild(bg, -1);

    auto header = CCLabelBMFont::create(title, "goldFont.fnt");
    header->setScale(0.42f);
    header->setAnchorPoint({0.f, 0.5f});
    header->setPosition({kCardPadX, totalH - kCardHeaderH / 2.f - 2.f});
    card->addChild(header);

    auto sep = CCLayerColor::create({255, 255, 255, 40}, width - kCardPadX * 2.f, 0.6f);
    sep->setPosition({kCardPadX, totalH - kCardHeaderH - 2.f});
    card->addChild(sep);

    float y = totalH - kCardHeaderH - 5.f;
    for (auto* r : rows) {
        y -= r->getContentSize().height;
        r->setPosition({kCardPadX, y});
        card->addChild(r);
        y -= kRowGap;
    }
    return card;
}

std::string upperCopy(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

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

    // "touch" = persist + push to Discord + refresh the live preview card
    auto touch = [this] { kick(); this->updatePreview(); };

    // --- Live preview card (Discord-style), fixed above the scroll area ---
    float previewH = 52.f;
    float previewTop = content.height - 32.f;
    {
        auto card = CCNode::create();
        card->setContentSize({w, previewH});
        card->setAnchorPoint({0.f, 1.f});
        card->setPosition({18.f, previewTop});
        m_mainLayer->addChild(card, 6);

        auto bg = CCScale9Sprite::create("square02b_001.png");
        bg->setContentSize({w, previewH});
        bg->setColor({20, 22, 30});
        bg->setPosition({w / 2.f, previewH / 2.f});
        card->addChild(bg, -1);

        // Mod logo as the "large image" of the presence
        float textX = 14.f;
        if (auto logo = CCSprite::create("logo.png"_spr)) {
            float target = 38.f;
            float s = target / std::max(logo->getContentSize().width, 1.f);
            logo->setScale(s);
            logo->setPosition({8.f + target / 2.f, previewH / 2.f});
            card->addChild(logo);
            textX = 8.f + target + 8.f;
        }

        m_prevHeader = CCLabelBMFont::create("PLAYING GEOMETRY DASH", "chatFont.fnt");
        m_prevHeader->setScale(0.32f);
        m_prevHeader->setColor({145, 155, 172});
        m_prevHeader->setAnchorPoint({0.f, 0.5f});
        m_prevHeader->setPosition({textX, previewH - 11.f});
        card->addChild(m_prevHeader);

        m_prevDetails = CCLabelBMFont::create("", "chatFont.fnt");
        m_prevDetails->setScale(0.42f);
        m_prevDetails->setColor({235, 238, 245});
        m_prevDetails->setAnchorPoint({0.f, 0.5f});
        m_prevDetails->setPosition({textX, previewH / 2.f});
        card->addChild(m_prevDetails);

        m_prevState = CCLabelBMFont::create("", "chatFont.fnt");
        m_prevState->setScale(0.36f);
        m_prevState->setColor({170, 178, 192});
        m_prevState->setAnchorPoint({0.f, 0.5f});
        m_prevState->setPosition({textX, 11.f});
        card->addChild(m_prevState);

        m_prevTime = CCLabelBMFont::create("00:42 elapsed", "chatFont.fnt");
        m_prevTime->setScale(0.32f);
        m_prevTime->setColor({87, 195, 120});
        m_prevTime->setAnchorPoint({1.f, 0.5f});
        m_prevTime->setPosition({w - 10.f, 11.f});
        card->addChild(m_prevTime);

        auto tag = CCLabelBMFont::create("PREVIEW", "bigFont.fnt");
        tag->setScale(0.2f);
        tag->setColor({120, 128, 145});
        tag->setAnchorPoint({1.f, 0.5f});
        tag->setPosition({w - 10.f, previewH - 10.f});
        card->addChild(tag);
    }

    // --- Section cards ---
    float iw = w - kCardPadX * 2.f; // row width inside a card

    std::vector<CCNode*> cards;

    cards.push_back(makeCard("General", {
        makeToggleRow("Enable Rich Presence",
            "Show your GD activity on your Discord profile",
            gset<bool>("discord-rpc-enabled"),
            [touch](bool v) { sset<bool>("discord-rpc-enabled", v); touch(); }, iw),
        makeToggleRow("Private Mode",
            "Hide level names and details, keep it minimal",
            gset<bool>("discord-rpc-private-mode"),
            [touch](bool v) { sset<bool>("discord-rpc-private-mode", v); touch(); }, iw),
        makeToggleRow("Idle When Unfocused",
            "Switch to idle when the game loses focus",
            gset<bool>("discord-rpc-idle-when-unfocused"),
            [touch](bool v) { sset<bool>("discord-rpc-idle-when-unfocused", v); touch(); }, iw),
    }, w));

    cards.push_back(makeCard("Display", {
        makeToggleRow("Show Elapsed Time",
            "Display how long you have been playing",
            gset<bool>("discord-rpc-show-timestamp"),
            [touch](bool v) { sset<bool>("discord-rpc-show-timestamp", v); touch(); }, iw),
        makeToggleRow("Show Level Progress",
            "Include percent and attempts while in a level",
            gset<bool>("discord-rpc-show-progress"),
            [touch](bool v) { sset<bool>("discord-rpc-show-progress", v); touch(); }, iw),
        makeToggleRow("Include Paimbnails Features",
            "Mention Paimbnails screens like the hub or editor",
            gset<bool>("discord-rpc-include-paimbnails-features"),
            [touch](bool v) { sset<bool>("discord-rpc-include-paimbnails-features", v); touch(); }, iw),
        makeCycleRow("Activity Type",
            "How the first line reads on your profile",
            gset<std::string>("discord-rpc-activity-type"),
            {"Playing", "Listening", "Watching", "Competing"},
            [touch](std::string const& v) { sset<std::string>("discord-rpc-activity-type", v); touch(); }, iw),
    }, w));

    cards.push_back(makeCard("Custom Text", {
        makeToggleRow("Override Details Line",
            "Replace the first text line with your own",
            gset<bool>("discord-rpc-override-details"),
            [touch](bool v) { sset<bool>("discord-rpc-override-details", v); touch(); }, iw),
        makeInputRow("Details", "Playing my own way",
            gset<std::string>("discord-rpc-custom-details"), 128,
            [touch](std::string const& v) { sset<std::string>("discord-rpc-custom-details", v); touch(); },
            iw, &m_detailsInput),
        makeToggleRow("Override State Line",
            "Replace the second text line with your own",
            gset<bool>("discord-rpc-override-state"),
            [touch](bool v) { sset<bool>("discord-rpc-override-state", v); touch(); }, iw),
        makeInputRow("State", "With Paimon by my side",
            gset<std::string>("discord-rpc-custom-state"), 128,
            [touch](std::string const& v) { sset<std::string>("discord-rpc-custom-state", v); touch(); },
            iw, &m_stateInput),
    }, w));

    cards.push_back(makeCard("Advanced", {
        makeInputRow("Banner hover text", "Paimbnails Rich Presence",
            gset<std::string>("discord-rpc-large-text"), 128,
            [touch](std::string const& v) { sset<std::string>("discord-rpc-large-text", v); touch(); },
            iw, &m_largeTextInput),
        makeInputRow("Large image key", "paimbnails",
            gset<std::string>("discord-rpc-large-image-key"), 128,
            [touch](std::string const& v) { sset<std::string>("discord-rpc-large-image-key", v); touch(); },
            iw, &m_largeImageKeyInput),
        makeInputRow("Small image key", "paimbnails",
            gset<std::string>("discord-rpc-small-image-key"), 128,
            [touch](std::string const& v) { sset<std::string>("discord-rpc-small-image-key", v); touch(); },
            iw, &m_smallImageKeyInput),
    }, w));

    // --- Scroll area with the cards ---
    float scrollBottom = 40.f;
    float scrollTop = previewTop - previewH - 6.f;
    auto scrollSize = CCSize{w, scrollTop - scrollBottom};
    auto scroll = ScrollLayer::create(scrollSize);
    scroll->setPosition({18.f, scrollBottom});
    scroll->setTouchEnabled(true);
    scroll->setTouchPriority(childTouchPrio());
    m_mainLayer->addChild(scroll, 5);

    auto contentLayer = scroll->m_contentLayer;
    float totalH = 0.f;
    for (auto* c : cards) totalH += c->getContentSize().height + 5.f;
    totalH = std::max(totalH, scrollSize.height);
    contentLayer->setContentSize({w, totalH});

    float y = totalH;
    for (auto* c : cards) {
        y -= c->getContentSize().height;
        c->setPosition({0.f, y});
        contentLayer->addChild(c);
        y -= 5.f;
    }
    scroll->scrollToTop();

    // --- Footer buttons ---
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

    this->updatePreview();

    paimon::markDynamicPopup(this);
    return true;
}

void DiscordConfigPopup::updatePreview() {
    if (!m_prevHeader) return;

    bool enabled = gset<bool>("discord-rpc-enabled");
    bool priv = gset<bool>("discord-rpc-private-mode");

    auto type = gset<std::string>("discord-rpc-activity-type");
    if (type.empty()) type = "Playing";
    m_prevHeader->setString(upperCopy(type + " Geometry Dash").c_str());

    std::string details;
    std::string state;
    if (!enabled) {
        details = "Rich Presence is disabled";
    } else if (priv) {
        details = "Playing Geometry Dash";
        state = "(private mode: no extra info)";
    } else {
        if (gset<bool>("discord-rpc-override-details")) {
            details = gset<std::string>("discord-rpc-custom-details");
        }
        if (details.empty()) details = "Browsing the menus";

        if (gset<bool>("discord-rpc-override-state")) {
            state = gset<std::string>("discord-rpc-custom-state");
        }
        if (state.empty() && gset<bool>("discord-rpc-show-progress")) {
            state = "Stereo Madness (34%, 12 attempts)";
        }
    }
    m_prevDetails->setString(details.c_str());
    m_prevState->setString(state.c_str());

    m_prevTime->setVisible(enabled && gset<bool>("discord-rpc-show-timestamp"));
    m_prevHeader->setOpacity(enabled ? 255 : 120);
    m_prevDetails->setOpacity(enabled ? 255 : 140);
}

void DiscordConfigPopup::onExit() {
    DiscordPresenceManager::get().refreshSoon();
    Popup::onExit();
}

void DiscordConfigPopup::onOpenGeodeSettings(CCObject*) {
    geode::openSettingsPopup(Mod::get(), false);
}

void DiscordConfigPopup::onRefreshPresence(CCObject*) {
    DiscordPresenceManager::get().refreshSoon();
    PaimonNotify::create("Rich Presence refreshed.", NotificationIcon::Success)->show();
}

void DiscordConfigPopup::onResetDefaults(CCObject*) {
    PopupManager::get().quickPopup("Reset Discord RPC",
        "Reset all Discord RPC settings to their defaults?",
        "Cancel", "Reset",
        [this](auto, bool btn2) {
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
            this->updatePreview();
            PaimonNotify::create("Discord RPC reset to defaults.", NotificationIcon::Success)->show();
        }).showInstant();
}

} // namespace paimon::discord
