// GD-style skin for the Paimon Hub.
//
// Alternative, guided presentation of the same hub data: GD assets
// (GJ_square01 panel, GJ buttons, gradient background, side art), a compact
// two-step drill-down (categories -> actions), a contextual hint bar, a
// highlighted recommended action per category and an interactive first-run
// tour. The original skin lives untouched in PaimonHubLayer.cpp; the user
// switches between both with the "UI" button (saved value "hub-ui-style").

#include "PaimonHubLayer.hpp"
#include "PaimonHubData.hpp"
#include "../features/quick-hub/ui/RadialConfigPopup.hpp"
#include "../features/guide/services/PaimonGuideService.hpp"
#include "../features/guide/GuideEvents.hpp"
#include "../features/updates/services/UpdateChecker.hpp"
#include "../ui/FeatureInfoPopup.hpp"
#include "../ui/FeatureConfigPopup.hpp"
#include "../utils/SpriteHelper.hpp"
#include "../utils/PaimonNotification.hpp"
#include "../utils/Localization.hpp"
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <algorithm>
#include <array>

using namespace geode::prelude;
using namespace paimon::hubdata;

namespace {

std::string tr(char const* key, char const* fallback = "") {
    auto value = Localization::get().getString(key);
    if (value == key && fallback && fallback[0] != '\0') {
        return fallback;
    }
    return value;
}

// Layout constants shared by every GD-home state (absolute layer coords,
// same footprint as the News/Forum panels so tab switching feels seamless).
constexpr float kPanelL = 15.f;
constexpr float kPanelB = 15.f;
constexpr float kPanelH = 250.f;
constexpr float kTileW  = 118.f;
constexpr float kTileH  = 44.f;
constexpr float kColGap = 128.f;
constexpr float kHintBarY = 22.f;
constexpr float kHintBarH = 26.f;

// GD button texture per category (same palette family as the actions).
constexpr std::array<char const*, 7> kCatButtonFiles = {
    "GJ_button_01.png", "GJ_button_02.png", "GJ_button_03.png",
    "GJ_button_06.png", "GJ_button_05.png", "GJ_button_04.png",
    "GJ_button_02.png",
};

void shrinkLabelToFit(CCLabelBMFont* label, float maxW) {
    float w = label->getScaledContentSize().width;
    if (w > maxW && w > 0.f) {
        label->setScale(label->getScale() * (maxW / w));
    }
}

// GD-style tile: GJ button 9-slice + title + one-line hint.
CCScale9Sprite* makeGDTile(
    std::string const& bgFile,
    std::string const& title,
    std::string const& desc,
    float w = kTileW, float h = kTileH
) {
    auto bg = paimon::SpriteHelper::safeCreateScale9(bgFile.c_str());
    if (!bg) bg = CCScale9Sprite::create("GJ_button_04.png");
    if (!bg) return nullptr;
    bg->setContentSize({w, h});

    auto titleLbl = CCLabelBMFont::create(title.c_str(), "bigFont.fnt");
    titleLbl->setScale(0.32f);
    shrinkLabelToFit(titleLbl, w - 14.f);
    titleLbl->setPosition({w / 2.f, desc.empty() ? h / 2.f : h - 15.f});
    bg->addChild(titleLbl, 1);

    if (!desc.empty()) {
        auto descLbl = CCLabelBMFont::create(desc.c_str(), "bigFont.fnt");
        descLbl->setScale(0.155f);
        descLbl->setColor({235, 240, 255});
        descLbl->setOpacity(210);
        shrinkLabelToFit(descLbl, w - 14.f);
        descLbl->setPosition({w / 2.f, 11.f});
        bg->addChild(descLbl, 1);
    }
    return bg;
}

// Column x for a grid row, centering incomplete rows.
float gridColX(float cx, int col, int countInRow) {
    float offset = (3 - countInRow) * 0.5f;
    return cx + (static_cast<float>(col) - 1.f + offset) * kColGap;
}

void animateTileEntrance(CCNode* btn, size_t index, CCPoint target) {
    btn->setPosition({target.x, target.y - 14.f});
    btn->setScale(0.f);
    btn->runAction(CCSequence::create(
        CCDelayTime::create(0.03f * static_cast<float>(index)),
        CCSpawn::create(
            CCEaseBackOut::create(CCMoveTo::create(0.22f, target)),
            CCScaleTo::create(0.22f, 1.f),
            nullptr
        ),
        nullptr
    ));
}

} // namespace

