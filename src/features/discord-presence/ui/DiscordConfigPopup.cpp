#include "../../../utils/FilesystemPath.hpp"
#include "DiscordConfigPopup.hpp"

#include "../services/DiscordPresenceManager.hpp"
#include "../services/DiscordPresenceSettings.hpp"
#include "../../../utils/MainThreadDelay.hpp"
#include "../../../utils/AtomicFileWrite.hpp"
#include <ctime>
#include "../../../core/Settings.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include "../../../utils/PaimonDrawNode.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/WebHelper.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/PopupManager.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/web.hpp>
#include "../../../ui/PaiConfigKit.hpp"

#include <cctype>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <string_view>

using namespace cocos2d;
using namespace geode::prelude;

namespace paimon::discord {

namespace {

namespace kit = paimon::configkit;
namespace ui = paimon::ui;

// main layer layout.
constexpr float kMargin = 12.f;
constexpr float kColumnGap = 10.f;
constexpr float kTitleSpace = 36.f;
constexpr float kSectionGap = 6.f;
constexpr float kScrollbarW = 6.f;
constexpr float kUpdateRowY = 47.f;
constexpr float kActionRowY = 21.f;
constexpr float kStatusY = 63.f;
constexpr float kStatusH = 22.f;
constexpr float kStatusScale = .42f;
constexpr float kInputRowH = 52.f;
constexpr float kInputScale = .8f;

// discord activity card.
constexpr float kCardPad = 9.f;
constexpr float kImageSize = 52.f;
constexpr float kPreviewButtonH = 22.f;
constexpr float kPreviewButtonGap = 4.f;
constexpr float kHoverStep = 11.f;
constexpr float kTextSlots[] = {7.f, 20.f, 31.f, 43.f};
constexpr float kImageRadius = 6.f;
constexpr float kBadgeRadius = 9.5f;
constexpr float kBadgeImageRadius = 7.5f;
// typing a url changes the reference on every keystroke; wait for a pause.
constexpr float kImageLoadDelay = .6f;
constexpr std::size_t kMaxPreviewImageBytes = 8 * 1024 * 1024;

enum PreviewImageState { kImageNone = 0, kImageLoading, kImageReady, kImageFailed };

// discord dark theme.
namespace dc {
constexpr ccColor3B card{30, 31, 34};
constexpr ccColor3B image{56, 58, 64};
constexpr ccColor3B button{78, 80, 88};
constexpr ccColor3B header{181, 186, 193};
constexpr ccColor3B name{242, 243, 245};
constexpr ccColor3B details{219, 222, 225};
constexpr ccColor3B state{181, 186, 193};
constexpr ccColor3B muted{148, 155, 164};
constexpr ccColor3B elapsed{35, 165, 90};
constexpr ccColor3B divider{63, 65, 71};
}

// card accents.
constexpr ccColor3B kGold{255, 200, 100};
constexpr ccColor3B kSky{115, 210, 255};
constexpr ccColor3B kMint{130, 235, 175};
constexpr ccColor3B kViolet{205, 160, 255};
constexpr ccColor3B kBlurple{140, 150, 255};
constexpr ccColor3B kSlate{170, 175, 190};

constexpr char const* kPresetNames[] = {"Balanced", "Detailed", "Minimal", "Private"};
// shared by balanced/detailed; minimal and private turn them off.
constexpr char const* kBroadPresetKeys[] = {
    "discord-rpc-show-level-name", "discord-rpc-show-creator", "discord-rpc-show-progress",
    "discord-rpc-show-best", "discord-rpc-show-difficulty", "discord-rpc-show-profile-names",
    "discord-rpc-show-music", "discord-rpc-include-paimbnails-features"};
// only detailed shares these.
constexpr char const* kDetailPresetKeys[] = {
    "discord-rpc-show-level-id", "discord-rpc-show-attempts", "discord-rpc-show-editor-stats",
    "discord-rpc-show-room-names", "discord-rpc-show-party-size"};

char const* presetRefreshMode(int preset) {
    return preset == 1 ? "Fast" : preset >= 2 ? "Low power" : "Balanced";
}

int matchPreset(PresenceConfiguration const& configuration) {
    for (int preset = 0; preset < 4; ++preset) {
        bool const detailed = preset == 1;
        bool const minimal = preset >= 2;
        bool match = configuration.flag("discord-rpc-private-mode") == (preset == 3) &&
            !configuration.flag("discord-rpc-share-private-levels") &&
            configuration.text("discord-rpc-refresh-mode") == presetRefreshMode(preset);
        for (auto key : kBroadPresetKeys) match = match && configuration.flag(key) == !minimal;
        for (auto key : kDetailPresetKeys) match = match && configuration.flag(key) == detailed;
        if (match) return preset;
    }
    return -1;
}

char const* presetDescription(int preset) {
    switch (preset) {
        case 0: return "Balanced: shares level, creator, progress, difficulty, profiles and music. "
                       "Hides IDs, attempts and room details.";
        case 1: return "Detailed: shares everything, including level IDs, attempts, editor objects "
                       "and PaiDraw rooms. Fast sampling.";
        case 2: return "Minimal: hides level, creator, progress, profiles, music and Paimbnails "
                       "screens. Low power sampling.";
        case 3: return "Private: only a generic Geometry Dash session. Overrides custom text "
                       "and images.";
        default: return "Custom: your privacy settings don't match a preset.";
    }
}

enum class StatusTone { Idle, Ready, Waiting, Error, Warning, Busy };

StatusTone classifyStatus(std::string_view status) {
    if (status.starts_with("Discord connected")) return StatusTone::Ready;
    if (status.starts_with("Disabled") || status.starts_with("Unavailable")) return StatusTone::Idle;
    // "Discord: <error>" and "Discord worker unavailable ..."
    if (status.starts_with("Discord")) return StatusTone::Error;
    return StatusTone::Waiting;
}

ccColor4F toneDot(StatusTone tone) {
    switch (tone) {
        case StatusTone::Ready: return {.14f, .65f, .35f, 1.f};
        case StatusTone::Waiting: return {.94f, .70f, .20f, 1.f};
        case StatusTone::Error: return {.95f, .25f, .26f, 1.f};
        case StatusTone::Warning: return {1.f, .69f, .29f, 1.f};
        case StatusTone::Busy: return {.35f, .40f, .95f, 1.f};
        default: return {.50f, .52f, .56f, 1.f};
    }
}

ccColor3B toneText(StatusTone tone) {
    switch (tone) {
        case StatusTone::Warning: return ui::palette::warning;
        case StatusTone::Error: return {255, 150, 150};
        default: return {225, 228, 235};
    }
}

// only direct links and discord media proxy references can be fetched locally;
// application asset keys live on Discord's side.
std::string previewImageUrl(std::string const& reference) {
    if (validLink(reference)) return reference;
    if (reference.starts_with("mp:") && reference.size() > 3) return "https://media.discordapp.net/" + reference.substr(3);
    return {};
}

// remembers the source text so polling doesn't re-truncate unchanged labels.
class FitState : public CCObject {
public:
    std::string value;
    float scale = 0.f;
    float width = 0.f;
    static FitState* create() {
        auto* state = new FitState();
        state->autorelease();
        return state;
    }
};

// discord truncates long lines instead of shrinking them.
void fitLabel(CCLabelBMFont* label, std::string const& value, float scale, float maxWidth) {
    if (!label) return;
    auto* state = static_cast<FitState*>(label->getUserObject("fit"_spr));
    if (!state) {
        state = FitState::create();
        label->setUserObject("fit"_spr, state);
    }
    if (state->value == value && state->scale == scale && state->width == maxWidth) return;
    state->value = value;
    state->scale = scale;
    state->width = maxWidth;
    label->setScale(scale);
    label->setString(value.c_str());
    if (label->getScaledContentSize().width <= maxWidth) return;
    std::string text = value;
    while (!text.empty()) {
        std::size_t cut = text.size() - 1;
        while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
        text.erase(cut);
        while (!text.empty() && text.back() == ' ') text.pop_back();
        label->setString((text + "...").c_str());
        if (label->getScaledContentSize().width <= maxWidth) return;
    }
}

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
        req2.timeout(asp::Duration::fromSecs(30));
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
    std::weak_ptr<std::function<void(int)>> weakTry = tryCatbox;
    *tryCatbox = [sharedData, filename, mime, sharedCb, isHttp2Error, tryFallback, weakTry](int attempt) mutable {
        auto tryCatbox = weakTry.lock();
        if (!tryCatbox) return;
        web::MultipartForm form;
        form.param("reqtype", "fileupload");
        form.file("fileToUpload", std::span<uint8_t const>(*sharedData), filename, mime);

        auto req = web::WebRequest();
        req.timeout(asp::Duration::fromSecs(30));
        req.userAgent("Paimbnails/2.x (Geode)");
        req.version(geode::utils::web::HttpVersion::VERSION_1_1);
        req.acceptEncoding("gzip, deflate");
        req.bodyMultipart(form);

        WebHelper::dispatch(std::move(req), "POST", "https://catbox.moe/user/api.php",
            [sharedCb, isHttp2Error, tryFallback, tryCatbox, attempt](geode::utils::web::WebResponse res) mutable {
                if (!res.ok()) {
                    std::string body = res.string().unwrapOr("");
                    if (isHttp2Error(res, body) && attempt == 0) {
                        (*tryCatbox)(1);
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

CCMenu* makeRowMenu(CCNode* row) {
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    row->addChild(menu, 5);
    return menu;
}

// config kit style row: title on top, full-width field below, optional picker button.
CCNode* makeInputRow(float width, char const* title, char const* placeholder,
                     std::string const& value, int maxChars,
                     std::function<void(std::string const&)> onChange,
                     TextInput** outInput = nullptr, std::function<void()> onPick = nullptr) {
    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, kInputRowH});
    row->addChild(ui::makeInset({width, kInputRowH}, kit::kRowAlpha), -1);

    auto* label = CCLabelBMFont::create(title, "bigFont.fnt");
    label->setAnchorPoint({0.f, 1.f});
    label->setColor(kit::kTitleColor);
    label->limitLabelWidth(width - 20.f, .34f, .1f);
    label->setPosition({10.f, kInputRowH - 7.f});
    row->addChild(label);

    constexpr float kPickSize = 24.f;
    float const pickSpace = onPick ? kPickSize + 6.f : 0.f;
    float const inputW = width - 20.f - pickSpace;
    auto* input = TextInput::create(inputW / kInputScale, placeholder, "chatFont.fnt");
    input->setCommonFilter(CommonFilter::TextEmojis);
    input->setMaxCharCount(maxChars);
    input->setString(value);
    input->setScale(kInputScale);
    input->setPosition({10.f + inputW / 2.f, 17.f});
    input->setCallback(std::move(onChange));
    row->addChild(input, 2);
    if (outInput) *outInput = input;

    if (onPick) {
        CCNode* face = nullptr;
        if (auto* folder = CCSprite::createWithSpriteFrameName("gj_folderBtn_001.png")) {
            folder->setScale(.6f);
            auto* circle = CircleButtonSprite::create(folder, CircleBaseColor::Green, CircleBaseSize::Medium);
            face = circle ? static_cast<CCNode*>(circle) : static_cast<CCNode*>(folder);
        } else {
            face = ui::makeButtonSprite("...", ui::Btn::Green, kPickSize, .5f, "bigFont.fnt");
        }
        float const faceW = face->getContentSize().width;
        if (faceW > 0.f) face->setScale(kPickSize / faceW);
        auto* button = CCMenuItemExt::createSpriteExtra(face, [pick = std::move(onPick)](CCMenuItemSpriteExtra*) {
            if (pick) pick();
        });
        button->setPosition({width - 10.f - kPickSize / 2.f, 17.f});
        makeRowMenu(row)->addChild(button);
    }
    return row;
}

CCNode* makeTokenGrid(float width) {
    constexpr float kPad = 8.f;
    constexpr float kGap = 4.f;
    constexpr float kChipH = 16.f;
    int const columns = width >= 240.f ? 3 : 2;
    int const count = static_cast<int>(kPresenceTokens.size());
    int const rows = (count + columns - 1) / columns;
    float const chipW = (width - kPad * 2.f - kGap * static_cast<float>(columns - 1)) / static_cast<float>(columns);
    float const height = kPad * 2.f + rows * kChipH + static_cast<float>(rows - 1) * kGap;

    auto* grid = CCNode::create();
    grid->setAnchorPoint({0.f, 0.f});
    grid->setContentSize({width, height});
    grid->addChild(ui::makeInset({width, height}, kit::kRowAlpha), -1);
    for (int i = 0; i < count; ++i) {
        int const column = i % columns;
        int const row = i / columns;
        float const x = kPad + static_cast<float>(column) * (chipW + kGap);
        float const y = height - kPad - static_cast<float>(row + 1) * kChipH - static_cast<float>(row) * kGap;
        auto* chip = ui::makeInset({chipW, kChipH}, 110);
        chip->setPosition({x, y});
        grid->addChild(chip);
        auto token = "{" + std::string(kPresenceTokens[static_cast<std::size_t>(i)]) + "}";
        auto* label = CCLabelBMFont::create(token.c_str(), "chatFont.fnt");
        label->setColor(ui::palette::gold);
        label->limitLabelWidth(chipW - 8.f, .5f, .3f);
        label->setPosition({x + chipW / 2.f, y + kChipH / 2.f + .5f});
        grid->addChild(label, 1);
    }
    return grid;
}

}

DiscordConfigPopup* DiscordConfigPopup::create() {
    auto* popup = new DiscordConfigPopup;
    if (popup->init()) { popup->autorelease(); return popup; }
    delete popup;
    return nullptr;
}

bool DiscordConfigPopup::init() {
    auto win = CCDirector::get()->getWinSize();
    if (!PaimonPopup::init(std::min(520.f, win.width - 28.f), std::min(360.f, win.height - 22.f))) return false;
    setID("discord-config-popup"_spr);
    setTitle("Discord Rich Presence", "goldFont.fnt", .6f);
    addInfoButton("Discord Rich Presence",
        "Changes are saved automatically. The preview uses the same activity builder as Discord. "
        "Choose which data to share in <cy>Privacy</c>; <cr>Private Mode</c> overrides all custom text and images. "
        "Text variables work in details, state and image hover text. "
        "The presence always shows the Paimbnails Page and Paimbnails Discord buttons. "
        "Updates are combined every 5 seconds. The desktop Discord app must be running.");
    setMouseEnabled(true);

    auto size = m_mainLayer->getContentSize();
    float const top = size.height - kTitleSpace;

    // left column: live preview, connection status and profile actions.
    float const leftW = std::clamp(size.width * .36f, 150.f, 186.f);
    float const previewBottom = kStatusY + kStatusH + kSectionGap;
    buildPreview({kMargin, previewBottom}, {leftW, top - previewBottom});
    buildStatus({kMargin, kStatusY}, leftW);
    buildActions(kMargin, leftW);

    // right column: master switch, tabs and the settings list.
    float const columnX = kMargin + leftW + kColumnGap;
    float const columnW = size.width - columnX - kMargin;
    auto* hero = kit::makeHeroToggle(columnW, "Share on Discord", nullptr, gset<bool>("discord-rpc-enabled"),
        [this](bool value) { sset<bool>("discord-rpc-enabled", value); changed(); });
    float const heroY = top - hero->getContentSize().height;
    hero->setPosition({columnX, heroY});
    hero->setID("discord-enable"_spr);
    m_mainLayer->addChild(hero, 5);

    float const tabsY = heroY - kSectionGap - kit::kTabBarHeight;
    auto* tabs = kit::makeTabBar(columnW, {"Activity", "Privacy", "Text", "Images"}, m_page,
        [this](int page) { showPage(page); });
    tabs->setPosition({columnX, tabsY});
    tabs->setID("discord-tabs"_spr);
    m_mainLayer->addChild(tabs, 6);

    float const scrollH = tabsY - kSectionGap - kMargin;
    m_scrollWidth = columnW - kScrollbarW - 4.f;
    m_scroll = ScrollLayer::create(CCSize{m_scrollWidth, scrollH});
    if (m_scroll) m_scroll->setAnchorPoint({0.f, 0.f});
    m_scroll->setPosition({columnX, kMargin});
    m_scroll->setID("discord-settings-scroll"_spr);
    m_mainLayer->addChild(m_scroll, 5);
    if (auto* bar = Scrollbar::create(m_scroll)) {
        bar->setContentSize({kScrollbarW, scrollH});
        bar->setPosition({columnX + columnW - kScrollbarW / 2.f, kMargin + scrollH / 2.f});
        bar->setID("discord-settings-scrollbar"_spr);
        m_mainLayer->addChild(bar, 6);
    }

    buildPage(0);
    updatePreview();
    schedule(schedule_selector(DiscordConfigPopup::updateSmoothScroll));
    schedule(schedule_selector(DiscordConfigPopup::pollPreview), .5f);
    paimon::markDynamicPopup(this);
    return true;
}

void DiscordConfigPopup::buildPreview(CCPoint origin, CCSize size) {
    auto* card = CCNode::create();
    card->setID("discord-preview"_spr);
    card->setAnchorPoint({0.f, 0.f});
    card->setContentSize(size);
    card->setPosition(origin);
    m_mainLayer->addChild(card, 5);
    m_previewCard = card;

    auto* rim = ui::makeInset({size.width + 4.f, size.height + 4.f}, 120);
    rim->setPosition({-2.f, -2.f});
    card->addChild(rim, -2);
    card->addChild(ui::makeInset(size, 255, dc::card), -1);

    auto text = [card](char const* font, float scale, ccColor3B color, CCPoint position,
                       CCPoint anchor = {0.f, .5f}) {
        auto* label = CCLabelBMFont::create("", font);
        label->setScale(scale);
        label->setColor(color);
        label->setAnchorPoint(anchor);
        label->setPosition(position);
        card->addChild(label, 2);
        return label;
    };

    float const headerY = size.height - 11.f;
    m_prevHeader = text("bigFont.fnt", .24f, dc::header, {kCardPad, headerY});
    m_prevTag = text("bigFont.fnt", .22f, dc::muted, {size.width - kCardPad, headerY}, {1.f, .5f});

    float const imageTop = size.height - 22.f;
    m_previewTextTop = imageTop;
    m_previewImage = CCNode::create();
    m_previewImage->setAnchorPoint({0.f, 0.f});
    m_previewImage->setContentSize({kImageSize, kImageSize});
    m_previewImage->setPosition({kCardPad, imageTop - kImageSize});
    card->addChild(m_previewImage, 1);

    m_previewPlaceholder = CCNode::create();
    m_previewPlaceholder->addChild(ui::makeInset({kImageSize, kImageSize}, 255, dc::image));
    auto* placeholderText = CCLabelBMFont::create("Custom\nimage", "chatFont.fnt",
        kImageSize / .42f, kCCTextAlignmentCenter);
    placeholderText->setScale(.42f);
    placeholderText->setColor(dc::muted);
    placeholderText->setPosition({kImageSize / 2.f, kImageSize / 2.f});
    m_previewPlaceholder->addChild(placeholderText, 1);
    m_previewImage->addChild(m_previewPlaceholder);
    m_previewPlaceholderText = placeholderText;

    // downloaded artwork, cropped like discord: rounded large image, round small image.
    auto makeClip = [](CCDrawNode* stencil, CCSize size) {
        auto* clip = CCClippingNode::create(stencil);
        clip->setAlphaThreshold(.05f);
        clip->setAnchorPoint({0.f, 0.f});
        clip->setContentSize(size);
        clip->setVisible(false);
        return clip;
    };
    if (auto* stencil = paimon::SpriteHelper::createRoundedRectStencil(kImageSize, kImageSize, kImageRadius)) {
        m_previewClips[0] = makeClip(stencil, {kImageSize, kImageSize});
        m_previewImage->addChild(m_previewClips[0], 1);
    }

    if (auto* logo = CCSprite::create("logo.png"_spr)) {
        logo->setScale(kImageSize / std::max(logo->getContentSize().width, 1.f));
        logo->setPosition({kImageSize / 2.f, kImageSize / 2.f});
        m_previewImage->addChild(logo, 1);
        m_previewLogo = logo;
    }

    // small image: discord's round badge on the large image corner.
    auto* badge = CCNode::create();
    badge->setPosition({kImageSize - 4.f, 4.f});
    auto* ring = PaimonDrawNode::create();
    ring->drawSolidCircle({0.f, 0.f}, kBadgeRadius, {dc::card.r / 255.f, dc::card.g / 255.f, dc::card.b / 255.f, 1.f});
    badge->addChild(ring);
    auto* fill = PaimonDrawNode::create();
    fill->drawSolidCircle({0.f, 0.f}, kBadgeImageRadius, {88.f / 255.f, 101.f / 255.f, 242.f / 255.f, 1.f});
    badge->addChild(fill, 1);
    m_previewBadgeFill = fill;
    auto* circle = PaimonDrawNode::create();
    circle->drawSolidCircle({kBadgeImageRadius, kBadgeImageRadius}, kBadgeImageRadius, {1.f, 1.f, 1.f, 1.f});
    m_previewClips[1] = makeClip(circle, {kBadgeImageRadius * 2.f, kBadgeImageRadius * 2.f});
    m_previewClips[1]->setPosition({-kBadgeImageRadius, -kBadgeImageRadius});
    badge->addChild(m_previewClips[1], 2);
    m_previewImage->addChild(badge, 2);
    m_previewBadge = badge;

    m_prevName = text("bigFont.fnt", .30f, dc::name, {0.f, 0.f});
    m_prevDetails = text("chatFont.fnt", .50f, dc::details, {0.f, 0.f});
    m_prevState = text("chatFont.fnt", .48f, dc::state, {0.f, 0.f});
    m_prevTime = text("chatFont.fnt", .45f, dc::elapsed, {0.f, 0.f});

    float const dividerY = imageTop - kImageSize - 7.f;
    auto* divider = ui::makeDivider(size.width - kCardPad * 2.f, dc::divider, 255);
    divider->setPosition({size.width / 2.f, dividerY});
    card->addChild(divider, 1);

    m_previewHoverTop = dividerY - 10.f;
    for (std::size_t i = 0; i < m_prevHover.size(); ++i) {
        m_prevHover[i] = text("chatFont.fnt", .42f, dc::muted,
            {kCardPad, m_previewHoverTop - kHoverStep * static_cast<float>(i)});
    }

    float const buttonW = size.width - kCardPad * 2.f;
    for (std::size_t i = 0; i < m_prevButtons.size(); ++i) {
        auto* button = CCNode::create();
        button->setAnchorPoint({0.f, 0.f});
        button->setContentSize({buttonW, kPreviewButtonH});
        button->addChild(ui::makeInset({buttonW, kPreviewButtonH}, 255, dc::button), -1);
        auto* label = CCLabelBMFont::create("", "chatFont.fnt");
        label->setColor(dc::name);
        label->setPosition({buttonW / 2.f, kPreviewButtonH / 2.f + .5f});
        button->addChild(label);
        card->addChild(button, 1);
        m_prevButtons[i] = button;
        m_prevButtonLabels[i] = label;
    }
    float const buttonsTop = kCardPad + kPreviewButtonH * 2.f + kPreviewButtonGap;
    m_previewHoverFits = m_previewHoverTop - kHoverStep - 5.f >= buttonsTop;
}

void DiscordConfigPopup::buildStatus(CCPoint origin, float width) {
    auto* box = CCNode::create();
    box->setID("discord-status"_spr);
    box->setAnchorPoint({0.f, 0.f});
    box->setContentSize({width, kStatusH});
    box->setPosition(origin);
    box->addChild(ui::makeInset({width, kStatusH}, 110), -1);

    m_statusDot = PaimonDrawNode::create();
    m_statusDot->setPosition({10.f, kStatusH / 2.f});
    box->addChild(m_statusDot);

    m_connection = CCLabelBMFont::create("", "chatFont.fnt", (width - 27.f) / kStatusScale, kCCTextAlignmentLeft);
    m_connection->setScale(kStatusScale);
    m_connection->setAnchorPoint({0.f, .5f});
    m_connection->setPosition({19.f, kStatusH / 2.f});
    box->addChild(m_connection);
    m_mainLayer->addChild(box, 5);
}

void DiscordConfigPopup::buildActions(float left, float width) {
    auto* menu = CCMenu::create();
    menu->setID("discord-actions"_spr);
    menu->setPosition({0.f, 0.f});
    menu->setTouchPriority(childTouchPrio());
    m_mainLayer->addChild(menu, 7);

    auto* update = ui::makeButton("Update now", [this] { onRefreshPresence(nullptr); },
        ui::Btn::Green, width, .62f, "bigFont.fnt");
    update->setPosition({left + width / 2.f, kUpdateRowY});
    menu->addChild(update);

    struct Action { char const* label; ui::Btn skin; };
    Action const actions[] = {{"Import", ui::Btn::Gray}, {"Export", ui::Btn::Gray}, {"Reset", ui::Btn::Red}};
    constexpr float kGap = 4.f;
    float const buttonW = (width - kGap * 2.f) / 3.f;
    std::vector<CCMenuItemSpriteExtra*> row;
    for (int i = 0; i < 3; ++i) {
        auto* button = ui::makeButton(actions[i].label, [this, i] {
            if (i == 0) importProfile();
            else if (i == 1) exportProfile();
            else onResetDefaults(nullptr);
        }, actions[i].skin, buttonW, .55f, "bigFont.fnt");
        button->setPosition({left + buttonW / 2.f + static_cast<float>(i) * (buttonW + kGap), kActionRowY});
        menu->addChild(button);
        row.push_back(button);
    }
    ui::matchButtonLabels(row);
}

void DiscordConfigPopup::showPage(int page) {
    WeakRef<DiscordConfigPopup> weak(this);
    // a select/toggle callback must return before its own row is destroyed.
    paimon::scheduleMainThreadDelay(.001f, [weak, page] {
        auto self = weak.lock();
        if (self && !self->m_destroyed) self->buildPage(page);
    });
}

CCNode* DiscordConfigPopup::makePresetRow(float width) {
    constexpr float kPad = 8.f;
    constexpr float kGap = 4.f;
    constexpr float kButtonsH = 26.f;
    constexpr float kTextScale = .46f;

    // size the row for the longest description so switching presets never overflows.
    auto* label = CCLabelBMFont::create("", "chatFont.fnt", (width - 20.f) / kTextScale, kCCTextAlignmentLeft);
    label->setScale(kTextScale);
    label->setAnchorPoint({0.f, 1.f});
    label->setColor(kit::kDescColor);
    float textH = 0.f;
    for (int preset = -1; preset < 4; ++preset) {
        label->setString(presetDescription(preset));
        textH = std::max(textH, label->getScaledContentSize().height);
    }
    float const height = kPad + kButtonsH + 4.f + textH + kPad;

    auto* row = CCNode::create();
    row->setAnchorPoint({0.f, 0.f});
    row->setContentSize({width, height});
    row->addChild(ui::makeInset({width, height}, kit::kRowAlpha), -1);

    auto* menu = makeRowMenu(row);
    float const buttonW = (width - kPad * 2.f - kGap * 3.f) / 4.f;
    for (int i = 0; i < 4; ++i) {
        auto* button = ui::makeButton(kPresetNames[i], [this, i] { applyPreset(kPresetNames[i]); },
            ui::Btn::Gray, buttonW, .5f, "bigFont.fnt");
        button->setPosition({kPad + buttonW / 2.f + static_cast<float>(i) * (buttonW + kGap),
            height - kPad - kButtonsH / 2.f + 2.f});
        menu->addChild(button);
        m_presetButtons.push_back(button);
    }

    label->setPosition({10.f, height - kPad - kButtonsH - 4.f});
    row->addChild(label);
    m_presetLabel = label;
    refreshPresetState();
    return row;
}

void DiscordConfigPopup::refreshPresetState() {
    if (m_presetButtons.empty()) return;
    int const match = matchPreset(readPresenceConfiguration());
    for (std::size_t i = 0; i < m_presetButtons.size(); ++i) {
        ui::setButtonSkin(m_presetButtons[i], static_cast<int>(i) == match ? ui::Btn::Green : ui::Btn::Gray);
    }
    ui::matchButtonLabels(m_presetButtons);
    if (m_presetLabel) m_presetLabel->setString(presetDescription(match));
}

void DiscordConfigPopup::buildPage(int page) {
    auto* content = m_scroll->m_contentLayer;
    float const viewH = m_scroll->getContentSize().height;
    // rebuilding the same page (preset, reset, context switch) keeps the scroll position.
    bool const keepOffset = m_pageBuilt && page == m_page;
    float const fromTop = keepOffset ? content->getPositionY() - (viewH - content->getContentSize().height) : 0.f;
    m_page = page;
    m_pageBuilt = true;
    m_largeImageKeyInput = m_smallImageKeyInput = nullptr;
    m_presetButtons.clear();
    m_presetLabel = nullptr;
    m_scrollTargetSet = false;
    content->removeAllChildren();

    auto configuration = readPresenceConfiguration();
    float const cardW = m_scrollWidth;
    float const inner = kit::cardInnerWidth(cardW);
    std::vector<CCNode*> items;
    auto card = [&](char const* title, ccColor3B accent, std::vector<CCNode*> rows) {
        items.push_back(kit::makeCard(cardW, title, accent, rows));
    };
    auto toggle = [&](char const* key, char const* title, char const* description) {
        return kit::makeToggleRow(inner, title, description, configuration.flag(key), [this, key](bool value) {
            sset<bool>(key, value); changed();
        });
    };
    auto select = [&](char const* key, char const* title, char const* description, std::vector<std::string> choices) {
        auto it = std::find(choices.begin(), choices.end(), configuration.text(key));
        int const index = it == choices.end() ? 0 : static_cast<int>(it - choices.begin());
        return kit::makeSelectRow(inner, title, description, choices, index, [this, key, choices](int value) {
            if (value < 0 || value >= static_cast<int>(choices.size())) return;
            sset<std::string>(key, choices[static_cast<std::size_t>(value)]); changed();
        });
    };
    auto input = [&](char const* key, char const* title, char const* hint, int length = 256, TextInput** target = nullptr) {
        return makeInputRow(inner, title, hint, configuration.text(key), length, [this, key](std::string const& value) {
            sset<std::string>(key, value); changed();
        }, target);
    };
    auto hint = [&](char const* text) { return kit::makeHint(inner, text); };

    if (page == 0) {
        card("Quick presets", kGold, {makePresetRow(inner)});
        card("Activity", kSky, {
            select("discord-rpc-activity-type", "Activity type", "Playing, Listening, Watching or Competing",
                {"Playing", "Listening", "Watching", "Competing"}),
            select("discord-rpc-refresh-mode", "Sampling", "Fast: 2s / Balanced: 5s / Low power: 15s",
                {"Fast", "Balanced", "Low power"})});
        card("Timer", kMint, {
            toggle("discord-rpc-show-timestamp", "Show elapsed time", "Turn off to remove the timer"),
            select("discord-rpc-timer-mode", "Timer starts at", "Changes in progress never restart an activity timer",
                {"Session", "Activity", "Level", "Attempt", "Off"})});
        card("Discord application", kBlurple, {
            input("discord-rpc-application-id", "Application ID", "Optional: your own Discord application", 20),
            hint("Leave blank to use Paimbnails. Your own app controls its name and asset keys. "
                 "Discord updates are combined every 5 seconds; clearing is immediate.")});
    } else if (page == 1) {
        card("Privacy controls", kViolet, {
            toggle("discord-rpc-private-mode", "Private Mode", "Only a generic Geometry Dash session; overrides all customization"),
            select("discord-rpc-sharing-scope", "Share while", "Other contexts clear your presence",
                {"Everywhere", "Gameplay only", "Gameplay & editor", "Menus only"}),
            toggle("discord-rpc-idle-when-unfocused", "Idle when unfocused", "Show away status when GD loses focus (Windows)"),
            toggle("discord-rpc-show-level-name", "Level names", "Include the level title in automatic text and {level}"),
            toggle("discord-rpc-show-creator", "Creator names", "Allow the level creator in automatic text and {creator}"),
            toggle("discord-rpc-show-level-id", "Level IDs", "Allow the public level ID in automatic text and {level_id}"),
            toggle("discord-rpc-share-private-levels", "Local / unlisted level identity", "Explicitly allow draft and unlisted names, creators and IDs")});
        card("Gameplay & editor details", kMint, {
            toggle("discord-rpc-show-progress", "Current progress", "Actual percentage of the current attempt (classic levels)"),
            toggle("discord-rpc-show-attempts", "Attempt count", "Show the current session attempt number"),
            toggle("discord-rpc-show-best", "Best percentage", "Use normal or practice best to match the current mode"),
            toggle("discord-rpc-show-difficulty", "Level difficulty", "Allow the automatic difficulty icon and {difficulty}"),
            toggle("discord-rpc-show-editor-stats", "Editor object count", "Include the current number of editor objects")});
        card("Other screens", kSky, {
            toggle("discord-rpc-include-paimbnails-features", "Paimbnails features", "Include tools, PaiDraw, captures, music and customization screens"),
            toggle("discord-rpc-show-profile-names", "Profile names", "Allow names of profiles you view"),
            toggle("discord-rpc-show-music", "Track title & artist", "Allow the current menu music track"),
            toggle("discord-rpc-show-room-names", "PaiDraw room names", "Allow the current room name; never words, passwords or chat"),
            toggle("discord-rpc-show-party-size", "PaiDraw player count", "Show the current room size in Discord")});
    } else if (page == 2) {
        static std::vector<std::string> const contexts{"Global", "Gameplay", "Editor", "Menus"};
        std::vector<CCNode*> rows{kit::makeSelectRow(inner, "Text context", "Blank fields use global text or automatic activity",
            contexts, m_textContext, [this](int value) {
                if (value < 0 || value >= static_cast<int>(contexts.size()) || value == m_textContext) return;
                m_textContext = value; showPage(2);
            })};
        if (m_textContext == 0) {
            rows.push_back(toggle("discord-rpc-override-details", "Custom details", "Replace the primary line when no context-specific text is set"));
            rows.push_back(input("discord-rpc-custom-details", "Details", "{level} by {creator}", 512));
            rows.push_back(toggle("discord-rpc-override-state", "Custom state", "Replace the secondary line when no context-specific text is set"));
            rows.push_back(input("discord-rpc-custom-state", "State", "{mode} | {progress}% | Attempt {attempts}", 512));
        } else {
            char const* details[] = {"", "discord-rpc-gameplay-details", "discord-rpc-editor-details", "discord-rpc-menu-details"};
            char const* states[] = {"", "discord-rpc-gameplay-state", "discord-rpc-editor-state", "discord-rpc-menu-state"};
            rows.push_back(input(details[m_textContext], "Details", "Blank = global / automatic", 512));
            rows.push_back(input(states[m_textContext], "State", "Blank = global / automatic", 512));
        }
        rows.push_back(hint("Variables only contain data allowed in Privacy. Output is limited to 128 UTF-8 bytes."));
        card("Context-aware text", kGold, rows);
        card("Available variables", kBlurple, {makeTokenGrid(inner),
            hint("Use {{ for a literal brace. Unknown variables remain visible for correction.")});
    } else if (page == 3) {
        auto image = [&](bool large) {
            char const* key = large ? "discord-rpc-large-image-key" : "discord-rpc-small-image-key";
            return makeInputRow(inner, "Image", "Asset key / HTTP(S) URL", configuration.text(key), 256,
                [this, key, large](std::string const& value) {
                    auto index = large ? 0 : 1; ++m_uploadGeneration[index]; m_uploading[index] = false;
                    sset<std::string>(key, value); changed();
                }, large ? &m_largeImageKeyInput : &m_smallImageKeyInput, [this, large] { pickImage(large); });
        };
        card("Large image", kGold, {
            toggle("discord-rpc-show-large-image", "Show large image", "Use the default logo, an application asset or a direct image URL"),
            image(true),
            input("discord-rpc-large-text", "Hover text", "Text or variables", 512)});
        card("Small image", kSky, {
            toggle("discord-rpc-show-small-image", "Show small image", "Automatic scene/difficulty icon unless you override it"),
            image(false),
            input("discord-rpc-small-text", "Hover text", "Blank = automatic", 512)});
        card("Uploads & assets", kSlate, {
            hint("The folder button uploads your selected image publicly to Catbox. If Catbox fails, 0x0.st is used. "
                 "Closing or editing the field ignores old results."),
            hint("Registered asset keys belong to the selected Discord application. URLs are validated as entered; "
                 "HTTP URLs are not silently changed."),
            hint("The preview downloads image URLs to show them. Application asset keys can't be previewed here; "
                 "check them in Discord after the update is accepted.")});
    }

    float total = 4.f;
    for (auto* item : items) total += item->getContentSize().height + kSectionGap;
    if (!items.empty()) total -= kSectionGap;
    float const contentH = std::max(total, viewH);
    content->setContentSize({m_scrollWidth, contentH});
    float y = contentH - 2.f;
    for (auto* item : items) {
        y -= item->getContentSize().height;
        item->setPosition({0.f, y});
        content->addChild(item);
        y -= kSectionGap;
    }
    float const minY = viewH - contentH;
    content->setPositionY(std::clamp(minY + fromTop, minY, 0.f));
}

void DiscordConfigPopup::changed() { kick(); updatePreview(); refreshPresetState(); }
void DiscordConfigPopup::pollPreview(float) { updatePreview(); }

void DiscordConfigPopup::updatePreview() {
    if (m_destroyed || !m_previewCard) return;
    auto& manager = DiscordPresenceManager::get();
    auto activity = manager.previewActivity();
    bool const enabled = gset<bool>("discord-rpc-enabled");
    bool const privateMode = readPresenceConfiguration().flag("discord-rpc-private-mode");
    float const cardW = m_previewCard->getContentSize().width;

    char const* type = activity.type == DiscordActivityType::Listening ? "LISTENING TO"
        : activity.type == DiscordActivityType::Watching ? "WATCHING"
        : activity.type == DiscordActivityType::Competing ? "COMPETING IN" : "PLAYING";
    std::string tag = "PREVIEW";
    ccColor3B tagColor = dc::muted;
    if (!enabled) tag = "OFF";
    else if (activity.hidden) { tag = "HIDDEN HERE"; tagColor = ui::palette::warning; }
    else if (privateMode) { tag = "PRIVATE"; tagColor = ui::palette::violet; }
    fitLabel(m_prevTag, tag, .22f, cardW * .5f);
    m_prevTag->setColor(tagColor);
    float const tagW = m_prevTag->getScaledContentSize().width;
    fitLabel(m_prevHeader, type, .24f, cardW - kCardPad * 2.f - tagW - 6.f);

    bool const hasLarge = !activity.largeImage.empty();
    bool const hasSmall = !activity.smallImage.empty();
    bool const logo = m_previewLogo && activity.largeImage == "paimbnails" && activity.applicationId == kDefaultApplicationID;
    refreshPreviewImage(0, logo ? std::string() : activity.largeImage);
    refreshPreviewImage(1, activity.smallImage);
    bool const largeReady = !logo && m_previewImageState[0] == kImageReady;
    bool const smallReady = m_previewImageState[1] == kImageReady;
    m_previewImage->setVisible(hasLarge);
    if (m_previewLogo) m_previewLogo->setVisible(logo);
    if (m_previewClips[0]) m_previewClips[0]->setVisible(largeReady);
    m_previewPlaceholder->setVisible(!logo && !largeReady);
    if (!logo && !largeReady) {
        // asset keys belong to the discord application and can't be fetched here.
        char const* placeholder = m_previewImageState[0] == kImageLoading ? "Loading..."
            : m_previewImageState[0] == kImageFailed ? "Can't\nload" : "App\nasset";
        if (m_previewPlaceholderText->getString() != std::string_view(placeholder)) {
            m_previewPlaceholderText->setString(placeholder);
        }
    }
    m_previewBadge->setVisible(hasSmall);
    m_previewClips[1]->setVisible(smallReady);
    m_previewBadgeFill->setVisible(!smallReady);

    // lines stack like discord: empty ones collapse instead of leaving gaps.
    float const textX = hasLarge ? kCardPad + kImageSize + 8.f : kCardPad;
    float const textW = cardW - textX - kCardPad;
    std::string name = activity.applicationId == kDefaultApplicationID ? "Paimbnails" : "Your Discord app";
    std::string state = activity.state;
    if (activity.partySize > 0 && activity.partyMax > 0) {
        state += fmt::format(" ({} of {})", activity.partySize, activity.partyMax);
    }
    std::string elapsed;
    if (activity.startTimestamp > 0) {
        auto seconds = std::max<int64_t>(0, std::time(nullptr) - activity.startTimestamp);
        elapsed = fmt::format("{:02}:{:02}:{:02} elapsed", seconds / 3600, seconds / 60 % 60, seconds % 60);
    }
    struct Line { CCLabelBMFont* label; std::string const& value; float scale; };
    Line const lines[] = {
        {m_prevName, name, .30f}, {m_prevDetails, activity.details, .50f},
        {m_prevState, state, .48f}, {m_prevTime, elapsed, .45f}};
    std::size_t slot = 0;
    for (auto const& line : lines) {
        bool const visible = !line.value.empty();
        line.label->setVisible(visible);
        if (!visible) continue;
        fitLabel(line.label, line.value, line.scale, textW);
        line.label->setPosition({textX, m_previewTextTop - kTextSlots[slot++]});
    }

    std::string const hover[] = {
        activity.largeText.empty() ? std::string() : "Large hover: " + activity.largeText,
        activity.smallText.empty() ? std::string() : "Small hover: " + activity.smallText};
    int hoverSlot = 0;
    for (std::size_t i = 0; i < m_prevHover.size(); ++i) {
        bool const visible = m_previewHoverFits && !hover[i].empty();
        m_prevHover[i]->setVisible(visible);
        if (!visible) continue;
        fitLabel(m_prevHover[i], hover[i], .42f, cardW - kCardPad * 2.f);
        m_prevHover[i]->setPositionY(m_previewHoverTop - kHoverStep * static_cast<float>(hoverSlot++));
    }

    std::string const* buttonLabels[] = {&activity.button1Label, &activity.button2Label};
    int buttonSlot = 0;
    for (std::size_t i = 0; i < m_prevButtons.size(); ++i) {
        bool const visible = !buttonLabels[i]->empty();
        m_prevButtons[i]->setVisible(visible);
        if (!visible) continue;
        fitLabel(m_prevButtonLabels[i], *buttonLabels[i], .48f, m_prevButtons[i]->getContentSize().width - 12.f);
        m_prevButtons[i]->setPosition({kCardPad,
            kCardPad + (kPreviewButtonH + kPreviewButtonGap) * static_cast<float>(1 - buttonSlot++)});
    }

    auto messages = manager.validationMessages();
    std::string status;
    StatusTone tone = StatusTone::Idle;
    if (m_uploading[0] || m_uploading[1]) {
        status = "Uploading selected image...";
        tone = StatusTone::Busy;
    } else if (!messages.empty()) {
        status = messages.front();
        tone = StatusTone::Warning;
    } else if (activity.hidden && enabled) {
        status = "Not sharing this screen / local preview only";
    } else {
        status = manager.connectionStatus();
        tone = classifyStatus(status);
    }
    if (status != m_statusText) {
        m_statusText = status;
        m_connection->setScale(kStatusScale);
        m_connection->setString(status.c_str());
        float const height = m_connection->getScaledContentSize().height;
        float const maxHeight = kStatusH - 3.f;
        if (height > maxHeight) m_connection->setScale(kStatusScale * maxHeight / height);
    }
    if (static_cast<int>(tone) != m_statusTone) {
        m_statusTone = static_cast<int>(tone);
        m_statusDot->clear();
        m_statusDot->drawSolidCircle({0.f, 0.f}, 3.5f, toneDot(tone));
        m_connection->setColor(toneText(tone));
    }
}

void DiscordConfigPopup::refreshPreviewImage(int slot, std::string const& reference) {
    auto const index = static_cast<std::size_t>(slot);
    auto url = previewImageUrl(reference);
    if (url == m_previewUrls[index]) return;
    m_previewUrls[index] = url;
    auto const generation = ++m_previewImageGeneration[index];
    if (auto* sprite = m_previewSprites[index]) {
        sprite->cancelLoad();
        sprite->removeFromParent();
        m_previewSprites[index] = nullptr;
    }
    m_previewImageState[index] = url.empty() ? kImageNone : kImageLoading;
    if (url.empty()) return;
    WeakRef<DiscordConfigPopup> weak(this);
    paimon::scheduleMainThreadDelay(kImageLoadDelay, [weak, slot, generation, url] {
        auto self = weak.lock();
        if (!self || self->m_destroyed || self->m_previewImageGeneration[static_cast<std::size_t>(slot)] != generation) return;
        self->loadPreviewImage(slot, generation, url);
    });
}

void DiscordConfigPopup::loadPreviewImage(int slot, uint32_t generation, std::string const& url) {
    auto const index = static_cast<std::size_t>(slot);
    WeakRef<DiscordConfigPopup> weak(this);
    // deferred so load callbacks never rebuild the preview from inside lazysprite.
    auto refreshLater = [weak] {
        geode::queueInMainThread([weak] {
            auto self = weak.lock();
            if (self && !self->m_destroyed) self->updatePreview();
        });
    };
    auto current = [weak, index, generation]() -> Ref<DiscordConfigPopup> {
        auto self = weak.lock();
        if (!self || self->m_destroyed || self->m_previewImageGeneration[index] != generation) return nullptr;
        return self;
    };
    auto request = web::WebRequest();
    request.timeout(asp::Duration::fromSecs(15));
    request.userAgent("Paimbnails/2.x (Geode)");
    WebHelper::dispatch(std::move(request), "GET", url,
        [current, refreshLater, index, generation, slot](web::WebResponse response) mutable {
            auto self = current();
            if (!self) return;
            bool const ok = response.ok();
            auto data = ok ? std::move(response).data() : ByteVector{};
            auto* clip = self->m_previewClips[index];
            if (!ok || data.empty() || data.size() > kMaxPreviewImageBytes || !clip) {
                self->m_previewImageState[index] = kImageFailed;
                refreshLater();
                return;
            }
            auto* sprite = LazySprite::create({1.f, 1.f}, false);
            if (!sprite) {
                self->m_previewImageState[index] = kImageFailed;
                refreshLater();
                return;
            }
            sprite->setVisible(false);
            clip->addChild(sprite);
            self->m_previewSprites[index] = sprite;
            float const target = slot == 0 ? kImageSize : kBadgeImageRadius * 2.f;
            sprite->setLoadCallback([current, refreshLater, index, target](Result<> result) {
                auto self = current();
                if (!self) return;
                auto* loaded = self->m_previewSprites[index];
                auto const size = loaded ? loaded->getContentSize() : CCSize{};
                if (result.isErr() || size.width <= 0.f || size.height <= 0.f) {
                    self->m_previewImageState[index] = kImageFailed;
                } else {
                    // cover the frame; the stencil crops the overflow.
                    loaded->setScale(std::max(target / size.width, target / size.height));
                    loaded->setPosition({target / 2.f, target / 2.f});
                    loaded->setVisible(true);
                    self->m_previewImageState[index] = kImageReady;
                }
                refreshLater();
            });
            sprite->loadFromData(std::move(data));
        });
}

void DiscordConfigPopup::onExit() {
    m_destroyed = true;
    ++m_uploadGeneration[0]; ++m_uploadGeneration[1];
    for (std::size_t i = 0; i < m_previewSprites.size(); ++i) {
        ++m_previewImageGeneration[i];
        if (m_previewSprites[i]) m_previewSprites[i]->cancelLoad();
    }
    unschedule(schedule_selector(DiscordConfigPopup::updateSmoothScroll));
    unschedule(schedule_selector(DiscordConfigPopup::pollPreview));
    DiscordPresenceManager::get().refreshSoon();
    Popup::onExit();
}
bool DiscordConfigPopup::scrollWheel(float x, float y) {
    return paimon::configkit::queueWheelScroll(m_scroll, x, y, m_scrollTargetY, m_scrollTargetSet);

}
void DiscordConfigPopup::updateSmoothScroll(float dt) {
    paimon::configkit::stepWheelScroll(m_scroll, m_scrollTargetY, m_scrollTargetSet, dt);
}

void DiscordConfigPopup::onRefreshPresence(CCObject*) {
    DiscordPresenceManager::get().refreshNow(true);
    updatePreview();
    PaimonNotify::show(gset<bool>("discord-rpc-enabled") ? "Presence update queued for Discord." : "Presence disabled; preview updated.", NotificationIcon::Info);
}

void DiscordConfigPopup::applyPreset(std::string const& preset) {
    auto* mod = Mod::get();
    auto const found = std::find(std::begin(kPresetNames), std::end(kPresetNames), preset);
    int const index = found == std::end(kPresetNames) ? 0 : static_cast<int>(found - std::begin(kPresetNames));
    bool const detailed = index == 1;
    bool const minimal = index >= 2;
    mod->setSavedValue("discord-rpc-private-mode", index == 3);
    for (auto key : kBroadPresetKeys) mod->setSavedValue(key, !minimal);
    for (auto key : kDetailPresetKeys) mod->setSavedValue(key, detailed);
    mod->setSavedValue("discord-rpc-share-private-levels", false);
    mod->setSavedValue("discord-rpc-refresh-mode", std::string(presetRefreshMode(index)));
    changed(); showPage(m_page);
}

void DiscordConfigPopup::pickImage(bool large) {
    auto index = large ? 0 : 1;
    auto generation = ++m_uploadGeneration[index];
    WeakRef<DiscordConfigPopup> weak(this);
    pt::pickImage([weak, large, index, generation](auto result) {
        auto self = weak.lock();
        if (!self || self->m_destroyed || self->m_uploadGeneration[index] != generation) return;
        if (result.isErr()) { PaimonNotify::show("Could not open the image picker.", NotificationIcon::Error); return; }
        auto path = std::move(result).unwrapOr(std::nullopt);
        if (!path) return;
        auto imported = paimon::assets::importToBucket(*path, "discord_rpc", paimon::assets::Kind::Image);
        if (!imported.success) { PaimonNotify::show("Image import failed: " + imported.error, NotificationIcon::Error); return; }
        auto pixels = ImageLoadHelper::readBinaryFile(imported.path, 8);
        if (pixels.empty()) { PaimonNotify::show("Image is empty or exceeds 8 MiB.", NotificationIcon::Error); return; }
        self->m_uploading[index] = true; self->updatePreview();
        auto filename = paimon::paths::toUtf8(path->filename());
        uploadToCatbox(pixels, filename, [weak, large, index, generation](bool ok, std::string value) {
            auto self = weak.lock();
            if (!self || self->m_destroyed || self->m_uploadGeneration[index] != generation) return;
            self->m_uploading[index] = false;
            if (!ok || imageReference(value).empty()) {
                PaimonNotify::show("Upload failed: " + cleanText(value, 180), NotificationIcon::Error);
                self->updatePreview(); return;
            }
            auto* key = large ? "discord-rpc-large-image-key" : "discord-rpc-small-image-key";
            sset<std::string>(key, value);
            auto* input = large ? self->m_largeImageKeyInput : self->m_smallImageKeyInput;
            if (input) input->setString(value);
            self->changed();
            PaimonNotify::show("Image uploaded and saved.", NotificationIcon::Success);
        });
    });
}

void DiscordConfigPopup::onResetDefaults(CCObject*) {
    WeakRef<DiscordConfigPopup> weak(this);
    PopupManager::get().quickPopup("Reset Discord profile", "Restore all customization defaults? The enable switch stays as it is.",
        "Cancel", "Reset", [weak](auto*, bool reset) {
            auto self = weak.lock(); if (!self || self->m_destroyed || !reset) return;
            resetPresenceConfiguration();
            ++self->m_uploadGeneration[0]; ++self->m_uploadGeneration[1]; self->m_uploading = {};
            self->changed(); self->showPage(self->m_page);
        }).showInstant();
}

void DiscordConfigPopup::exportProfile() {
    matjson::Value profile = matjson::makeObject({{"schema", "paimbnails-discord-profile"}, {"version", 1}});
    auto configuration = readPresenceConfiguration();
    auto values = matjson::Value::object();
    for (auto const& option : kBooleanOptions) values[option.key] = configuration.flag(option.key);
    for (auto const& option : kTextOptions) values[option.key] = configuration.text(option.key);
    profile["settings"] = std::move(values);
    pt::saveJson("paimbnails-discord.json", [profile = std::move(profile)](auto result) {
        if (result.isErr()) { PaimonNotify::show("Could not open the export dialog.", NotificationIcon::Error); return; }
        auto path = std::move(result).unwrapOr(std::nullopt); if (!path) return;
        bool ok = paimon::file::writeAtomically(*path, profile.dump(2));
        PaimonNotify::show(ok ? "Discord profile exported." : "Could not export profile.", ok ? NotificationIcon::Success : NotificationIcon::Error);
    });
}

void DiscordConfigPopup::importProfile() {
    WeakRef<DiscordConfigPopup> weak(this);
    pt::pickJson([weak](auto result) {
        auto self = weak.lock(); if (!self || self->m_destroyed) return;
        if (result.isErr()) { PaimonNotify::show("Could not open the import dialog.", NotificationIcon::Error); return; }
        auto path = std::move(result).unwrapOr(std::nullopt); if (!path) return;
        std::error_code error;
        auto size = std::filesystem::file_size(*path, error);
        if (error || size > 64 * 1024) { PaimonNotify::show("Invalid profile or file exceeds 64 KiB.", NotificationIcon::Error); return; }
        auto parsed = utils::file::readJson(paimon::paths::toSdkPath(*path));
        if (!parsed || !parsed.unwrap().isObject() ||
            parsed.unwrap()["schema"].asString().unwrapOr("") != "paimbnails-discord-profile" ||
            parsed.unwrap()["version"].asInt().unwrapOr(0) != 1 || !parsed.unwrap()["settings"].isObject()) {
            PaimonNotify::show("Not a supported Discord presence profile.", NotificationIcon::Error); return;
        }
        auto const& values = parsed.unwrap()["settings"];
        PresenceConfiguration configuration;
        bool valid = true;
        for (std::size_t i = 0; i < kBooleanOptions.size(); ++i) {
            auto key = kBooleanOptions[i].key;
            if (!values.contains(key)) continue;
            auto value = values[key].asBool();
            if (!value) { valid = false; break; }
            configuration.flags[i] = value.unwrap();
        }
        for (std::size_t i = 0; valid && i < kTextOptions.size(); ++i) {
            auto key = kTextOptions[i].key;
            if (!values.contains(key)) continue;
            auto value = values[key].asString();
            if (!value || value.unwrap().size() > 1024 || !validChoice(key, value.unwrap())) { valid = false; break; }
            configuration.texts[i] = value.unwrap();
        }
        if (!valid) { PaimonNotify::show("Profile contains invalid setting values.", NotificationIcon::Error); return; }
        auto* mod = Mod::get();
        for (auto const& option : kBooleanOptions) mod->setSavedValue(option.key, configuration.flag(option.key));
        for (auto const& option : kTextOptions) mod->setSavedValue(option.key, configuration.text(option.key));
        ++self->m_uploadGeneration[0]; ++self->m_uploadGeneration[1]; self->m_uploading = {};
        self->changed(); self->showPage(self->m_page);
        PaimonNotify::show("Profile imported; enable switch unchanged.", NotificationIcon::Success);
    });
}

} // namespace paimon::discord
