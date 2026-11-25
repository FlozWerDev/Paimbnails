#include "../../../utils/FilesystemPath.hpp"
#include "DeathEffectPopup.hpp"
#include "../visuals/DeathAnimation.hpp"

#include "../services/DeathEffectManager.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "../../../utils/LocalAssetStore.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../ui/PaimonUI.hpp"

#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace paimon::death_effects {

namespace {
constexpr float kPopupWidth = 524.f;
constexpr float kPopupHeight = 328.f;
constexpr float kListWidth = 336.f;
constexpr float kListHeight = 166.f;
constexpr float kRowHeight = 42.f;

constexpr std::array<char const*, 8> kQuickTags = {
    "explosion", "retro", "hit", "vine", "meme", "glass", "anime", "oof"
};

CCMenuItemToggler* makeToggle(CCObject* target, SEL_MenuHandler selector, bool checked, float scale = 0.56f) {
    auto* off = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
    auto* on  = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
    if (!off || !on) return nullptr;
    off->setScale(scale);
    on->setScale(scale);
    auto* toggle = CCMenuItemToggler::create(off, on, target, selector);
    if (toggle) toggle->toggle(checked);
    return toggle;
}

CCMenuItemSpriteExtra* makeTextButton(
    std::string const& text,
    CCObject* target,
    SEL_MenuHandler selector,
    char const* texture = "GJ_button_01.png",
    float width = 0.f,
    float scale = 0.44f
) {
    ButtonSprite* sprite = width > 0.f
        ? ButtonSprite::create(text.c_str(), static_cast<int>(width), true, "bigFont.fnt", texture, 20.f, scale)
        : ButtonSprite::create(text.c_str(), "bigFont.fnt", texture, 0.7f);
    if (!sprite) return nullptr;
    if (width <= 0.f) sprite->setScale(scale);
    return CCMenuItemSpriteExtra::create(sprite, target, selector);
}

std::string formatDuration(unsigned int durationMs) {
    if (durationMs == 0) return "?";
    float sec = static_cast<float>(durationMs) / 1000.f;
    return sec < 10.f ? fmt::format("{:.1f}s", sec) : fmt::format("{}s", static_cast<int>(std::lround(sec)));
}

char const* orderLabel(PlaybackOrder o) {
    switch (o) {
        case PlaybackOrder::ShuffleBag: return "Shuffle Bag";
        case PlaybackOrder::Random:     return "Random";
        case PlaybackOrder::Sequential: return "Sequential";
    }
    return "Shuffle Bag";
}

char const* dspLabel(DeathDspEffect e) {
    switch (e) {
        case DeathDspEffect::None:       return "None";
        case DeathDspEffect::Reverb:     return "Reverb";
        case DeathDspEffect::Echo:       return "Echo";
        case DeathDspEffect::Distortion: return "Distort";
        case DeathDspEffect::LowPass:    return "Low-Pass";
        case DeathDspEffect::HighPass:   return "High-Pass";
        case DeathDspEffect::Chorus:     return "Chorus";
        case DeathDspEffect::Flange:     return "Flange";
    }
    return "None";
}

std::string maxDurLabel(float sec) {
    if (sec <= 0.05f) return "Off";
    return fmt::format("{:.1f}s", sec);
}

void setBtnText(CCMenuItemSpriteExtra* btn, char const* txt) {
    if (!btn) return;
    if (auto* spr = typeinfo_cast<ButtonSprite*>(btn->getNormalImage())) {
        spr->setString(txt);
    }
}

geode::NineSlice* addInset(CCNode* parent, CCSize size, CCPoint pos, int z = -1, GLubyte opacity = 90) {
    auto* n = paimon::ui::makeInset(size, opacity);
    if (n && parent) {
        n->setPosition(pos);
        parent->addChild(n, z);
    }
    return n;
}

void addListChrome(CCNode* parent, geode::ScrollLayer*& scroll, char const* id, float yOffset = 34.f) {
    addInset(parent, {kListWidth + 8.f, kListHeight + 8.f}, {14.f, yOffset - 4.f}, 0);
    scroll = ScrollLayer::create({kListWidth, kListHeight});
    scroll->setAnchorPoint({0.f, 0.f});
    scroll->setPosition({18.f, yOffset});
    scroll->setID(id);
    parent->addChild(scroll, 2);

    auto* borders = ListBorders::create();
    borders->setContentSize({kListWidth + 4.f, kListHeight + 2.f});
    borders->setAnchorPoint({0.f, 0.f});
    borders->setPosition({16.f, yOffset - 1.f});
    parent->addChild(borders, 3);
}

} // namespace