void PaimonHubLayer::buildGDShell() {
    auto winSize = CCDirector::get()->getWinSize();
    float cx = winSize.width / 2.f;
    float top = winSize.height;

    // GD gradient background + bottom side art
    if (auto bg = paimon::SpriteHelper::safeCreate("GJ_gradientBG.png")) {
        bg->setAnchorPoint({0.f, 0.f});
        bg->setScaleX(winSize.width / bg->getContentSize().width);
        bg->setScaleY(winSize.height / bg->getContentSize().height);
        bg->setColor({40, 70, 160});
        this->addChild(bg, -2);
    } else {
        auto flat = CCLayerColor::create(ccc4(30, 50, 120, 255));
        flat->setContentSize(winSize);
        this->addChild(flat, -2);
    }

    if (auto leftArt = paimon::SpriteHelper::safeCreateWithFrameName("GJ_sideArt_001.png")) {
        leftArt->setAnchorPoint({0.f, 0.f});
        leftArt->setPosition({0.f, 0.f});
        this->addChild(leftArt, -1);
    }
    if (auto rightArt = paimon::SpriteHelper::safeCreateWithFrameName("GJ_sideArt_001.png")) {
        rightArt->setAnchorPoint({1.f, 0.f});
        rightArt->setFlipX(true);
        rightArt->setPosition({winSize.width, 0.f});
        this->addChild(rightArt, -1);
    }

    m_mainMenu = CCMenu::create();
    m_mainMenu->setID("paimon-hub-main-menu"_spr);
    m_mainMenu->setPosition({0, 0});
    this->addChild(m_mainMenu, 10);

    auto backSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
    auto backBtn = CCMenuItemSpriteExtra::create(backSpr, this, menu_selector(PaimonHubLayer::onBack));
    backBtn->setPosition({20.f, top - 20.f});
    backBtn->setScale(0.75f);
    m_mainMenu->addChild(backBtn);

    auto title = CCLabelBMFont::create(tr("pai.hub.title", "Paimbnails").c_str(), "goldFont.fnt");
    title->setAnchorPoint({0.f, 0.5f});
    title->setPosition({46.f, top - 20.f});
    title->setScale(0.48f);
    this->addChild(title);

    // tab bar (same footprint as the original skin so muscle memory carries)
    std::vector<std::string> tabNames = {
        tr("pai.hub.tab.home", "Inicio"),
        tr("pai.hub.tab.news", "News"),
        tr("pai.hub.tab.forum", "Forum")
    };
    auto tabBar = CCMenu::create();
    tabBar->setID("paimon-hub-tab-bar"_spr);
    tabBar->setPosition({cx + 40.f, top - 20.f});
    tabBar->setContentSize({200.f, 24.f});
    tabBar->setAnchorPoint({0.5f, 0.5f});
    tabBar->setLayout(
        RowLayout::create()
            ->setGap(6.f)
            ->setAutoScale(false)
            ->setAxisAlignment(AxisAlignment::Center)
    );
    this->addChild(tabBar, 10);

    m_tabBtns.clear();
    for (int i = 0; i < 3; i++) {
        auto spr = ButtonSprite::create(tabNames[i].c_str(), "bigFont.fnt", "GJ_button_04.png", .8f);
        spr->setScale(0.38f);
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(PaimonHubLayer::onTabSwitch));
        btn->setTag(i);
        if (i == 0)      btn->setID("home-tab-btn"_spr);
        else if (i == 1) btn->setID("news-tab-btn"_spr);
        else             btn->setID("forum-tab-btn"_spr);
        tabBar->addChild(btn);
        m_tabBtns.push_back(btn);
    }
    tabBar->updateLayout();

    // switch back to the classic skin
    auto uiSpr = ButtonSprite::create(tr("pai.hub.style.classic", "UI: Clasica").c_str(), "bigFont.fnt", "GJ_button_06.png", .8f);
    uiSpr->setScale(0.32f);
    auto uiBtn = CCMenuItemSpriteExtra::create(uiSpr, this, menu_selector(PaimonHubLayer::onToggleUIStyle));
    uiBtn->setID("ui-style-btn"_spr);
    uiBtn->setPosition({winSize.width - 42.f, top - 20.f});
    m_mainMenu->addChild(uiBtn);

    // tab containers (same structure the original skin builds in init)
    m_homeTab = CCLayerRGBA::create();
    m_homeTab->setID("home-tab"_spr);
    m_homeTab->setCascadeOpacityEnabled(true);
    this->addChild(m_homeTab, 5);
    m_homeMenu = CCMenu::create();
    m_homeMenu->setID("home-menu"_spr);
    m_homeMenu->setPosition({0, 0});
    this->addChild(m_homeMenu, 11);

    m_newsTab = CCLayerRGBA::create();
    m_newsTab->setID("news-tab"_spr);
    m_newsTab->setVisible(false);
    m_newsTab->setCascadeOpacityEnabled(true);
    this->addChild(m_newsTab, 5);
    m_newsMenu = CCMenu::create();
    m_newsMenu->setID("news-menu"_spr);
    m_newsMenu->setPosition({0, 0});
    m_newsMenu->setVisible(false);
    this->addChild(m_newsMenu, 11);

    m_forumTab = CCLayerRGBA::create();
    m_forumTab->setID("forum-tab"_spr);
    m_forumTab->setVisible(false);
    m_forumTab->setCascadeOpacityEnabled(true);
    this->addChild(m_forumTab, 5);
    m_forumMenu = CCMenu::create();
    m_forumMenu->setID("forum-menu"_spr);
    m_forumMenu->setPosition({0, 0});
    m_forumMenu->setVisible(false);
    this->addChild(m_forumMenu, 11);

    gdBuildHome();
    buildNewsTab();
    buildForumTab();

    switchTab(0);

    {
        auto& chk = paimon::updates::UpdateChecker::get();
        std::string verText = fmt::format(
            fmt::runtime(tr("pai.hub.version", "Version: {}")), chk.localVersion()
        );
        auto verLbl = CCLabelBMFont::create(verText.c_str(), "bigFont.fnt");
        verLbl->setScale(0.2f);
        verLbl->setColor({180, 195, 235});
        verLbl->setOpacity(160);
        verLbl->setAnchorPoint({0.f, 0.5f});
        verLbl->setPosition({17.f, 7.f});
        this->addChild(verLbl, 2);
    }

    // first-run: auto-start the guided tour once the entrance settles
    if (!Mod::get()->getSavedValue<bool>("hub-gd-tour-done", false)) {
        this->runAction(CCSequence::create(
            CCDelayTime::create(0.7f),
            CCCallFunc::create(this, callfunc_selector(PaimonHubLayer::gdStartTour)),
            nullptr
        ));
    }
}

