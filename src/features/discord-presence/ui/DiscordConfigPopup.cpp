#include "DiscordConfigPopup.hpp"

#include "../services/DiscordPresenceManager.hpp"
#include "../../../core/Settings.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/WebHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/utils/web.hpp>
#include "../../../ui/PaiConfigKit.hpp"

#include <cctype>
#include <filesystem>

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::discord {

namespace {

constexpr float kPopupW = 380.f;
constexpr float kPopupH = 285.f;

constexpr float kToggleRowH = 24.f;
constexpr float kInputRowH  = 22.f;
constexpr float kCardHeaderH = 15.f;
constexpr float kCardPadX = 7.f;
constexpr float kRowGap = 2.f;

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

bool isExternalUrl(std::string const& v) {
    return v.rfind("https://", 0) == 0 || v.rfind("http://", 0) == 0 || v.rfind("mp:", 0) == 0;
}

// Upload anonymously with an HTTP/1.1 retry; fall back to 0x0.st on failure.
void uploadToCatbox(std::vector<uint8_t> const& data, std::string const& filename,
                    std::function<void(bool, std::string)> cb) {
    if (data.empty()) {
        if (cb) cb(false, "Empty file");
        return;
    }
    std::string ext = "png";
    auto dot = filename.rfind('.');
    if (dot != std::string::npos && dot + 1 < filename.size()) {
        ext = filename.substr(dot + 1);
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    std::string mime = "image/png";
    if (ext == "jpg" || ext == "jpeg") mime = "image/jpeg";
    else if (ext == "webp") mime = "image/webp";
    else if (ext == "gif") mime = "image/gif";
    else if (ext == "bmp") mime = "image/bmp";

    auto sharedData = std::make_shared<std::vector<uint8_t>>(data);
    auto sharedCb = std::make_shared<std::function<void(bool, std::string)>>(std::move(cb));

    auto isHttp2Error = [](geode::utils::web::WebResponse const& res, std::string const& body) {
        std::string msg = std::string(res.errorMessage());
        std::string lower = msg;
        for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower.find("http/2") != std::string::npos || lower.find("http2") != std::string::npos) return true;
        if (res.code() == 0 && !msg.empty()) return true;
        if (res.code() == 0 && body.empty()) return true;
        return false;
    };

    auto extractError = [](geode::utils::web::WebResponse const& res) {
        std::string body = res.string().unwrapOr("");
        std::string err = std::string(res.errorMessage());
        if (!body.empty()) {
            if (body.size() > 300) body.resize(300);
            return std::string("HTTP ") + std::to_string(res.code()) + ": " + body;
        }
        if (!err.empty()) {
            if (err.size() > 300) err.resize(300);
            return std::string("HTTP ") + std::to_string(res.code()) + " (" + err + ")";
        }
        return std::string("HTTP ") + std::to_string(res.code());
    };

    std::function<void()> tryFallback;
    tryFallback = [sharedData, filename, mime, sharedCb, extractError]() {
        web::MultipartForm form2;
        form2.file("file", std::span<uint8_t const>(*sharedData), filename, mime);
        auto req2 = web::WebRequest();
        req2.timeout(std::chrono::seconds(30));
        req2.userAgent("Paimbnails/2.x (Geode)");
        req2.version(geode::utils::web::HttpVersion::VERSION_1_1);
        req2.acceptEncoding("gzip, deflate");
        req2.bodyMultipart(form2);
        WebHelper::dispatch(std::move(req2), "POST", "https://0x0.st",
            [sharedCb, extractError](geode::utils::web::WebResponse res) mutable {
                if (!res.ok()) {
                    if (sharedCb && *sharedCb) (*sharedCb)(false, extractError(res) + " (catbox + fallback fallaron)");
                    return;
                }
                std::string url = res.string().unwrapOr("");
                while (!url.empty() && std::isspace(static_cast<unsigned char>(url.front()))) url.erase(url.begin());
                while (!url.empty() && std::isspace(static_cast<unsigned char>(url.back()))) url.pop_back();
                if (url.rfind("https://", 0) != 0) {
                    if (sharedCb && *sharedCb) (*sharedCb)(false, url.empty() ? "Respuesta vacia del fallback (0x0.st)" : url);
                    return;
                }
                if (sharedCb && *sharedCb) (*sharedCb)(true, url);
            });
    };

    auto tryCatbox = std::make_shared<std::function<void(int)>>();
    *tryCatbox = [sharedData, filename, mime, sharedCb, isHttp2Error, extractError, tryFallback, tryCatbox](int attempt) mutable {
        web::MultipartForm form;
        form.param("reqtype", "fileupload");
        form.file("fileToUpload", std::span<uint8_t const>(*sharedData), filename, mime);

        auto req = web::WebRequest();
        req.timeout(std::chrono::seconds(30));
        req.userAgent("Paimbnails/2.x (Geode)");
        req.version(geode::utils::web::HttpVersion::VERSION_1_1);
        req.acceptEncoding("gzip, deflate");
        req.bodyMultipart(form);

        WebHelper::dispatch(std::move(req), "POST", "https://catbox.moe/user/api.php",
            [sharedCb, isHttp2Error, extractError, tryFallback, tryCatbox, attempt](geode::utils::web::WebResponse res) mutable {
                if (!res.ok()) {
                    std::string body = res.string().unwrapOr("");
                    bool http2 = isHttp2Error(res, body);
                    if (http2 && attempt == 0) {
                        (*tryCatbox)(1);
                        return;
                    }
                    if (attempt >= 1) {
                        tryFallback();
                        return;
                    }
                    tryFallback();
                    return;
                }
                std::string url = res.string().unwrapOr("");
                while (!url.empty() && std::isspace(static_cast<unsigned char>(url.front()))) url.erase(url.begin());
                while (!url.empty() && std::isspace(static_cast<unsigned char>(url.back()))) url.pop_back();
                if (url.rfind("https://", 0) != 0) {
                    if (url.empty()) {
                        if (attempt == 0) { (*tryCatbox)(1); return; }
                        tryFallback(); return;
                    }
                    if (sharedCb && *sharedCb) (*sharedCb)(false, url);
                    return;
                }
                if (sharedCb && *sharedCb) (*sharedCb)(true, url);
            });
    };

    (*tryCatbox)(0);
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

CCNode* makeToggleRow(const char* title, const char* desc, bool value,
                      std::function<void(bool)> onChange, float width) {
    auto row = CCNode::create();
    row->setContentSize({width, kToggleRowH});
    row->setAnchorPoint({0.f, 0.f});

    auto lbl = CCLabelBMFont::create(title, "chatFont.fnt");
    lbl->setScale(0.46f);
    lbl->setColor({240, 240, 240});
    lbl->setAnchorPoint({0.f, 0.5f});
    lbl->setPosition({2.f, kToggleRowH - 7.f});
    row->addChild(lbl);

    auto sub = CCLabelBMFont::create(desc, "chatFont.fnt");
    sub->setScale(0.32f);
    sub->setColor({155, 163, 178});
    sub->setAnchorPoint({0.f, 0.5f});
    sub->setPosition({2.f, 6.f});
    row->addChild(sub);

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    row->addChild(menu);

    auto cb = ToggleCallback::create(std::move(onChange));
    auto toggler = CCMenuItemToggler::createWithStandardSprites(
        cb, menu_selector(ToggleCallback::onToggle), 0.44f);
    cb->m_toggler = toggler;
    toggler->toggle(value);
    toggler->setPosition({width - 13.f, kToggleRowH / 2.f});
    toggler->setUserObject(cb);
    menu->addChild(toggler);

    return row;
}

CCNode* makeCycleRow(const char* title, const char* desc,
                     std::string const& initialValue,
                     std::vector<std::string> const& options,
                     std::function<void(std::string const&)> onChange,
                     float width) {
    auto row = CCNode::create();
    row->setContentSize({width, kToggleRowH});
    row->setAnchorPoint({0.f, 0.f});

    auto lbl = CCLabelBMFont::create(title, "chatFont.fnt");
    lbl->setScale(0.46f);
    lbl->setColor({240, 240, 240});
    lbl->setAnchorPoint({0.f, 0.5f});
    lbl->setPosition({2.f, kToggleRowH - 7.f});
    row->addChild(lbl);

    auto sub = CCLabelBMFont::create(desc, "chatFont.fnt");
    sub->setScale(0.32f);
    sub->setColor({155, 163, 178});
    sub->setAnchorPoint({0.f, 0.5f});
    sub->setPosition({2.f, 6.f});
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

    float rightEdge = width - 6.f;
    float valueW = 68.f;

    auto leftSpr = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
    if (!leftSpr) leftSpr = CCSprite::create();
    leftSpr->setScale(0.23f);
    leftSpr->setFlipX(true);
    auto leftBtn = CCMenuItemSpriteExtra::create(leftSpr, cb, menu_selector(CycleCallback::onPrev));
    leftBtn->setPosition({rightEdge - valueW - 11.f, kToggleRowH / 2.f});
    leftBtn->setUserObject(cb);
    menu->addChild(leftBtn);

    auto valLabel = CCLabelBMFont::create(
        options.empty() ? "" : options[initIdx].c_str(), "bigFont.fnt");
    valLabel->setScale(0.27f);
    valLabel->setAnchorPoint({0.5f, 0.5f});
    valLabel->setPosition({rightEdge - valueW / 2.f, kToggleRowH / 2.f});
    row->addChild(valLabel);
    cb->m_valueLabel = valLabel;

    auto rightSpr = CCSprite::createWithSpriteFrameName("navArrowBtn_001.png");
    if (!rightSpr) rightSpr = CCSprite::create();
    rightSpr->setScale(0.23f);
    auto rightBtn = CCMenuItemSpriteExtra::create(rightSpr, cb, menu_selector(CycleCallback::onNext));
    rightBtn->setPosition({rightEdge + 4.f, kToggleRowH / 2.f});
    menu->addChild(rightBtn);

    return row;
}

CCNode* makeInputRow(const char* title, const char* placeholder,
                     std::string const& value, int maxChars,
                     std::function<void(std::string const&)> onChange,
                     float width, TextInput** outInput) {
    auto row = CCNode::create();
    row->setContentSize({width, kInputRowH});
    row->setAnchorPoint({0.f, 0.f});

    auto lbl = CCLabelBMFont::create(title, "chatFont.fnt");
    lbl->setScale(0.40f);
    lbl->setColor({200, 206, 216});
    lbl->setAnchorPoint({0.f, 0.5f});
    lbl->setPosition({2.f, kInputRowH / 2.f});
    row->addChild(lbl);

    float inputW = width * 0.58f;
    auto input = TextInput::create(inputW / 0.68f, placeholder, "chatFont.fnt");
    input->setCommonFilter(CommonFilter::Any);
    input->setMaxCharCount(maxChars);
    input->setString(value);
    input->setScale(0.68f);
    input->setPosition({width - inputW / 2.f - 2.f, kInputRowH / 2.f});
    input->setCallback(std::move(onChange));
    row->addChild(input);

    if (outInput) *outInput = input;
    return row;
}

// Let each user import a local image and upload it as a Discord external asset.
CCNode* makeImagePickerRow(const char* title, const char* placeholder,
                           std::string const& value, int maxChars,
                           std::function<void(std::string const&)> onChange,
                           std::function<void()> onPick,
                           float width, TextInput** outInput) {
    auto row = CCNode::create();
    row->setContentSize({width, kInputRowH});
    row->setAnchorPoint({0.f, 0.f});

    auto lbl = CCLabelBMFont::create(title, "chatFont.fnt");
    lbl->setScale(0.40f);
    lbl->setColor({200, 206, 216});
    lbl->setAnchorPoint({0.f, 0.5f});
    lbl->setPosition({2.f, kInputRowH / 2.f});
    row->addChild(lbl);

    float btnSize = 18.f;
    float gap = 3.f;
    float inputW = width * 0.52f;
    float inputX = width - inputW - btnSize - gap - 2.f;

    auto input = TextInput::create(inputW / 0.68f, placeholder, "chatFont.fnt");
    input->setCommonFilter(CommonFilter::Any);
    input->setMaxCharCount(maxChars);
    input->setString(value);
    input->setScale(0.68f);
    input->setPosition({inputX + inputW / 2.f, kInputRowH / 2.f});
    input->setCallback(std::move(onChange));
    row->addChild(input);
    if (outInput) *outInput = input;

    auto menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    row->addChild(menu);

    CCSprite* icon = nullptr;
    if (auto spr = CCSprite::createWithSpriteFrameName("folderIcon_001.png")) {
        icon = spr;
        icon->setScale(0.42f);
    } else {
        icon = CCSprite::create("GJ_button_01.png");
        icon->setScale(0.18f);
    }

    struct PickCallback : CCObject {
        std::function<void()> fn;
        static PickCallback* create(std::function<void()> f) {
            auto* r = new PickCallback();
            r->fn = std::move(f);
            r->autorelease();
            return r;
        }
        void onPick(CCObject*) { if (fn) fn(); }
    };
    auto cb = PickCallback::create(std::move(onPick));
    auto btn = CCMenuItemSpriteExtra::create(icon, cb, menu_selector(PickCallback::onPick));
    btn->setPosition({width - btnSize / 2.f - 2.f, kInputRowH / 2.f});
    btn->setUserObject(cb);
    auto bg = CCScale9Sprite::create("square02b_001.png");
    if (bg) {
        bg->setContentSize({btnSize + 4.f, btnSize + 4.f});
        bg->setColor({255, 255, 255});
        bg->setOpacity(18);
        bg->setPosition(btn->getPosition());
        row->addChild(bg, -1);
    }
    menu->addChild(btn);


    return row;
}

CCNode* makeCard(const char* title, std::vector<CCNode*> const& rows, float width) {
    float innerH = 0.f;
    for (auto* r : rows) innerH += r->getContentSize().height + kRowGap;
    float totalH = kCardHeaderH + innerH + 5.f;

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
    header->setScale(0.38f);
    header->setAnchorPoint({0.f, 0.5f});
    header->setPosition({kCardPadX, totalH - kCardHeaderH / 2.f - 1.f});
    card->addChild(header);

    auto sep = CCLayerColor::create({255, 255, 255, 40}, width - kCardPadX * 2.f, 0.6f);
    sep->setPosition({kCardPadX, totalH - kCardHeaderH - 1.f});
    card->addChild(sep);

    float y = totalH - kCardHeaderH - 4.f;
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

}

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
    float w = content.width - 30.f;

// Touch persists, pushes to Discord, and refreshes the preview.
    auto touch = [this] { kick(); this->updatePreview(); };

    float previewH = 46.f;
    float previewTop = content.height - 28.f;
    {
        auto card = CCNode::create();
        card->setContentSize({w, previewH});
        card->setAnchorPoint({0.f, 1.f});
        card->setPosition({15.f, previewTop});
        m_mainLayer->addChild(card, 6);

        auto bg = CCScale9Sprite::create("square02b_001.png");
        bg->setContentSize({w, previewH});
        bg->setColor({20, 22, 30});
        bg->setPosition({w / 2.f, previewH / 2.f});
        card->addChild(bg, -1);

        float textX = 14.f;
        float logoSz = 34.f;
        if (auto logo = CCSprite::create("logo.png"_spr)) {
            float s = logoSz / std::max(logo->getContentSize().width, 1.f);
            logo->setScale(s);
            logo->setPosition({7.f + logoSz / 2.f, previewH / 2.f + 1.f});
            card->addChild(logo);
            auto badgeBg = CCSprite::create("GJ_button_01.png");
            if (badgeBg) {
                badgeBg->setScale(0.18f);
                badgeBg->setPosition({7.f + logoSz - 5.f, 10.f});
                badgeBg->setOpacity(210);
                card->addChild(badgeBg, 2);
            }
            textX = 7.f + logoSz + 7.f;
        }

        m_prevHeader = CCLabelBMFont::create("PLAYING GEOMETRY DASH", "chatFont.fnt");
        m_prevHeader->setScale(0.30f);
        m_prevHeader->setColor({145, 155, 172});
        m_prevHeader->setAnchorPoint({0.f, 0.5f});
        m_prevHeader->setPosition({textX, previewH - 10.f});
        card->addChild(m_prevHeader);

        m_prevDetails = CCLabelBMFont::create("", "chatFont.fnt");
        m_prevDetails->setScale(0.38f);
        m_prevDetails->setColor({235, 238, 245});
        m_prevDetails->setAnchorPoint({0.f, 0.5f});
        m_prevDetails->setPosition({textX, previewH / 2.f + 1.f});
        card->addChild(m_prevDetails);

        m_prevState = CCLabelBMFont::create("", "chatFont.fnt");
        m_prevState->setScale(0.33f);
        m_prevState->setColor({170, 178, 192});
        m_prevState->setAnchorPoint({0.f, 0.5f});
        m_prevState->setPosition({textX, 10.f});
        card->addChild(m_prevState);

        m_prevTime = CCLabelBMFont::create("00:42 elapsed", "chatFont.fnt");
        m_prevTime->setScale(0.30f);
        m_prevTime->setColor({87, 195, 120});
        m_prevTime->setAnchorPoint({1.f, 0.5f});
        m_prevTime->setPosition({w - 9.f, 10.f});
        card->addChild(m_prevTime);

        m_prevSmall = CCLabelBMFont::create("", "chatFont.fnt");
        m_prevSmall->setScale(0.26f);
        m_prevSmall->setColor({120, 180, 255});
        m_prevSmall->setAnchorPoint({0.f, 0.5f});
        m_prevSmall->setPosition({textX, 10.f});
        m_prevSmall->setVisible(false);
        card->addChild(m_prevSmall);

        auto tag = CCLabelBMFont::create("PREVIEW", "bigFont.fnt");
        tag->setScale(0.18f);
        tag->setColor({120, 128, 145});
        tag->setAnchorPoint({1.f, 0.5f});
        tag->setPosition({w - 9.f, previewH - 9.f});
        card->addChild(tag);
    }

    float iw = w - kCardPadX * 2.f;

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

// Pick a local image and upload it as a per-user custom Discord asset.
    auto makePickHandler = [this, touch](bool isLarge) {
    // Capture weakly so the popup may close before completion.
        auto* self = this;
    // Keep the popup alive through the callback.
        pt::pickImage([self, isLarge, touch](geode::Result<std::optional<std::filesystem::path>> result) {
            if (!self) return;
            if (result.isErr()) {
                PaimonNotify::create("No se pudo abrir el selector", NotificationIcon::Error)->show();
                return;
            }
            auto opt = result.unwrap();
    if (!opt || opt->empty()) return;

            auto srcPath = *opt;
            std::string fname = srcPath.filename().string();
            if (fname.empty()) fname = (isLarge ? "large.png" : "small.png");

            auto imported = paimon::assets::importToBucket(srcPath, "discord_rpc", paimon::assets::Kind::Image);
            if (!imported.success) {
                PaimonNotify::create("No se pudo importar: " + imported.error, NotificationIcon::Error)->show();
                return;
            }

            auto data = ImageLoadHelper::readBinaryFile(imported.path, 8);
            if (data.empty()) {
                PaimonNotify::create("Archivo vacio o muy grande (>8MB)", NotificationIcon::Error)->show();
                return;
            }

            std::string key = isLarge ? "discord-rpc-large-image-key" : "discord-rpc-small-image-key";
            TextInput* targetInput = isLarge ? self->m_largeImageKeyInput : self->m_smallImageKeyInput;
            std::string prevVal = targetInput ? std::string(targetInput->getString()) : gset<std::string>(key.c_str());
            if (prevVal == "subiendo...") prevVal = gset<std::string>(key.c_str());

            if (targetInput) targetInput->setString("subiendo...");
            PaimonNotify::create("Subiendo imagen...", NotificationIcon::Info)->show();

            uploadToCatbox(data, fname, [self, key, targetInput, touch, prevVal](bool ok, std::string urlOrErr) {
                if (!self) return;
                if (!ok) {
                    std::string msg = urlOrErr;
                    if (msg.size() > 220) msg.resize(220);
                    PaimonNotify::create("Error al subir: " + msg, NotificationIcon::Error)->show();
                    if (targetInput) {
    // Restore the previous URL on failure so presence keeps working.
                        if (!prevVal.empty() && prevVal != "subiendo...") targetInput->setString(prevVal);
                        else targetInput->setString("");
                    }
                    return;
                }
    // Discord fetches the stored HTTPS URL as a per-user external image.
                sset<std::string>(key.c_str(), urlOrErr);
                if (targetInput) targetInput->setString(urlOrErr);
                touch();
                PaimonNotify::create("Imagen subida! Visible para todos en Discord.", NotificationIcon::Success)->show();
            });
        });
    };

    cards.push_back(makeCard("Images", {
        makeImagePickerRow("Large image", "paimbnails / https://...",
            gset<std::string>("discord-rpc-large-image-key"), 256,
            [touch](std::string const& v) { sset<std::string>("discord-rpc-large-image-key", v); touch(); },
            [makePickHandler]() { makePickHandler(true); },
            iw, &m_largeImageKeyInput),
        makeInputRow("Large hover", "Paimbnails Rich Presence",
            gset<std::string>("discord-rpc-large-text"), 128,
            [touch](std::string const& v) { sset<std::string>("discord-rpc-large-text", v); touch(); },
            iw, &m_largeTextInput),
        makeImagePickerRow("Small image", "auto / https://... / key",
            gset<std::string>("discord-rpc-small-image-key"), 256,
            [touch](std::string const& v) { sset<std::string>("discord-rpc-small-image-key", v); touch(); },
            [makePickHandler]() { makePickHandler(false); },
            iw, &m_smallImageKeyInput),
        makeInputRow("Small hover", "auto",
            gset<std::string>("discord-rpc-small-text"), 128,
            [touch](std::string const& v) { sset<std::string>("discord-rpc-small-text", v); touch(); },
            iw, &m_smallTextInput),
    }, w));

    {
        auto hint = CCLabelBMFont::create("Tip: usa el boton carpeta para elegir imagen local.", "chatFont.fnt");
        hint->setScale(0.30f);
        hint->setColor({140, 155, 175});
        hint->setAnchorPoint({0.f, 0.5f});
        auto hintCard = CCNode::create();
        hintCard->setContentSize({w, 10.f});
        hint->setPosition({4.f, 5.f});
        hintCard->addChild(hint);

        auto hint2 = CCLabelBMFont::create("Cada usuario sube la suya (catbox) -> visible en Discord.", "chatFont.fnt");
        hint2->setScale(0.28f);
        hint2->setColor({110, 140, 160});
        hint2->setAnchorPoint({0.f, 0.5f});
        hint2->setPosition({4.f, -3.f});
        hintCard->addChild(hint2);

        cards.push_back(hintCard);
    }

    float scrollBottom = 36.f;
    float scrollTop = previewTop - previewH - 5.f;
    auto scrollSize = CCSize{w, scrollTop - scrollBottom};
    auto scroll = ScrollLayer::create(scrollSize);
    scroll->setPosition({15.f, scrollBottom});
    m_scroll = scroll;
    m_mainLayer->addChild(scroll, 5);

    auto contentLayer = scroll->m_contentLayer;
    float totalH = 0.f;
    for (auto* c : cards) totalH += c->getContentSize().height + 4.f;
    totalH = std::max(totalH, scrollSize.height);
    contentLayer->setContentSize({w, totalH});

    float y = totalH;
    for (auto* c : cards) {
        y -= c->getContentSize().height;
        c->setPosition({0.f, y});
        contentLayer->addChild(c);
        y -= 4.f;
    }
    scroll->moveToTop();

    this->schedule(schedule_selector(DiscordConfigPopup::updateSmoothScroll));

    {
        auto footer = CCMenu::create();
        footer->setID("discord-footer-menu"_spr);
        footer->setPosition({0.f, 0.f});
        m_mainLayer->addChild(footer, 20);

        auto resetSpr = ButtonSprite::create("Reset", "bigFont.fnt", "GJ_button_06.png", 0.8f);
        resetSpr->setScale(0.38f);
        auto resetBtn = CCMenuItemSpriteExtra::create(
            resetSpr, this, menu_selector(DiscordConfigPopup::onResetDefaults));
        resetBtn->setPosition({40.f, 16.f});
        footer->addChild(resetBtn);

        auto refreshSpr = ButtonSprite::create("Refresh", "bigFont.fnt", "GJ_button_05.png", 0.8f);
        refreshSpr->setScale(0.38f);
        auto refreshBtn = CCMenuItemSpriteExtra::create(
            refreshSpr, this, menu_selector(DiscordConfigPopup::onRefreshPresence));
        refreshBtn->setPosition({content.width / 2.f, 16.f});
        footer->addChild(refreshBtn);

        auto geodeSpr = ButtonSprite::create("Geode", "bigFont.fnt", "GJ_button_04.png", 0.8f);
        geodeSpr->setScale(0.38f);
        auto geodeBtn = CCMenuItemSpriteExtra::create(
            geodeSpr, this, menu_selector(DiscordConfigPopup::onOpenGeodeSettings));
        geodeBtn->setPosition({content.width - 40.f, 16.f});
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

    bool showTime = enabled && gset<bool>("discord-rpc-show-timestamp");
    m_prevTime->setVisible(showTime);

    if (m_prevSmall) {
        auto smallTxt = gset<std::string>("discord-rpc-small-text");
        auto smallKey = gset<std::string>("discord-rpc-small-image-key");
// Show only the final path component for long HTTPS URLs.
        std::string displayKey = smallKey;
        if (isExternalUrl(displayKey)) {
            auto lastSlash = displayKey.rfind('/');
            if (lastSlash != std::string::npos && lastSlash + 1 < displayKey.size()) {
                displayKey = displayKey.substr(lastSlash + 1);
                if (displayKey.size() > 20) displayKey.resize(20);
            } else if (displayKey.size() > 20) {
                displayKey = displayKey.substr(0, 20) + "...";
            }
            displayKey = "[url] " + displayKey;
        }
        if (enabled && (!smallTxt.empty() || !smallKey.empty())) {
            std::string label = smallTxt.empty() ? displayKey : smallTxt;
            if (label.size() > 28) label.resize(28);
            m_prevSmall->setString(label.c_str());
            m_prevSmall->setVisible(true);
            m_prevState->setVisible(state.empty() ? false : true);
            if (!state.empty() && showTime) {
                m_prevState->setPositionY(14.f);
                m_prevSmall->setPositionY(5.f);
            }
        } else {
            m_prevSmall->setVisible(false);
            m_prevState->setVisible(!state.empty());
            m_prevState->setPositionY(10.f);
        }
    }

    m_prevHeader->setOpacity(enabled ? 255 : 120);
    m_prevDetails->setOpacity(enabled ? 255 : 140);
}

void DiscordConfigPopup::onExit() {
    this->unschedule(schedule_selector(DiscordConfigPopup::updateSmoothScroll));
    DiscordPresenceManager::get().refreshSoon();
    Popup::onExit();
}

void DiscordConfigPopup::scrollWheel(float x, float y) {
// Use the shared helper at a lower speed so global smooth scroll does not amplify it.
    if (paimon::configkit::queueWheelScroll(m_scroll, x, y, m_scrollTargetY, m_scrollTargetSet, 12.f)) return;
}

void DiscordConfigPopup::updateSmoothScroll(float dt) {
    paimon::configkit::stepWheelScroll(m_scroll, m_scrollTargetY, m_scrollTargetSet, dt);
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
            mod->setSavedValue<std::string>("discord-rpc-small-text", "");
            mod->setSavedValue<std::string>("discord-rpc-activity-type", "Playing");
            mod->setSavedValue("discord-rpc-show-timestamp", true);
            mod->setSavedValue("discord-rpc-override-details", false);
            mod->setSavedValue<std::string>("discord-rpc-custom-details", "");
            mod->setSavedValue("discord-rpc-override-state", false);
            mod->setSavedValue<std::string>("discord-rpc-custom-state", "");

            if (m_detailsInput) m_detailsInput->setString("");
            if (m_stateInput) m_stateInput->setString("");
            if (m_largeTextInput) m_largeTextInput->setString("");
            if (m_largeImageKeyInput) m_largeImageKeyInput->setString("");
            if (m_smallImageKeyInput) m_smallImageKeyInput->setString("");
            if (m_smallTextInput) m_smallTextInput->setString("");

            DiscordPresenceManager::get().refreshSoon();
            this->updatePreview();
            PaimonNotify::create("Discord RPC reset to defaults.", NotificationIcon::Success)->show();
        }).showInstant();
}

}