DeathEffectPopup* DeathEffectPopup::create() {
    auto* ret = new DeathEffectPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool DeathEffectPopup::init() {
    if (!PaimonPopup::init(kPopupWidth, kPopupHeight)) return false;

    this->setTitle("Death SFX Studio", "goldFont.fnt", 0.70f);
    this->setID("death-effects-popup"_spr);
    this->addInfoButton("Death SFX Studio",
        "Customize gameplay death audio with <cg>FMOD DSP Effects</c>, max duration trim, "
        "<cy>High-% Clutch Sounds</c>, Shuffle Bag, and Openverse search.");

    buildHeader();

    for (int i = 0; i < 4; ++i) {
        auto* t = CCNode::create();
        t->setContentSize({kPopupWidth, kPopupHeight});
        t->setVisible(i == 0);
        m_mainLayer->addChild(t, 5);
        m_tabNodes[i] = t;
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 10);

    struct Def { char const* label; int tag; };
    Def defs[] = {
        {"Library",  0},
        {"Online",   1},
        {"Audio FX", 2},
        {"Rules",    3},
    };
    std::vector<CCMenuItem*> tabItems;
    for (auto const& d : defs) {
        auto* btn = makeTextButton(d.label, this, menu_selector(DeathEffectPopup::onTabSwitch),
            "GJ_button_04.png", 86.f, 0.38f);
        if (!btn) continue;
        btn->setTag(d.tag);
        menu->addChild(btn);
        m_tabs.push_back(btn);
        tabItems.push_back(btn);
    }
    paimon::ui::layoutButtonRow(tabItems, 20.f, kPopupWidth - 20.f, 248.f, 8.f, paimon::ui::RowAlign::Center);

    buildLibraryTab(m_tabNodes[0]);
    buildOnlineTab(m_tabNodes[1]);
    buildFxTab(m_tabNodes[2]);
    buildRulesTab(m_tabNodes[3]);

    if (!m_tabs.empty()) onTabSwitch(m_tabs.front());
    refreshControls();
    rebuildList();
    return true;
}

void DeathEffectPopup::buildHeader() {
    addInset(m_mainLayer, {kPopupWidth - 34.f, 28.f}, {17.f, 267.f}, 1);

    m_statusLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_statusLabel->setAnchorPoint({0.f, 0.5f});
    m_statusLabel->setScale(0.30f);
    m_statusLabel->setPosition({24.f, 281.f});
    m_mainLayer->addChild(m_statusLabel, 4);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    m_mainLayer->addChild(menu, 8);

    auto* testBtn = makeTextButton("Test FX", this, menu_selector(DeathEffectPopup::onTestSound),
        "GJ_button_02.png", 52.f, 0.32f);
    menu->addChild(testBtn);

    auto* stopBtn = makeTextButton("Stop", this, menu_selector(DeathEffectPopup::onStopAll),
        "GJ_button_06.png", 40.f, 0.32f);
    menu->addChild(stopBtn);

    auto* vfxBtn = makeTextButton("Death VFX ->", this, menu_selector(DeathEffectPopup::onOpenVfxStudio),
        "GJ_button_01.png", 88.f, 0.32f);
    menu->addChild(vfxBtn);

    paimon::ui::layoutButtonRow({testBtn, stopBtn, vfxBtn}, 256.f, 500.f, 281.f, 6.f, paimon::ui::RowAlign::Right);
}

void DeathEffectPopup::onTabSwitch(CCObject* sender) {
    auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    m_currentTab = btn->getTag();
    for (int i = 0; i < 4; ++i) {
        if (m_tabNodes[i]) m_tabNodes[i]->setVisible(i == m_currentTab);
    }
    for (auto* tab : m_tabs) {
        paimon::ui::setButtonSkin(tab,
            tab->getTag() == m_currentTab ? paimon::ui::Btn::Green : paimon::ui::Btn::Gray);
    }
}

void DeathEffectPopup::buildLibraryTab(CCNode* tab) {
    auto* hdr = CCLabelBMFont::create("Sound Pool (Star = Clutch)", "goldFont.fnt");
    hdr->setAnchorPoint({0.f, 0.5f});
    hdr->limitLabelWidth(182.f, 0.36f, 0.22f);
    hdr->setPosition({18.f, 218.f});
    tab->addChild(hdr);

    m_filterInput = TextInput::create(136.f, "Filter...", "bigFont.fnt");
    if (m_filterInput) {
        m_filterInput->setMaxCharCount(48);
        m_filterInput->setAlignment(Label::Alignment::Left);
        m_filterInput->setScale(0.54f);
        m_filterInput->setPosition({292.f, 218.f});
        m_filterInput->setCallback(paimon::ui::safeTextInputCallback<DeathEffectPopup>(
            this, &DeathEffectPopup::onFilterChanged));
        tab->addChild(m_filterInput, 4);
    }

    addListChrome(tab, m_scroll, "sound-library-scroll"_spr, 26.f);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    tab->addChild(menu, 4);

    auto addSide = [&](char const* text, SEL_MenuHandler cb, float y, char const* bg = "GJ_button_01.png") {
        auto* btn = makeTextButton(text, this, cb, bg, 118.f, 0.36f);
        btn->setPosition({438.f, y});
        menu->addChild(btn);
    };
    addSide("Import Audio",  menu_selector(DeathEffectPopup::onImportFile),   200.f, "GJ_button_02.png");
    addSide("Import Folder", menu_selector(DeathEffectPopup::onImportFolder), 170.f, "GJ_button_01.png");
    addSide("Select All",    menu_selector(DeathEffectPopup::onSelectAll),    140.f, "GJ_button_04.png");
    addSide("Use Original",  menu_selector(DeathEffectPopup::onUseOriginal),  110.f, "GJ_button_06.png");
    addSide("Credits",       menu_selector(DeathEffectPopup::onCredits),       80.f, "GJ_button_04.png");

    if (auto* folderSpr = CCSprite::createWithSpriteFrameName("folderIcon_001.png")) {
        folderSpr->setScale(0.55f);
        auto* openBtn = CCMenuItemSpriteExtra::create(folderSpr, this, menu_selector(DeathEffectPopup::onOpenFolder));
        openBtn->setPosition({438.f, 42.f});
        menu->addChild(openBtn);
    }
}

void DeathEffectPopup::onFilterChanged(std::string const& text) {
    m_filter = geode::utils::string::toLower(text);
    rebuildList();
}

void DeathEffectPopup::rebuildList() {
    if (!m_scroll) return;
    auto* content = m_scroll->m_contentLayer;
    content->removeAllChildrenWithCleanup(true);

    auto sounds = DeathEffectManager::get().scanLibrary();
    std::erase_if(sounds, [this](SoundInfo const& s) {
        return !m_filter.empty()
            && geode::utils::string::toLower(s.name).find(m_filter) == std::string::npos;
    });

    if (sounds.empty()) {
        content->setContentSize({kListWidth, kListHeight});
        auto* empty = CCLabelBMFont::create(
            m_filter.empty() ? "No custom sounds yet" : "No sounds match", "bigFont.fnt");
        empty->setScale(0.40f);
        empty->setOpacity(170);
        empty->setPosition({kListWidth * 0.5f, kListHeight * 0.5f + 10.f});
        content->addChild(empty);

        auto* hint = CCLabelBMFont::create("Import audio files or browse Openverse Online", "chatFont.fnt");
        hint->setScale(0.46f);
        hint->setOpacity(135);
        hint->setPosition({kListWidth * 0.5f, kListHeight * 0.5f - 10.f});
        content->addChild(hint);
        m_scroll->scrollToTop();
        refreshStatus();
        return;
    }

    float const totalH = std::max(kListHeight, kRowHeight * sounds.size());
    content->setContentSize({kListWidth, totalH});

    for (std::size_t idx = 0; idx < sounds.size(); ++idx) {
        auto const& sound = sounds[idx];
        float bottom = totalH - kRowHeight * (idx + 1);

        auto* row = CCNode::create();
        row->setContentSize({kListWidth, kRowHeight});
        row->setPosition({0.f, bottom});
        content->addChild(row);

        addInset(row, {kListWidth - 4.f, kRowHeight - 4.f}, {2.f, 2.f}, -1);

        auto* name = CCLabelBMFont::create(sound.name.c_str(), "bigFont.fnt");
        name->setAnchorPoint({0.f, 0.5f});
        name->limitLabelWidth(154.f, 0.36f, 0.18f);
        name->setPosition({10.f, 27.f});
        name->setColor(sound.playable ? ccWHITE : ccc3(255, 125, 125));
        row->addChild(name);

        auto ext = geode::utils::string::toUpper(paimon::paths::toUtf8(sound.path.extension()));
        std::string details = fmt::format("{}  -  {}", ext, formatDuration(sound.durationMs));
        if (sound.hasCredit && !sound.credit.license.empty()) {
            details += "  -  " + sound.credit.license;
        }
        auto* detailLbl = CCLabelBMFont::create(details.c_str(), "chatFont.fnt");
        detailLbl->setAnchorPoint({0.f, 0.5f});
        detailLbl->limitLabelWidth(174.f, 0.40f, 0.2f);
        detailLbl->setOpacity(160);
        detailLbl->setPosition({10.f, 11.f});
        row->addChild(detailLbl);

        auto* menu = CCMenu::create();
        menu->ignoreAnchorPointForPosition(true);
        menu->setAnchorPoint({0.f, 0.f});
        menu->setPosition({0.f, 0.f});
        menu->setContentSize(row->getContentSize());
        row->addChild(menu, 2);

        auto* pathObj = CCString::create(paimon::paths::toUtf8(sound.path));

        if (auto* sel = makeToggle(this, menu_selector(DeathEffectPopup::onToggleSound), sound.selected, 0.54f)) {
            sel->setPosition({196.f, kRowHeight * 0.5f});
            sel->setUserObject(pathObj);
            sel->setEnabled(sound.playable);
            menu->addChild(sel);
        }

        // High-% Clutch star button
        if (auto* star = CCSprite::createWithSpriteFrameName("GJ_starsIcon_001.png")) {
            star->setScale(0.46f);
            star->setColor(sound.isClutch ? ccc3(255, 220, 70) : ccc3(120, 125, 140));
            star->setOpacity(sound.isClutch ? 255 : 140);
            auto* starBtn = CCMenuItemSpriteExtra::create(star, this, menu_selector(DeathEffectPopup::onToggleClutchSound));
            starBtn->setPosition({226.f, kRowHeight * 0.5f});
            starBtn->setUserObject(pathObj);
            starBtn->setEnabled(sound.playable);
            menu->addChild(starBtn);
        }

        if (auto* playSpr = CCSprite::createWithSpriteFrameName("GJ_playMusicBtn_001.png")) {
            playSpr->setScale(0.48f);
            auto* playBtn = CCMenuItemSpriteExtra::create(playSpr, this, menu_selector(DeathEffectPopup::onPreview));
            playBtn->setPosition({256.f, kRowHeight * 0.5f});
            playBtn->setUserObject(pathObj);
            playBtn->setEnabled(sound.playable);
            menu->addChild(playBtn);
        }

        if (sound.hasCredit) {
            if (auto* webSpr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png")) {
                webSpr->setScale(0.46f);
                auto* webBtn = CCMenuItemSpriteExtra::create(webSpr, this, menu_selector(DeathEffectPopup::onOpenCredit));
                webBtn->setPosition({284.f, kRowHeight * 0.5f});
                webBtn->setUserObject(pathObj);
                menu->addChild(webBtn);
            }
        }

        if (auto* delSpr = CCSprite::createWithSpriteFrameName("GJ_deleteIcon_001.png")) {
            delSpr->setScale(0.44f);
            auto* delBtn = CCMenuItemSpriteExtra::create(delSpr, this, menu_selector(DeathEffectPopup::onDelete));
            delBtn->setPosition({312.f, kRowHeight * 0.5f});
            delBtn->setUserObject(pathObj);
            menu->addChild(delBtn);
        }
    }

    m_scroll->scrollToTop();
    refreshStatus();
}

void DeathEffectPopup::buildOnlineTab(CCNode* tab) {
    m_onlineInput = TextInput::create(300.f, "Search Openverse audio...", "bigFont.fnt");
    if (m_onlineInput) {
        m_onlineInput->setMaxCharCount(64);
        m_onlineInput->setAlignment(Label::Alignment::Left);
        m_onlineInput->setScale(0.56f);
        m_onlineInput->setPosition({104.f, 222.f});
        m_onlineInput->setCallback(paimon::ui::safeTextInputCallback<DeathEffectPopup>(
            this, &DeathEffectPopup::onOnlineSearchChanged));
        tab->addChild(m_onlineInput, 4);
    }

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    tab->addChild(menu, 4);

    auto* searchBtn = makeTextButton("Search", this, menu_selector(DeathEffectPopup::onSearchOnline),
        "GJ_button_02.png", 64.f, 0.40f);
    searchBtn->setPosition({246.f, 222.f});
    menu->addChild(searchBtn);

    // Quick preset tag buttons on the right side
    auto* tagHdr = CCLabelBMFont::create("Quick Tags", "goldFont.fnt");
    tagHdr->setScale(0.38f);
    tagHdr->setPosition({436.f, 200.f});
    tab->addChild(tagHdr);

    for (std::size_t row = 0; row < 4; ++row) {
        std::vector<CCMenuItem*> rowBtns;
        for (std::size_t col = 0; col < 2; ++col) {
            std::size_t i = row * 2 + col;
            if (i >= kQuickTags.size()) break;
            auto* tBtn = makeTextButton(kQuickTags[i], this, menu_selector(DeathEffectPopup::onQuickTag),
                "GJ_button_04.png", 54.f, 0.28f);
            tBtn->setTag(static_cast<int>(i));
            menu->addChild(tBtn);
            rowBtns.push_back(tBtn);
        }
        paimon::ui::layoutButtonRow(rowBtns, 366.f, 506.f, 174.f - static_cast<float>(row) * 28.f, 6.f, paimon::ui::RowAlign::Center);
    }

    addListChrome(tab, m_onlineScroll, "sound-online-scroll"_spr, 34.f);

    m_onlineStatus = CCLabelBMFont::create(
        "Openly licensed CC audio via Openverse (credits auto-saved).", "chatFont.fnt");
    m_onlineStatus->setScale(0.42f);
    m_onlineStatus->setOpacity(170);
    m_onlineStatus->setPosition({kPopupWidth * 0.5f, 16.f});
    tab->addChild(m_onlineStatus);
}

void DeathEffectPopup::onOnlineSearchChanged(std::string const& text) {
    m_onlineQuery = text;
}

void DeathEffectPopup::onQuickTag(CCObject* sender) {
    auto* node = typeinfo_cast<CCNode*>(sender);
    int idx = node ? node->getTag() : -1;
    if (idx < 0 || idx >= static_cast<int>(kQuickTags.size())) return;
    m_onlineQuery = kQuickTags[idx];
    if (m_onlineInput) m_onlineInput->setString(m_onlineQuery);
    onSearchOnline(nullptr);
}

void DeathEffectPopup::onSearchOnline(CCObject*) {
    if (m_onlineBusy) return;
    auto query = m_onlineQuery;
    if (m_onlineInput) query = m_onlineInput->getString();
    if (query.empty()) {
        PaimonNotify::show("Type or tap a quick tag first.", NotificationIcon::Info);
        return;
    }

    m_onlineBusy = true;
    if (m_onlineStatus) m_onlineStatus->setString("Searching Openverse...");
    WeakRef<DeathEffectPopup> self = this;
    openverse::search(query, [self](bool ok, std::vector<openverse::ResultItem> items, std::string error) {
        auto popup = self.lock();
        if (!popup) return;
        popup->m_onlineBusy = false;
        if (!ok) {
            if (popup->m_onlineStatus) popup->m_onlineStatus->setString(error.c_str());
            PaimonNotify::show(error, NotificationIcon::Error);
            return;
        }
        popup->m_onlineItems = std::move(items);
        if (popup->m_onlineStatus) {
            popup->m_onlineStatus->setString(popup->m_onlineItems.empty()
                ? "No sounds found. Try another keyword."
                : fmt::format("{} results - tap Preview or Get.", popup->m_onlineItems.size()).c_str());
        }
        popup->rebuildOnlineList();
    });
}

void DeathEffectPopup::rebuildOnlineList() {
    if (!m_onlineScroll) return;
    auto* content = m_onlineScroll->m_contentLayer;
    content->removeAllChildrenWithCleanup(true);

    if (m_onlineItems.empty()) {
        content->setContentSize({kListWidth, kListHeight});
        auto* empty = CCLabelBMFont::create("Search or pick a Quick Tag ->", "bigFont.fnt");
        empty->setScale(0.38f);
        empty->setOpacity(170);
        empty->setPosition({kListWidth * 0.5f, kListHeight * 0.5f});
        content->addChild(empty);
        m_onlineScroll->scrollToTop();
        return;
    }

    float const totalH = std::max(kListHeight, kRowHeight * m_onlineItems.size());
    content->setContentSize({kListWidth, totalH});

    for (std::size_t idx = 0; idx < m_onlineItems.size(); ++idx) {
        auto const& item = m_onlineItems[idx];
        float bottom = totalH - kRowHeight * (idx + 1);

        auto* row = CCNode::create();
        row->setContentSize({kListWidth, kRowHeight});
        row->setPosition({0.f, bottom});
        content->addChild(row);

        addInset(row, {kListWidth - 4.f, kRowHeight - 4.f}, {2.f, 2.f}, -1);

        auto* name = CCLabelBMFont::create(item.title.c_str(), "bigFont.fnt");
        name->setAnchorPoint({0.f, 0.5f});
        name->limitLabelWidth(205.f, 0.35f, 0.18f);
        name->setPosition({10.f, 27.f});
        row->addChild(name);

        auto details = fmt::format("{}  -  {}  -  by {}",
            item.licenseLabel(), formatDuration(item.durationMs),
            item.creator.empty() ? "unknown" : item.creator);
        auto* detailLbl = CCLabelBMFont::create(details.c_str(), "chatFont.fnt");
        detailLbl->setAnchorPoint({0.f, 0.5f});
        detailLbl->limitLabelWidth(235.f, 0.40f, 0.2f);
        detailLbl->setOpacity(160);
        detailLbl->setPosition({10.f, 11.f});
        row->addChild(detailLbl);

        auto* menu = CCMenu::create();
        menu->ignoreAnchorPointForPosition(true);
        menu->setAnchorPoint({0.f, 0.f});
        menu->setPosition({0.f, 0.f});
        menu->setContentSize(row->getContentSize());
        row->addChild(menu, 2);

        auto* getBtn = makeTextButton("Get", this, menu_selector(DeathEffectPopup::onDownloadOnline),
            "GJ_button_02.png", 38.f, 0.36f);
        getBtn->setTag(static_cast<int>(idx));
        getBtn->setPosition({260.f, kRowHeight * 0.5f});
        menu->addChild(getBtn);

        if (auto* playSpr = CCSprite::createWithSpriteFrameName("GJ_playMusicBtn_001.png")) {
            playSpr->setScale(0.48f);
            auto* playBtn = CCMenuItemSpriteExtra::create(playSpr, this, menu_selector(DeathEffectPopup::onPreviewOnline));
            playBtn->setTag(static_cast<int>(idx));
            playBtn->setPosition({308.f, kRowHeight * 0.5f});
            menu->addChild(playBtn);
        }
    }

    m_onlineScroll->scrollToTop();
}

void DeathEffectPopup::onDownloadOnline(CCObject* sender) {
    auto* node = typeinfo_cast<CCNode*>(sender);
    int index = node ? node->getTag() : -1;
    if (index < 0 || index >= static_cast<int>(m_onlineItems.size())) return;
    auto item = m_onlineItems[index];

    WeakRef<DeathEffectPopup> self = this;
    if (m_onlineStatus) m_onlineStatus->setString("Downloading...");
    openverse::download(item, [self, item](bool ok, std::vector<std::uint8_t> bytes, std::string error) {
        auto popup = self.lock();
        if (!ok) {
            PaimonNotify::show(error, NotificationIcon::Error);
            if (popup && popup->m_onlineStatus) popup->m_onlineStatus->setString(error.c_str());
            return;
        }
        auto imported = DeathEffectManager::get().importDownloaded(
            item.title, item.fileType, bytes, item.toCredit());
        if (!imported) {
            PaimonNotify::show(imported.unwrapErr(), NotificationIcon::Error);
            return;
        }
        DeathEffectManager::get().setSoundSelected(imported.unwrap(), true);
        PaimonNotify::show(fmt::format("Added \"{}\"", item.title), NotificationIcon::Success);
        if (popup) {
            if (popup->m_onlineStatus) popup->m_onlineStatus->setString("Added and enabled in Library!");
            popup->rebuildList();
            popup->refreshControls();
        }
    });
}

void DeathEffectPopup::onPreviewOnline(CCObject* sender) {
    auto* node = typeinfo_cast<CCNode*>(sender);
    int index = node ? node->getTag() : -1;
    if (index < 0 || index >= static_cast<int>(m_onlineItems.size())) return;
    auto item = m_onlineItems[index];

    auto cacheDir = paimon::paths::toStdPath(Mod::get()->getConfigDir()) / "death-effects-preview";
    std::error_code ec;
    std::filesystem::create_directories(cacheDir, ec);
    auto fileName = fmt::format("preview-{}.{}", std::hash<std::string>{}(item.downloadUrl), item.fileType);
    auto previewPath = cacheDir / fileName;
    if (std::filesystem::exists(previewPath, ec) && !ec) {
        if (!DeathEffectManager::get().preview(previewPath)) {
            PaimonNotify::show("Could not preview sound.", NotificationIcon::Error);
        }
        return;
    }

    openverse::download(item, [previewPath](bool ok, std::vector<std::uint8_t> bytes, std::string error) {
        if (!ok) { PaimonNotify::show(error, NotificationIcon::Error); return; }
        if (!geode::utils::file::writeBinary(paimon::paths::toSdkPath(previewPath),
                geode::ByteVector(bytes.begin(), bytes.end()))) {
            PaimonNotify::show("Could not cache preview.", NotificationIcon::Error);
            return;
        }
        if (!DeathEffectManager::get().preview(previewPath)) {
            PaimonNotify::show("Could not preview sound.", NotificationIcon::Error);
        }
    });
}

void DeathEffectPopup::onOpenSourceOnline(CCObject* sender) {
    auto* node = typeinfo_cast<CCNode*>(sender);
    int index = node ? node->getTag() : -1;
    if (index < 0 || index >= static_cast<int>(m_onlineItems.size())) return;
    web::openLinkInBrowser(m_onlineItems[index].sourceUrl);
}

void DeathEffectPopup::buildFxTab(CCNode* tab) {
    addInset(tab, {kPopupWidth - 34.f, 204.f}, {17.f, 18.f}, -1);

    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    tab->addChild(menu, 4);

    // Left column: 4 core sliders
    auto addSlider = [&](char const* name, float x, float y, Slider*& sl, CCLabelBMFont*& vl, SEL_MenuHandler cb) {
        auto* lbl = CCLabelBMFont::create(name, "bigFont.fnt");
        lbl->setAnchorPoint({0.f, 0.5f}); lbl->setScale(0.34f); lbl->setPosition({x, y + 12.f});
        tab->addChild(lbl);

        sl = Slider::create(this, cb, 0.56f);
        sl->setPosition({x + 74.f, y - 6.f});
        tab->addChild(sl, 4);

        vl = CCLabelBMFont::create("", "bigFont.fnt");
        vl->setAnchorPoint({0.f, 0.5f}); vl->setScale(0.32f); vl->setPosition({x + 160.f, y + 12.f});
        tab->addChild(vl);
    };
    addSlider("Volume",    30.f, 194.f, m_volumeSlider, m_volumeLabel, menu_selector(DeathEffectPopup::onVolumeChanged));
    addSlider("Speed",     30.f, 148.f, m_speedSlider,  m_speedLabel,  menu_selector(DeathEffectPopup::onSpeedChanged));
    addSlider("Pitch +/-", 30.f, 102.f, m_pitchSlider,  m_pitchLabel,  menu_selector(DeathEffectPopup::onPitchChanged));
    addSlider("Stereo Pan",30.f,  56.f, m_panSlider,    m_panLabel,    menu_selector(DeathEffectPopup::onPanChanged));

    // Right column: DSP Effect + DSP Intensity + Max Clip Duration
    auto* fxHdr = CCLabelBMFont::create("FMOD Real-Time DSP", "goldFont.fnt");
    fxHdr->setScale(0.40f);
    fxHdr->setPosition({384.f, 202.f});
    tab->addChild(fxHdr);

    auto* dspLbl = CCLabelBMFont::create("DSP Filter", "bigFont.fnt");
    dspLbl->setAnchorPoint({0.f, 0.5f}); dspLbl->setScale(0.33f); dspLbl->setPosition({276.f, 174.f});
    tab->addChild(dspLbl);

    m_dspBtn = makeTextButton("None", this, menu_selector(DeathEffectPopup::onCycleDsp),
        "GJ_button_01.png", 104.f, 0.36f);
    m_dspBtn->setPosition({436.f, 174.f});
    menu->addChild(m_dspBtn);

    addSlider("DSP Mix", 276.f, 134.f, m_dspMixSlider, m_dspMixLabel, menu_selector(DeathEffectPopup::onDspMixChanged));

    auto* durLbl = CCLabelBMFont::create("Max Length Trim", "bigFont.fnt");
    durLbl->setAnchorPoint({0.f, 0.5f}); durLbl->setScale(0.33f); durLbl->setPosition({276.f, 92.f});
    tab->addChild(durLbl);

    m_maxDurBtn = makeTextButton("Off", this, menu_selector(DeathEffectPopup::onCycleMaxDur),
        "GJ_button_04.png", 86.f, 0.36f);
    m_maxDurBtn->setPosition({445.f, 92.f});
    menu->addChild(m_maxDurBtn);

    auto* testBtn = makeTextButton("Test Sound With FX", this, menu_selector(DeathEffectPopup::onTestSound),
        "GJ_button_02.png", 174.f, 0.40f);
    testBtn->setPosition({384.f, 44.f});
    menu->addChild(testBtn);
}

void DeathEffectPopup::buildRulesTab(CCNode* tab) {
    addInset(tab, {kPopupWidth - 34.f, 204.f}, {17.f, 18.f}, -1);

    auto& mgr = DeathEffectManager::get();
    auto* menu = CCMenu::create();
    menu->setPosition({0.f, 0.f});
    tab->addChild(menu, 4);

    // Left side: Playback mode & general rules
    auto* ordLbl = CCLabelBMFont::create("Pool Order", "bigFont.fnt");
    ordLbl->setAnchorPoint({0.f, 0.5f}); ordLbl->setScale(0.34f); ordLbl->setPosition({30.f, 198.f});
    tab->addChild(ordLbl);

    m_orderButton = makeTextButton("Shuffle Bag", this, menu_selector(DeathEffectPopup::onCycleOrder),
        "GJ_button_01.png", 104.f, 0.36f);
    m_orderButton->setPosition({184.f, 198.f});
    menu->addChild(m_orderButton);

    auto addTog = [&](char const* label, float x, float y, bool val, CCMenuItemToggler*& out, SEL_MenuHandler cb) {
        auto* l = CCLabelBMFont::create(label, "bigFont.fnt");
        l->setAnchorPoint({0.f, 0.5f}); l->setScale(0.32f); l->setPosition({x, y});
        tab->addChild(l);
        out = makeToggle(this, cb, val, 0.54f);
        out->setPosition({x + 172.f, y});
        menu->addChild(out);
    };
    addTog("Avoid Repeats",         30.f, 162.f, mgr.avoidRepeats(),     m_avoidRepeatToggle,  menu_selector(DeathEffectPopup::onToggleAvoidRepeats));
    addTog("Stop on Respawn",       30.f, 130.f, mgr.stopOnReset(),      m_stopOnResetToggle,  menu_selector(DeathEffectPopup::onToggleStopOnReset));
    addTog("Overlap Rapid Deaths",  30.f,  98.f, mgr.allowOverlap(),     m_allowOverlapToggle, menu_selector(DeathEffectPopup::onToggleAllowOverlap));
    addTog("Layer w/ Vanilla SFX",  30.f,  66.f, mgr.layerWithVanilla(), m_layerVanillaToggle, menu_selector(DeathEffectPopup::onToggleLayerVanilla));

    // Right side: High-% Clutch Special Sound
    auto* clHdr = CCLabelBMFont::create("High-% / Clutch Special SFX", "goldFont.fnt");
    clHdr->setScale(0.38f);
    clHdr->setPosition({386.f, 198.f});
    tab->addChild(clHdr);

    addTog("Enable Clutch SFX", 278.f, 162.f, mgr.clutchEnabled(),     m_clutchEnabledToggle, menu_selector(DeathEffectPopup::onToggleClutchEnabled));
    addTog("Only on New Best",  278.f, 130.f, mgr.clutchOnlyNewBest(), m_clutchNewBestToggle, menu_selector(DeathEffectPopup::onToggleClutchNewBest));

    auto* thrLbl = CCLabelBMFont::create("Min %", "bigFont.fnt");
    thrLbl->setAnchorPoint({0.f, 0.5f}); thrLbl->setScale(0.32f); thrLbl->setPosition({278.f, 98.f});
    tab->addChild(thrLbl);

    m_clutchThreshSlider = Slider::create(this, menu_selector(DeathEffectPopup::onClutchThresholdChanged), 0.48f);
    m_clutchThreshSlider->setPosition({382.f, 98.f});
    tab->addChild(m_clutchThreshSlider, 4);

    m_clutchThreshLabel = CCLabelBMFont::create("80%", "bigFont.fnt");
    m_clutchThreshLabel->setAnchorPoint({0.f, 0.5f}); m_clutchThreshLabel->setScale(0.32f); m_clutchThreshLabel->setPosition({456.f, 98.f});
    tab->addChild(m_clutchThreshLabel);

    m_clutchSoundNameLabel = CCLabelBMFont::create("", "chatFont.fnt");
    m_clutchSoundNameLabel->setScale(0.44f);
    m_clutchSoundNameLabel->setPosition({386.f, 54.f});
    m_clutchSoundNameLabel->setColor(ccc3(255, 225, 110));
    tab->addChild(m_clutchSoundNameLabel);
}

void DeathEffectPopup::refreshStatus() {
    if (!m_statusLabel) return;
    auto& mgr = DeathEffectManager::get();
    auto sel = mgr.selectedCount();
    auto tot = mgr.scanLibrary().size();
    if (sel == 0 && mgr.clutchSoundName() == "(none)") {
        m_statusLabel->setString("Vanilla SFX active (select sounds below)");
        m_statusLabel->setColor(ccc3(255, 195, 110));
    } else {
        m_statusLabel->setString(fmt::format(
            "{} active / {} total  |  DSP: {}", sel, tot, dspLabel(mgr.dspEffect())).c_str());
        m_statusLabel->setColor(ccc3(170, 245, 210));
    }
    m_statusLabel->limitLabelWidth(218.f, 0.30f, 0.16f);
}

void DeathEffectPopup::refreshControls() {
    auto& mgr = DeathEffectManager::get();

    setBtnText(m_orderButton, orderLabel(mgr.order()));
    setBtnText(m_dspBtn, dspLabel(mgr.dspEffect()));
    setBtnText(m_maxDurBtn, maxDurLabel(mgr.maxDurationSec()).c_str());

    if (m_avoidRepeatToggle)  m_avoidRepeatToggle->toggle(mgr.avoidRepeats());
    if (m_stopOnResetToggle)  m_stopOnResetToggle->toggle(mgr.stopOnReset());
    if (m_allowOverlapToggle) m_allowOverlapToggle->toggle(mgr.allowOverlap());
    if (m_layerVanillaToggle) m_layerVanillaToggle->toggle(mgr.layerWithVanilla());
    if (m_clutchEnabledToggle) m_clutchEnabledToggle->toggle(mgr.clutchEnabled());
    if (m_clutchNewBestToggle) m_clutchNewBestToggle->toggle(mgr.clutchOnlyNewBest());

    if (m_volumeSlider && m_volumeLabel) {
        float v = std::clamp(mgr.volume(), 0.f, 2.f);
        m_volumeSlider->setValue(v / 2.f);
        m_volumeSlider->updateBar();
        m_volumeLabel->setString(fmt::format("{}%", std::lround(v * 100.f)).c_str());
    }
    if (m_speedSlider && m_speedLabel) {
        float s = std::clamp(mgr.speed(), 0.25f, 2.f);
        m_speedSlider->setValue((s - 0.25f) / 1.75f);
        m_speedSlider->updateBar();
        m_speedLabel->setString(fmt::format("{:.2f}x", s).c_str());
    }
    if (m_pitchSlider && m_pitchLabel) {
        float p = std::clamp(mgr.pitchVariation(), 0.f, 0.5f);
        m_pitchSlider->setValue(p * 2.f);
        m_pitchSlider->updateBar();
        m_pitchLabel->setString(fmt::format("+/-{}%", std::lround(p * 100.f)).c_str());
    }
    if (m_panSlider && m_panLabel) {
        float pan = std::clamp(mgr.pan(), -1.f, 1.f);
        m_panSlider->setValue((pan + 1.f) * 0.5f);
        m_panSlider->updateBar();
        m_panLabel->setString(pan == 0.f ? "Center"
            : fmt::format("{}{}%", pan < 0.f ? "L " : "R ", std::lround(std::abs(pan) * 100.f)).c_str());
    }
    if (m_dspMixSlider && m_dspMixLabel) {
        float mix = std::clamp(mgr.dspIntensity(), 0.f, 1.f);
        m_dspMixSlider->setValue(mix);
        m_dspMixSlider->updateBar();
        m_dspMixLabel->setString(fmt::format("{}%", std::lround(mix * 100.f)).c_str());
    }
    if (m_clutchThreshSlider && m_clutchThreshLabel) {
        float th = std::clamp(mgr.clutchThreshold(), 50.f, 99.f);
        m_clutchThreshSlider->setValue((th - 50.f) / 49.f);
        m_clutchThreshSlider->updateBar();
        m_clutchThreshLabel->setString(fmt::format("{:.0f}%", th).c_str());
    }
    if (m_clutchSoundNameLabel) {
        auto cn = mgr.clutchSoundName();
        m_clutchSoundNameLabel->setString(cn == "(none)"
            ? "Clutch SFX: (tap Star in Library)"
            : fmt::format("Clutch SFX: {}", cn).c_str());
    }

    refreshStatus();
}

std::filesystem::path DeathEffectPopup::pathFromSender(CCObject* sender) {
    auto* item = typeinfo_cast<CCMenuItem*>(sender);
    if (!item) return {};
    auto* value = typeinfo_cast<CCString*>(item->getUserObject());
    if (!value) return {};
    return paimon::assets::pathFromUtf8(std::string(value->getCString()));
}

void DeathEffectPopup::onToggleSound(CCObject* sender) {
    auto path = pathFromSender(sender);
    if (path.empty()) return;
    auto* toggle = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (toggle) DeathEffectManager::get().setSoundSelected(path, !toggle->isToggled());
    refreshControls();
}

void DeathEffectPopup::onToggleClutchSound(CCObject* sender) {
    auto path = pathFromSender(sender);
    if (path.empty()) return;
    auto& mgr = DeathEffectManager::get();
    if (mgr.isClutchSound(path)) mgr.setClutchSound({});
    else mgr.setClutchSound(path);
    rebuildList();
    refreshControls();
}

void DeathEffectPopup::onPreview(CCObject* sender) {
    auto path = pathFromSender(sender);
    if (!path.empty() && !DeathEffectManager::get().preview(path)) {
        PaimonNotify::show("Could not preview this sound.", NotificationIcon::Error);
    }
}

void DeathEffectPopup::onStopAll(CCObject*) {
    DeathEffectManager::get().stopPreview();
    DeathEffectManager::get().stopActiveDeath();
}

void DeathEffectPopup::onDelete(CCObject* sender) {
    auto path = pathFromSender(sender);
    if (path.empty()) return;
    WeakRef<DeathEffectPopup> self = this;
    auto name = paimon::paths::toUtf8(path.stem());
    geode::createQuickPopup(
        "Delete Sound",
        fmt::format("Delete <cy>{}</c> from disk?", name),
        "Cancel", "Delete",
        [self, path](FLAlertLayer*, bool confirmed) {
            if (!confirmed) return;
            if (!DeathEffectManager::get().removeSound(path)) {
                PaimonNotify::show("Could not delete file.", NotificationIcon::Error);
            } else {
                PaimonNotify::show("Deleted.", NotificationIcon::Success);
            }
            if (auto p = self.lock()) { p->rebuildList(); p->refreshControls(); }
        }
    );
}

void DeathEffectPopup::onOpenCredit(CCObject* sender) {
    auto path = pathFromSender(sender);
    auto credit = DeathEffectManager::get().creditFor(path);
    if (!credit) return;
    geode::createQuickPopup(
        "Sound Credit",
        fmt::format("<cy>{}</c>\nby <cg>{}</c>\nLicense: <cl>{}</c>{}",
            credit->title.empty() ? credit->license : credit->title,
            credit->creator.empty() ? "unknown author" : credit->creator,
            credit->license,
            credit->attribution.empty() ? "" : "\n\n" + credit->attribution),
        "Close", "Source Page",
        [url = credit->sourceUrl](FLAlertLayer*, bool open) {
            if (open) web::openLinkInBrowser(url);
        }
    );
}

void DeathEffectPopup::onImportFile(CCObject*) {
    WeakRef<DeathEffectPopup> weakSelf = this;
    pt::pickAudio([weakSelf](geode::Result<std::optional<std::filesystem::path>> result) {
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;
        auto imported = DeathEffectManager::get().importSound(*pathOpt);
        if (!imported) {
            PaimonNotify::show(imported.unwrapErr(), NotificationIcon::Error);
            return;
        }
        DeathEffectManager::get().setSoundSelected(imported.unwrap(), true);
        PaimonNotify::show("Sound added and selected.", NotificationIcon::Success);
        if (auto self = weakSelf.lock()) {
            self->rebuildList();
            self->refreshControls();
        }
    });
}

void DeathEffectPopup::onImportFolder(CCObject*) {
    WeakRef<DeathEffectPopup> weakSelf = this;
    pt::pickFolder(DeathEffectManager::get().libraryDir(),
        [weakSelf](geode::Result<std::optional<std::filesystem::path>> result) {
            auto pathOpt = std::move(result).unwrapOr(std::nullopt);
            if (!pathOpt || pathOpt->empty()) return;
            PaimonNotify::show("Importing folder...", NotificationIcon::Loading);
            DeathEffectManager::get().importFolderAsync(*pathOpt,
                [weakSelf](FolderImportResult imported) {
                    PaimonNotify::show(fmt::format(
                        "{} imported ({} skipped).", imported.imported.size(), imported.skipped),
                        imported.skipped == 0 ? NotificationIcon::Success : NotificationIcon::Info);
                    if (auto self = weakSelf.lock()) {
                        self->rebuildList();
                        self->refreshControls();
                    }
                });
        });
}

void DeathEffectPopup::onOpenFolder(CCObject*) {
    auto dir = DeathEffectManager::get().libraryDir();
#if defined(GEODE_IS_MOBILE)
    geode::utils::clipboard::write(paimon::paths::toUtf8(dir));
    PaimonNotify::show(fmt::format("Efectos de muerte (ruta copiada): {}", paimon::paths::toUtf8(dir)),
        NotificationIcon::Info, 3.0f);
#else
    geode::utils::file::openFolder(paimon::paths::toSdkPath(dir));
#endif
}

void DeathEffectPopup::onSelectAll(CCObject*) {
    std::vector<std::filesystem::path> paths;
    for (auto const& s : DeathEffectManager::get().scanLibrary()) {
        if (s.playable) paths.push_back(s.path);
    }
    DeathEffectManager::get().addSelected(paths);
    rebuildList();
    refreshControls();
    PaimonNotify::show("All playable sounds selected.", NotificationIcon::Success);
}

void DeathEffectPopup::onUseOriginal(CCObject*) {
    DeathEffectManager::get().useOriginal();
    rebuildList();
    refreshControls();
    PaimonNotify::show("Original vanilla SFX active.", NotificationIcon::Info);
}

void DeathEffectPopup::onCredits(CCObject*) {
    auto credits = DeathEffectManager::get().allCredits();
    if (credits.empty()) {
        FLAlertLayer::create("Sound Credits",
            "No downloaded Openverse sounds yet.\nDownloaded sounds save their author & CC license here.",
            "OK")->show();
        return;
    }
    std::string text = "Openverse Attribution:\n";
    for (auto const& [file, credit] : credits) {
        text += fmt::format("\n<cy>{}</c>\n{}\nLicense: {}",
            file,
            credit.attribution.empty()
                ? fmt::format("\"{}\" by {}", credit.title, credit.creator)
                : credit.attribution,
            credit.license);
    }
    FLAlertLayer::create("Sound Credits", text, "OK")->show();
}

void DeathEffectPopup::onTestSound(CCObject*) {
    if (!DeathEffectManager::get().previewFirstSelected()) {
        PaimonNotify::show("Select or star a sound first.", NotificationIcon::Info);
    }
}

void DeathEffectPopup::onOpenVfxStudio(CCObject*) {
    this->onClose(nullptr);
    if (auto* vfx = DeathAnimationPopup::create()) {
        vfx->show();
    }
}

void DeathEffectPopup::onCycleDsp(CCObject*) {
    auto& mgr = DeathEffectManager::get();
    mgr.setDspEffect(static_cast<DeathDspEffect>((static_cast<int>(mgr.dspEffect()) + 1) % 8));
    refreshControls();
}

void DeathEffectPopup::onDspMixChanged(CCObject* sender) {
    auto* sl = typeinfo_cast<Slider*>(sender);
    if (!sl) return;
    DeathEffectManager::get().setDspIntensity(sl->getThumb()->getValue());
    refreshControls();
}

void DeathEffectPopup::onCycleMaxDur(CCObject*) {
    static constexpr float kPresets[] = {0.f, 0.8f, 1.5f, 2.5f, 4.0f};
    auto& mgr = DeathEffectManager::get();
    float cur = mgr.maxDurationSec();
    int nextIdx = 1;
    for (int i = 0; i < 5; ++i) {
        if (std::abs(cur - kPresets[i]) < 0.15f) {
            nextIdx = (i + 1) % 5;
            break;
        }
    }
    mgr.setMaxDurationSec(kPresets[nextIdx]);
    refreshControls();
}

void DeathEffectPopup::onVolumeChanged(CCObject* sender) {
    auto* sl = typeinfo_cast<Slider*>(sender);
    if (!sl) return;
    DeathEffectManager::get().setVolume(sl->getThumb()->getValue() * 2.f);
    refreshControls();
}

void DeathEffectPopup::onSpeedChanged(CCObject* sender) {
    auto* sl = typeinfo_cast<Slider*>(sender);
    if (!sl) return;
    DeathEffectManager::get().setSpeed(0.25f + sl->getThumb()->getValue() * 1.75f);
    refreshControls();
}

void DeathEffectPopup::onPitchChanged(CCObject* sender) {
    auto* sl = typeinfo_cast<Slider*>(sender);
    if (!sl) return;
    DeathEffectManager::get().setPitchVariation(sl->getThumb()->getValue() * 0.5f);
    refreshControls();
}

void DeathEffectPopup::onPanChanged(CCObject* sender) {
    auto* sl = typeinfo_cast<Slider*>(sender);
    if (!sl) return;
    DeathEffectManager::get().setPan(sl->getThumb()->getValue() * 2.f - 1.f);
    refreshControls();
}

void DeathEffectPopup::onCycleOrder(CCObject*) {
    auto& mgr = DeathEffectManager::get();
    PlaybackOrder next = PlaybackOrder::ShuffleBag;
    if (mgr.order() == PlaybackOrder::ShuffleBag) next = PlaybackOrder::Random;
    else if (mgr.order() == PlaybackOrder::Random) next = PlaybackOrder::Sequential;
    mgr.setOrder(next);
    refreshControls();
}

void DeathEffectPopup::onToggleAvoidRepeats(CCObject* sender) {
    auto* t = typeinfo_cast<CCMenuItemToggler*>(sender);
    DeathEffectManager::get().setAvoidRepeats(t && !t->isToggled());
    refreshControls();
}

void DeathEffectPopup::onToggleStopOnReset(CCObject* sender) {
    auto* t = typeinfo_cast<CCMenuItemToggler*>(sender);
    DeathEffectManager::get().setStopOnReset(t && !t->isToggled());
    refreshControls();
}

void DeathEffectPopup::onToggleAllowOverlap(CCObject* sender) {
    auto* t = typeinfo_cast<CCMenuItemToggler*>(sender);
    DeathEffectManager::get().setAllowOverlap(t && !t->isToggled());
    refreshControls();
}

void DeathEffectPopup::onToggleLayerVanilla(CCObject* sender) {
    auto* t = typeinfo_cast<CCMenuItemToggler*>(sender);
    DeathEffectManager::get().setLayerWithVanilla(t && !t->isToggled());
    refreshControls();
}

void DeathEffectPopup::onToggleClutchEnabled(CCObject* sender) {
    auto* t = typeinfo_cast<CCMenuItemToggler*>(sender);
    DeathEffectManager::get().setClutchEnabled(t && !t->isToggled());
    refreshControls();
}

void DeathEffectPopup::onToggleClutchNewBest(CCObject* sender) {
    auto* t = typeinfo_cast<CCMenuItemToggler*>(sender);
    DeathEffectManager::get().setClutchOnlyNewBest(t && !t->isToggled());
    refreshControls();
}

void DeathEffectPopup::onClutchThresholdChanged(CCObject* sender) {
    auto* sl = typeinfo_cast<Slider*>(sender);
    if (!sl) return;
    DeathEffectManager::get().setClutchThreshold(50.f + sl->getThumb()->getValue() * 49.f);
    refreshControls();
}

void DeathEffectPopup::onClose(CCObject* sender) {
    DeathEffectManager::get().stopPreview();
    PaimonPopup::onClose(sender);
}

} // namespace paimon::death_effects