void PaimonHubLayer::gdBuildHome() {
    auto winSize = CCDirector::get()->getWinSize();
    float panelW = winSize.width - 30.f;

    auto panel = CCScale9Sprite::create("GJ_square01.png");
    if (panel) {
        panel->setAnchorPoint({0.f, 0.f});
        panel->setContentSize({panelW, kPanelH});
        panel->setPosition({kPanelL, kPanelB});
        panel->setID("gd-home-panel"_spr);
        m_homeTab->addChild(panel, 0);
    }

    // global search: type any setting name, open its config directly
    m_searchInput = TextInput::create(120.f, tr("pai.hub.search", "Buscar...").c_str(), "chatFont.fnt");
    m_searchInput->setCommonFilter(CommonFilter::Any);
    m_searchInput->setMaxCharCount(20);
    m_searchInput->setPosition({winSize.width - 88.f, 243.f});
    m_searchInput->setScale(0.62f);
    WeakRef<PaimonHubLayer> hubRef = this;
    m_searchInput->setCallback([hubRef](std::string const& q) {
        auto selfRef = hubRef.lock();
        auto* hub = selfRef.data();
        if (!hub || !hub->getParent()) return;
        if (q.empty()) {
            if (hub->m_gdHomeState == 2) hub->gdShowCategories();
        } else {
            hub->gdShowSearchResults(q);
        }
    });
    m_homeTab->addChild(m_searchInput, 10);

    // contextual hint bar (bottom strip): tells the user what to do next
    float hintBarW = panelW - 24.f - 76.f;
    auto hintBg = CCScale9Sprite::create("GJ_square02.png");
    if (hintBg) {
        hintBg->setAnchorPoint({0.f, 0.f});
        hintBg->setContentSize({hintBarW, kHintBarH});
        hintBg->setPosition({kPanelL + 12.f, kHintBarY});
        hintBg->setID("gd-hint-bar"_spr);
        m_homeTab->addChild(hintBg, 2);
    }
    if (auto infoIcon = paimon::SpriteHelper::safeCreateWithFrameName("GJ_infoIcon_001.png")) {
        infoIcon->setScale(0.45f);
        infoIcon->setPosition({kPanelL + 24.f, kHintBarY + kHintBarH / 2.f});
        m_homeTab->addChild(infoIcon, 3);
    }
    m_gdHintLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_gdHintLabel->setAnchorPoint({0.f, 0.5f});
    m_gdHintLabel->setPosition({kPanelL + 38.f, kHintBarY + kHintBarH / 2.f});
    m_gdHintLabel->setScale(0.185f);
    m_gdHintLabel->setColor({235, 235, 245});
    m_homeTab->addChild(m_gdHintLabel, 3);

    // replayable guided tour
    auto tourSpr = ButtonSprite::create(tr("pai.hub.gd.tour", "Guia").c_str(), "goldFont.fnt", "GJ_button_06.png", .8f);
    tourSpr->setScale(0.42f);
    auto tourBtn = CCMenuItemExt::createSpriteExtra(tourSpr, [hubRef](CCMenuItemSpriteExtra*) {
        auto selfRef = hubRef.lock();
        auto* hub = selfRef.data();
        if (hub && hub->getParent()) hub->gdStartTour();
    });
    tourBtn->setID("gd-tour-btn"_spr);
    tourBtn->setPosition({winSize.width - kPanelL - 47.f, kHintBarY + kHintBarH / 2.f});
    m_homeMenu->addChild(tourBtn, 5);

    m_gdContent = CCNode::create();
    m_gdContent->setID("gd-home-content"_spr);
    m_homeTab->addChild(m_gdContent, 2);

    m_gdContentMenu = CCMenu::create();
    m_gdContentMenu->setPosition({0, 0});
    m_gdContentMenu->setID("gd-home-content-menu"_spr);
    m_homeMenu->addChild(m_gdContentMenu, 3);

    gdShowCategories(true);
}

void PaimonHubLayer::gdClearContent() {
    if (m_gdContent) m_gdContent->removeAllChildren();
    if (m_gdContentMenu) m_gdContentMenu->removeAllChildren();
    if (m_gdScroll) {
        m_gdScroll->removeFromParent();
        m_gdScroll = nullptr;
    }
}

void PaimonHubLayer::gdSetHint(std::string const& text) {
    if (!m_gdHintLabel) return;
    auto winSize = CCDirector::get()->getWinSize();
    float maxW = (winSize.width - 30.f - 24.f - 76.f) - 32.f;
    m_gdHintLabel->setString(text.c_str());
    m_gdHintLabel->setScale(0.185f);
    shrinkLabelToFit(m_gdHintLabel, maxW);
    m_gdHintLabel->stopAllActions();
    m_gdHintLabel->setOpacity(0);
    m_gdHintLabel->runAction(CCFadeTo::create(0.25f, 255));
}

void PaimonHubLayer::gdShowCategories(bool animate) {
    if (!m_gdContent || !m_gdContentMenu) return;
    gdClearContent();
    m_gdHomeState = 0;
    if (m_searchInput && !m_searchInput->getString().empty()) {
        m_searchInput->setString("");
    }

    auto winSize = CCDirector::get()->getWinSize();
    float cx = winSize.width / 2.f;
    auto categories = getHubCategories();

    auto header = CCLabelBMFont::create(
        tr("pai.hub.gd.step1", "Paso 1: Elige que personalizar").c_str(), "goldFont.fnt");
    header->setAnchorPoint({0.f, 0.5f});
    header->setPosition({30.f, 247.f});
    header->setScale(0.4f);
    m_gdContent->addChild(header);

    auto sub = CCLabelBMFont::create(
        tr("pai.hub.gd.step1.sub", "Toca una categoria para ver sus opciones").c_str(), "bigFont.fnt");
    sub->setAnchorPoint({0.f, 0.5f});
    sub->setPosition({30.f, 230.f});
    sub->setScale(0.195f);
    sub->setColor({200, 205, 225});
    m_gdContent->addChild(sub);

    size_t n = categories.size();
    WeakRef<PaimonHubLayer> self = this;
    for (size_t i = 0; i < n; i++) {
        int row = static_cast<int>(i / 3);
        int col = static_cast<int>(i % 3);
        int countInRow = std::min<int>(3, static_cast<int>(n) - row * 3);

        float x = gridColX(cx, col, countInRow);
        float y = 190.f - row * 50.f;

        auto tile = makeGDTile(
            kCatButtonFiles[i % kCatButtonFiles.size()],
            categories[i].title, categories[i].shortDesc
        );
        if (!tile) continue;

        int catIdx = static_cast<int>(i);
        auto btn = CCMenuItemExt::createSpriteExtra(tile, [self, catIdx](CCMenuItemSpriteExtra*) {
            auto selfRef = self.lock();
            auto* hub = selfRef.data();
            if (hub && hub->getParent()) hub->gdShowCategory(catIdx);
        });
        m_gdContentMenu->addChild(btn);

        if (animate) animateTileEntrance(btn, i, {x, y});
        else btn->setPosition({x, y});
    }

    gdSetHint(tr("pai.hub.gd.hint.cats",
        "Toca una categoria. Con la lupa de arriba buscas cualquier ajuste por nombre."));
}

void PaimonHubLayer::gdShowCategory(int idx) {
    if (!m_gdContent || !m_gdContentMenu) return;
    auto categories = getHubCategories();
    if (idx < 0 || idx >= static_cast<int>(categories.size())) return;

    gdClearContent();
    m_gdHomeState = 1;
    m_gdCategoryIdx = idx;
    auto const& cat = categories[idx];

    auto winSize = CCDirector::get()->getWinSize();
    float cx = winSize.width / 2.f;
    WeakRef<PaimonHubLayer> self = this;

    // back to categories
    auto backSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    backSpr->setScale(0.62f);
    auto backBtn = CCMenuItemExt::createSpriteExtra(backSpr, [self](CCMenuItemSpriteExtra*) {
        auto selfRef = self.lock();
        auto* hub = selfRef.data();
        if (hub && hub->getParent()) hub->gdShowCategories();
    });
    backBtn->setID("gd-cat-back-btn"_spr);
    backBtn->setPosition({34.f, 241.f});
    m_gdContentMenu->addChild(backBtn);

    // breadcrumb: Inicio > Category
    auto crumbHome = CCLabelBMFont::create(tr("pai.hub.gd.home", "Inicio >").c_str(), "bigFont.fnt");
    crumbHome->setAnchorPoint({0.f, 0.5f});
    crumbHome->setScale(0.26f);
    crumbHome->setColor({170, 180, 205});
    crumbHome->setPosition({52.f, 246.f});
    m_gdContent->addChild(crumbHome);

    auto crumbCat = CCLabelBMFont::create(cat.title.c_str(), "goldFont.fnt");
    crumbCat->setAnchorPoint({0.f, 0.5f});
    crumbCat->setScale(0.42f);
    crumbCat->setColor(cat.color);
    crumbCat->setPosition({52.f + crumbHome->getScaledContentSize().width + 6.f, 246.f});
    m_gdContent->addChild(crumbCat);

    // full info popup for the category
    auto infoBtn = CCMenuItemExt::createSpriteExtra(
        CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png"),
        [self, idx](CCMenuItemSpriteExtra*) {
            auto selfRef = self.lock();
            auto* hub = selfRef.data();
            if (!hub || !hub->getParent()) return;
            auto cats = getHubCategories();
            if (idx < 0 || idx >= static_cast<int>(cats.size())) return;
            auto sections = cats[idx].getInfo();
            if (auto popup = paimon::ui::FeatureInfoPopup::create(cats[idx].title, sections)) {
                popup->show();
            }
        }
    );
    infoBtn->setScale(0.42f);
    infoBtn->setPosition({
        52.f + crumbHome->getScaledContentSize().width + 6.f
            + crumbCat->getScaledContentSize().width + 12.f,
        246.f
    });
    m_gdContentMenu->addChild(infoBtn);

    auto step = CCLabelBMFont::create(
        tr("pai.hub.gd.step2", "Paso 2: Elige una accion").c_str(), "bigFont.fnt");
    step->setAnchorPoint({0.f, 0.5f});
    step->setScale(0.2f);
    step->setColor({255, 222, 120});
    step->setPosition({52.f, 228.f});
    m_gdContent->addChild(step);

    auto desc = CCLabelBMFont::create(cat.shortDesc.c_str(), "bigFont.fnt");
    desc->setAnchorPoint({0.f, 0.5f});
    desc->setScale(0.185f);
    desc->setColor({195, 200, 220});
    desc->setPosition({52.f + step->getScaledContentSize().width + 12.f, 228.f});
    shrinkLabelToFit(desc, winSize.width - 100.f - step->getScaledContentSize().width);
    m_gdContent->addChild(desc);

    auto actions = getHubActions(idx);
    for (auto& act : actions) act.categoryIndex = idx;

    // GD-skin extras that the original exposes through sidebar shortcuts
    if (idx == 5) {
        actions.push_back({
            "Quick Hub", "GJ_button_06.png",
            [](PaimonHubLayer*) {
                if (auto popup = paimon::quickhub::RadialConfigPopup::create()) popup->show();
            },
            5, "Menu radial rapido"
        });
    }
    if (idx == 0) {
        bool guideOn = paimon::guide::PaimonGuideService::get().isEnabled();
        actions.push_back({
            guideOn ? "Guia: ON" : "Guia: OFF",
            guideOn ? "GJ_button_01.png" : "GJ_button_03.png",
            [](PaimonHubLayer* hub) {
                using namespace paimon::guide;
                bool newState = !PaimonGuideService::get().isEnabled();
                PaimonGuideService::get().setEnabled(newState);
                GuideEnabledChangedEvent(kGuideEventFilter).send(newState);
                PaimonNotify::create(
                    newState
                        ? Localization::get().getString("pai.guide.toggle.on")
                        : Localization::get().getString("pai.guide.toggle.off"),
                    newState ? NotificationIcon::Success : NotificationIcon::None
                )->show();
                hub->gdShowCategory(0); // refresh the ON/OFF label
            },
            0, "Asistente Paimon en el menu"
        });
    }

    size_t n = actions.size();
    int rows = static_cast<int>((n + 2) / 3);
    float gridH = rows * kTileH + (rows - 1) * 6.f;
    float gridCenterY = 128.f;
    float topRowY = gridCenterY + gridH / 2.f - kTileH / 2.f;

    for (size_t i = 0; i < n; i++) {
        auto const& action = actions[i];
        int row = static_cast<int>(i / 3);
        int col = static_cast<int>(i % 3);
        int countInRow = std::min<int>(3, static_cast<int>(n) - row * 3);

        float x = gridColX(cx, col, countInRow);
        float y = topRowY - row * 50.f;

        auto tile = makeGDTile(action.sprite, action.title, action.desc);
        if (!tile) continue;

        auto btn = CCMenuItemExt::createSpriteExtra(tile, [self, action](CCMenuItemSpriteExtra*) {
            auto selfRef = self.lock();
            auto* hub = selfRef.data();
            if (hub && hub->getParent()) action.onPress(hub);
        });
        m_gdContentMenu->addChild(btn);
        animateTileEntrance(btn, i, {x, y});

        // first action = recommended entry point: golden outline + tag
        if (i == 0) {
            auto outline = paimon::SpriteHelper::createRoundedRectOutline(
                kTileW + 10.f, kTileH + 10.f, 7.f,
                {255 / 255.f, 222 / 255.f, 90 / 255.f, 0.9f}, 2.f
            );
            if (outline) {
                outline->setAnchorPoint({0.5f, 0.5f});
                outline->setPosition({x, y});
                m_gdContent->addChild(outline, 1);
                outline->runAction(CCRepeatForever::create(CCSequence::create(
                    CCScaleTo::create(0.55f, 1.045f),
                    CCScaleTo::create(0.55f, 1.f),
                    nullptr
                )));
            }
            auto tag = CCLabelBMFont::create(
                tr("pai.hub.gd.recommended", "EMPIEZA AQUI").c_str(), "goldFont.fnt");
            tag->setScale(0.24f);
            tag->setPosition({x, y + kTileH / 2.f + 12.f});
            m_gdContent->addChild(tag, 2);
            tag->runAction(CCRepeatForever::create(CCSequence::create(
                CCFadeTo::create(0.55f, 150),
                CCFadeTo::create(0.55f, 255),
                nullptr
            )));
        }
    }

    gdSetHint(fmt::format(
        fmt::runtime(tr("pai.hub.gd.hint.cat",
            "Estas en {}. El boton con borde dorado es el punto de partida recomendado.")),
        cat.title
    ));
}

void PaimonHubLayer::gdShowSearchResults(std::string const& query) {
    if (!m_gdContent || !m_gdContentMenu) return;
    gdClearContent();
    m_gdHomeState = 2;

    auto winSize = CCDirector::get()->getWinSize();
    auto categories = getHubCategories();

    auto toLower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        return s;
    };
    std::string lowerQuery = toLower(query);

    std::vector<HubActionMeta> results;
    for (size_t catIdx = 0; catIdx < categories.size(); ++catIdx) {
        auto catActions = getHubActions(static_cast<int>(catIdx));
        for (auto const& act : catActions) {
            if (toLower(act.title).find(lowerQuery) != std::string::npos ||
                toLower(categories[catIdx].title).find(lowerQuery) != std::string::npos) {
                HubActionMeta entry = act;
                entry.categoryIndex = static_cast<int>(catIdx);
                entry.desc = categories[catIdx].title;
                results.push_back(entry);
            }
        }
    }

    std::string lang = Mod::get()->getSettingValue<std::string>("language");
    bool isSpanish = (lang == "spanish");
    for (auto const& gs : getGranularSettings()) {
        if (toLower(gs.englishName).find(lowerQuery) != std::string::npos ||
            toLower(gs.spanishName).find(lowerQuery) != std::string::npos) {
            HubActionMeta entry;
            entry.title = isSpanish ? gs.spanishName : gs.englishName;
            entry.sprite = "GJ_button_02.png";
            entry.categoryIndex = gs.categoryIndex;
            entry.desc = categories[gs.categoryIndex].title;
            entry.onPress = [gs](PaimonHubLayer*) {
                paimon::ui::openFeatureConfigFor(gs.englishName, gs.categoryIndex);
            };
            results.push_back(entry);
        }
    }

    auto header = CCLabelBMFont::create(
        tr("pai.hub.search_results", "Resultados").c_str(), "goldFont.fnt");
    header->setAnchorPoint({0.f, 0.5f});
    header->setPosition({30.f, 247.f});
    header->setScale(0.4f);
    m_gdContent->addChild(header);

    auto sub = CCLabelBMFont::create(
        fmt::format(fmt::runtime(tr("pai.hub.gd.search.count", "{} coincidencias")), results.size()).c_str(),
        "bigFont.fnt");
    sub->setAnchorPoint({0.f, 0.5f});
    sub->setPosition({30.f, 230.f});
    sub->setScale(0.195f);
    sub->setColor({200, 205, 225});
    m_gdContent->addChild(sub);

    if (results.empty()) {
        auto none = CCLabelBMFont::create(
            tr("pai.hub.gd.search.none", "Sin resultados. Prueba con otra palabra.").c_str(),
            "bigFont.fnt");
        none->setPosition({winSize.width / 2.f, 135.f});
        none->setScale(0.3f);
        none->setColor({190, 195, 215});
        m_gdContent->addChild(none);
        gdSetHint(tr("pai.hub.gd.hint.search",
            "Toca un resultado para abrir su configuracion directamente."));
        return;
    }

    float scrollW = winSize.width - 60.f;
    float scrollH = 168.f;
    m_gdScroll = ScrollLayer::create(CCSize{scrollW, scrollH});
    m_gdScroll->setPosition({30.f, 42.f});
    m_gdScroll->setID("gd-search-scroll"_spr);
    m_homeTab->addChild(m_gdScroll, 3);

    float sCx = scrollW / 2.f;
    size_t n = results.size();
    int rows = static_cast<int>((n + 2) / 3);
    float rowH = 50.f;
    float contentH = std::max(scrollH, rows * rowH + 8.f);
    m_gdScroll->m_contentLayer->setContentSize({scrollW, contentH});

    auto scrollMenu = CCMenu::create();
    scrollMenu->setPosition({0, 0});
    scrollMenu->setContentSize(m_gdScroll->m_contentLayer->getContentSize());
    m_gdScroll->m_contentLayer->addChild(scrollMenu, 10);

    WeakRef<PaimonHubLayer> self = this;
    for (size_t i = 0; i < n; i++) {
        auto const& entry = results[i];
        int row = static_cast<int>(i / 3);
        int col = static_cast<int>(i % 3);
        int countInRow = std::min<int>(3, static_cast<int>(n) - row * 3);

        float x = gridColX(sCx, col, countInRow);
        float y = contentH - (row * rowH + rowH / 2.f + 4.f);

        auto tile = makeGDTile(entry.sprite, entry.title, entry.desc);
        if (!tile) continue;

        auto btn = CCMenuItemExt::createSpriteExtra(tile, [self, entry](CCMenuItemSpriteExtra*) {
            auto selfRef = self.lock();
            auto* hub = selfRef.data();
            if (hub && hub->getParent()) entry.onPress(hub);
        });
        btn->setPosition({x, y});
        scrollMenu->addChild(btn);
    }
    m_gdScroll->scrollToTop();

    gdSetHint(tr("pai.hub.gd.hint.search",
        "Toca un resultado para abrir su configuracion directamente."));
}

// ---- guided tour -----------------------------------------------------------

namespace {
struct GDTourStep {
    CCRect target;
    std::string title;
    std::string body;
};

std::vector<GDTourStep> makeTourSteps(CCSize winSize) {
    float cx = winSize.width / 2.f;
    float panelW = winSize.width - 30.f;
    return {
        {{kPanelL, kPanelB, panelW, kPanelH},
         tr("pai.hub.gd.tour1.title", "Nuevo Paimon Hub"),
         tr("pai.hub.gd.tour1.body", "Este es el hub con estilo GD.\nTe enseno lo basico en 5 pasos.")},
        {{cx - 187.f, 62.f, 374.f, 152.f},
         tr("pai.hub.gd.tour2.title", "Categorias"),
         tr("pai.hub.gd.tour2.body", "Todas las funciones del mod,\nagrupadas en tarjetas.\nToca una y elige la accion.")},
        {{winSize.width - 130.f, 230.f, 92.f, 26.f},
         tr("pai.hub.gd.tour3.title", "Busqueda total"),
         tr("pai.hub.gd.tour3.body", "Escribe cualquier ajuste y\nabrelo directo, sin navegar\npor menus.")},
        {{cx - 40.f, winSize.height - 32.f, 170.f, 24.f},
         tr("pai.hub.gd.tour4.title", "Noticias y Foro"),
         tr("pai.hub.gd.tour4.body", "Novedades del mod y el foro\nde la comunidad, en pestanas.")},
        {{kPanelL + 9.f, kHintBarY - 3.f, winSize.width - 30.f - 21.f - 76.f + 6.f, kHintBarH + 6.f},
         tr("pai.hub.gd.tour5.title", "Barra de ayuda"),
         tr("pai.hub.gd.tour5.body", "Aqui salen consejos segun donde\nestes. Con 'UI: Clasica' arriba\nvuelves al estilo original.")},
    };
}
} // namespace

void PaimonHubLayer::gdStartTour() {
    if (m_gdTourOverlay) return;
    if (m_currentTab != 0) switchTab(0);
    if (m_gdHomeState != 0) gdShowCategories(false);
    gdShowTourStep(0);
}

void PaimonHubLayer::gdShowTourStep(int step) {
    auto winSize = CCDirector::get()->getWinSize();
    auto steps = makeTourSteps(winSize);
    if (step < 0 || step >= static_cast<int>(steps.size())) {
        gdEndTour();
        return;
    }
    m_gdTourStep = step;

    if (m_gdTourOverlay) {
        m_gdTourOverlay->removeFromParent();
        m_gdTourOverlay = nullptr;
    }

    auto const& st = steps[step];
    auto overlay = CCNode::create();
    overlay->setID("gd-tour-overlay"_spr);
    this->addChild(overlay, 300);
    m_gdTourOverlay = overlay;

    // dim everything except the highlighted target (4 rects around it)
    auto addDim = [&](float x, float y, float w, float h) {
        if (w <= 0.f || h <= 0.f) return;
        auto dim = CCLayerColor::create(ccc4(0, 0, 0, 155));
        dim->setContentSize({w, h});
        dim->setPosition({x, y});
        overlay->addChild(dim, 0);
    };
    float tx = st.target.origin.x, ty = st.target.origin.y;
    float tw = st.target.size.width, th = st.target.size.height;
    addDim(0.f, 0.f, tx, winSize.height);
    addDim(tx + tw, 0.f, winSize.width - tx - tw, winSize.height);
    addDim(tx, 0.f, tw, ty);
    addDim(tx, ty + th, tw, winSize.height - ty - th);

    // golden frame around the target
    if (auto frame = paimon::SpriteHelper::createRoundedRectOutline(
            tw + 10.f, th + 10.f, 8.f, {255 / 255.f, 222 / 255.f, 90 / 255.f, 0.95f}, 2.5f)) {
        frame->setAnchorPoint({0.f, 0.f});
        frame->setPosition({tx - 5.f, ty - 5.f});
        overlay->addChild(frame, 1);
    }

    // explanation card, placed on the opposite half of the screen
    float targetCY = ty + th / 2.f;
    bool cardAbove = targetCY < winSize.height * 0.5f;
    float cardW = 240.f, cardH = 96.f;
    float cardX = std::clamp(tx + tw / 2.f, cardW / 2.f + 10.f, winSize.width - cardW / 2.f - 10.f);
    float cardY = cardAbove
        ? std::min(ty + th + 66.f, winSize.height - cardH / 2.f - 8.f)
        : std::max(ty - 66.f, cardH / 2.f + 8.f);

    auto card = CCScale9Sprite::create("GJ_square01.png");
    if (card) {
        card->setContentSize({cardW, cardH});
        card->setPosition({cardX, cardY});
        overlay->addChild(card, 2);
    }

    auto title = CCLabelBMFont::create(st.title.c_str(), "goldFont.fnt");
    title->setScale(0.42f);
    shrinkLabelToFit(title, cardW - 40.f);
    title->setPosition({cardX, cardY + cardH / 2.f - 16.f});
    overlay->addChild(title, 3);

    auto body = CCLabelBMFont::create(
        st.body.c_str(), "bigFont.fnt", kCCLabelAutomaticWidth, kCCTextAlignmentCenter);
    body->setScale(0.19f);
    shrinkLabelToFit(body, cardW - 24.f);
    body->setPosition({cardX, cardY + 2.f});
    body->setColor({225, 228, 240});
    overlay->addChild(body, 3);

    // bouncing arrow between the card and the target
    if (auto arrow = paimon::SpriteHelper::safeCreateWithFrameName("GJ_arrow_01_001.png")) {
        arrow->setScale(0.6f);
        arrow->setColor({255, 222, 90});
        // GJ_arrow points left; +90 = up, -90 = down
        arrow->setRotation(cardAbove ? -90.f : 90.f);
        float arrowY = cardAbove ? ty + th + 16.f : ty - 16.f;
        arrow->setPosition({std::clamp(tx + tw / 2.f, 20.f, winSize.width - 20.f), arrowY});
        overlay->addChild(arrow, 3);
        float dir = cardAbove ? -5.f : 5.f;
        arrow->runAction(CCRepeatForever::create(CCSequence::create(
            CCEaseSineOut::create(CCMoveBy::create(0.4f, {0.f, dir})),
            CCEaseSineIn::create(CCMoveBy::create(0.4f, {0.f, -dir})),
            nullptr
        )));
    }

    auto counter = CCLabelBMFont::create(
        fmt::format("{}/{}", step + 1, steps.size()).c_str(), "bigFont.fnt");
    counter->setScale(0.22f);
    counter->setColor({255, 222, 120});
    counter->setPosition({cardX - cardW / 2.f + 24.f, cardY - cardH / 2.f + 14.f});
    overlay->addChild(counter, 3);

    // controls: close (skip), next, and tap-anywhere to advance
    auto menu = CCMenu::create();
    menu->setPosition({0, 0});
    overlay->addChild(menu, 4);
    // -200 sits above every hub menu. Deferred one frame: when this runs from
    // a touch callback (Next/tap-to-advance) the dispatcher is locked, the
    // menu's handler registration is still queued, and an immediate
    // setHandlerPriority would deref a null handler (crash in setPriority).
    Ref<CCMenu> menuRef = menu;
    Loader::get()->queueInMainThread([menuRef] {
        if (menuRef && menuRef->isRunning()) {
            menuRef->setHandlerPriority(-200);
        }
    });

    WeakRef<PaimonHubLayer> self = this;
    bool isLast = (step + 1 >= static_cast<int>(steps.size()));

    if (auto closeSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_closeBtn_001.png")) {
        closeSpr->setScale(0.5f);
        auto closeBtn = CCMenuItemExt::createSpriteExtra(closeSpr, [self](CCMenuItemSpriteExtra*) {
            auto selfRef = self.lock();
            auto* hub = selfRef.data();
            if (hub && hub->getParent()) hub->gdEndTour();
        });
        closeBtn->setPosition({cardX - cardW / 2.f + 6.f, cardY + cardH / 2.f - 6.f});
        menu->addChild(closeBtn);
    }

    auto nextSpr = ButtonSprite::create(
        isLast ? tr("pai.hub.gd.tour.done", "Listo!").c_str()
               : tr("pai.hub.gd.tour.next", "Siguiente").c_str(),
        "goldFont.fnt", "GJ_button_01.png", .8f);
    nextSpr->setScale(0.5f);
    auto nextBtn = CCMenuItemExt::createSpriteExtra(nextSpr, [self, step, isLast](CCMenuItemSpriteExtra*) {
        auto selfRef = self.lock();
        auto* hub = selfRef.data();
        if (!hub || !hub->getParent()) return;
        if (isLast) hub->gdEndTour();
        else hub->gdShowTourStep(step + 1);
    });
    nextBtn->setPosition({cardX + cardW / 2.f - 46.f, cardY - cardH / 2.f + 14.f});
    menu->addChild(nextBtn);

    // added last so explicit buttons win the hit test
    auto catcherNode = CCNode::create();
    catcherNode->setContentSize(winSize);
    auto catcher = CCMenuItemExt::createSpriteExtra(catcherNode, [self, step, isLast](CCMenuItemSpriteExtra*) {
        auto selfRef = self.lock();
        auto* hub = selfRef.data();
        if (!hub || !hub->getParent()) return;
        if (isLast) hub->gdEndTour();
        else hub->gdShowTourStep(step + 1);
    });
    catcher->setPosition({winSize.width / 2.f, winSize.height / 2.f});
    menu->addChild(catcher);

    // entrance
    if (card) {
        card->setScale(0.85f);
        card->runAction(CCEaseBackOut::create(CCScaleTo::create(0.22f, 1.f)));
    }
}

void PaimonHubLayer::gdEndTour() {
    if (m_gdTourOverlay) {
        m_gdTourOverlay->removeFromParent();
        m_gdTourOverlay = nullptr;
    }
    Mod::get()->setSavedValue<bool>("hub-gd-tour-done", true);
    gdSetHint(tr("pai.hub.gd.hint.tourdone",
        "Tour completado! Puedes repetirlo con el boton Guia de abajo."));
}
