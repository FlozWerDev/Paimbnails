#include "CaptureAssetBrowserPopup.hpp"
#include "../services/CaptureVisibilityState.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/GeodeTextInputSafe.hpp"
#include "CapturePreviewPopup.hpp"
#include "CaptureMiniPreview.hpp"
#include "CaptureListWidgets.hpp"
#include "CaptureUIConstants.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/PaimonButtonHighlighter.hpp"
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/GameObject.hpp>
#include <Geode/binding/GJBaseGameLayer.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include "../services/FramebufferCapture.hpp"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <cstring>
#include <fmt/format.h>
#include "../../../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;
using namespace cocos2d;

using paimon::capture::ui::ClippedMenu;

// Intentionally leaked to avoid destruction-order crashes at DLL unload.
static auto& s_originalAssetVisibilities = *new std::vector<paimon::capture::VisibilityRecord>();
// Object IDs already snapshotted this capture session. Snapshots are taken the
// first time a type is edited: a big level has tens of thousands of objects and
// recording every one up front just to open the browser is not worth it.
static auto& s_snapshottedIDs = *new std::unordered_set<int>();

namespace {
    constexpr ccColor3B kAccent      {255, 215, 90};
    constexpr ccColor3B kTextOn      {255, 255, 255};
    constexpr ccColor3B kTextOff     {130, 130, 130};
    constexpr ccColor3B kHeaderOn    {255, 226, 120};
    constexpr ccColor3B kHeaderOff   {120, 110, 80};
    constexpr ccColor3B kPartialTint {255, 190, 90};

    std::string loc(char const* key) {
        return Localization::get().getString(key);
    }
}

std::string CaptureAssetBrowserPopup::categoryForObjectID(int id) {
    if ((id >= 10 && id <= 13) || id == 45 || id == 46 ||
        id == 47 || id == 99 || id == 101 ||
        (id >= 286 && id <= 287) ||
        (id >= 660 && id <= 661) ||
        (id >= 745 && id <= 747) ||
        (id >= 749 && id <= 750) ||
        id == 1331 || id == 1334) {
        return "assets.cat_portals";
    }

    if ((id >= 899 && id <= 915) ||
        (id >= 1006 && id <= 1019) ||
        (id >= 1049 && id <= 1062) ||
        (id >= 1268 && id <= 1275) ||
        (id >= 1346 && id <= 1364) ||
        (id >= 1585 && id <= 1620) ||
        (id >= 1811 && id <= 1818) ||
        (id >= 1912 && id <= 1917) ||
        (id >= 2062 && id <= 2070) ||
        id == 1007 || id == 1520 || id == 1595 ||
        id == 2903 || id == 2904 || id == 2905) {
        return "assets.cat_triggers";
    }

    if ((id >= 8 && id <= 9) ||
        (id >= 39 && id <= 42) ||
        (id >= 135 && id <= 136) ||
        (id >= 177 && id <= 178) ||
        (id >= 183 && id <= 184) ||
        (id >= 187 && id <= 188) ||
        (id >= 363 && id <= 369) ||
        (id >= 446 && id <= 453) ||
        (id >= 667 && id <= 680) ||
        (id >= 1701 && id <= 1714) ||
        (id >= 1715 && id <= 1720)) {
        return "assets.cat_spikes";
    }

    if ((id >= 1 && id <= 7) ||
        (id >= 15 && id <= 38) ||
        (id >= 62 && id <= 98) ||
        (id >= 119 && id <= 134) ||
        (id >= 140 && id <= 176) ||
        (id >= 247 && id <= 285) ||
        (id >= 288 && id <= 362) ||
        (id >= 370 && id <= 445) ||
        (id >= 454 && id <= 500)) {
        return "assets.cat_blocks";
    }

    if ((id >= 43 && id <= 44) || id == 48 ||
        (id >= 100 && id <= 118) ||
        (id >= 200 && id <= 246) ||
        (id >= 1022 && id <= 1048) ||
        (id >= 1330 && id <= 1345) ||
        (id >= 1594 && id <= 1599)) {
        return "assets.cat_special";
    }

    if ((id >= 501 && id <= 659) ||
        (id >= 662 && id <= 666) ||
        (id >= 681 && id <= 744) ||
        (id >= 748 && id <= 898) ||
        (id >= 916 && id <= 999) ||
        (id >= 1063 && id <= 1267) ||
        (id >= 1276 && id <= 1329) ||
        (id >= 1365 && id <= 1519) ||
        (id >= 1521 && id <= 1584) ||
        (id >= 1621 && id <= 1700) ||
        (id >= 1721 && id <= 1810) ||
        (id >= 1819 && id <= 1911) ||
        (id >= 1918 && id <= 2061) ||
        (id >= 2071 && id <= 2902)) {
        return "assets.cat_deco";
    }

    return "assets.cat_other";
}

CaptureAssetBrowserPopup* CaptureAssetBrowserPopup::create(CapturePreviewPopup* previewPopup) {
    auto ret = new CaptureAssetBrowserPopup();
    ret->m_previewPopup = previewPopup;
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

CaptureAssetBrowserPopup::~CaptureAssetBrowserPopup() {
    for (auto& g : m_groups) {
        if (g.representativeFrame) {
            g.representativeFrame->release();
            g.representativeFrame = nullptr;
        }
    }
}

void CaptureAssetBrowserPopup::restoreAllAssets() {
    if (PlayLayer::get() && !s_originalAssetVisibilities.empty()) {
        paimon::capture::restoreVisibility(s_originalAssetVisibilities);
    }
    s_originalAssetVisibilities.clear();
    s_snapshottedIDs.clear();
    log::info("[AssetBrowser] All assets restored to original visibility");
}

void CaptureAssetBrowserPopup::discardTrackedAssets() {
    s_originalAssetVisibilities.clear();
    s_snapshottedIDs.clear();
}

bool CaptureAssetBrowserPopup::init() {
    namespace C = paimon::capture::assets;
    namespace E = paimon::capture::editor;

    if (!Popup::init(C::POPUP_WIDTH, C::POPUP_HEIGHT)) return false;
    this->setTitle(loc("assets.title").c_str());

    auto content = m_mainLayer->getContentSize();

    scanObjects();

    if (m_groups.empty()) {
        auto noLabel = CCLabelBMFont::create(
            loc(PlayLayer::get() ? "assets.no_objects" : "assets.no_playlayer").c_str(),
            "bigFont.fnt");
        noLabel->setScale(0.4f);
        noLabel->setPosition({content.width * 0.5f, content.height * 0.5f});
        m_mainLayer->addChild(noLabel);
        return true;
    }

    const float previewTop = content.height - E::HEADER_TOP_PAD;
    const float previewCY  = previewTop - E::PREVIEW_H * 0.5f;
    const float previewCX  = E::SIDE_PAD + E::PREVIEW_W * 0.5f;

    m_miniPreview = paimon::capture::MiniPreview::create(E::PREVIEW_W, E::PREVIEW_H);
    if (m_miniPreview) {
        m_miniPreview->setPosition({previewCX, previewCY});
        if (auto preview = m_previewPopup.lock()) {
            m_miniPreview->setPlayersHidden(preview->isPlayer1Hidden(), preview->isPlayer2Hidden());
        }
        m_mainLayer->addChild(m_miniPreview, 1);
    }

    // Right column: search, live counters, collapse-all.
    const float colX = E::SIDE_PAD + E::PREVIEW_W + E::TOOLS_GAP;

    auto* toolMenu = CCMenu::create();
    toolMenu->setPosition({0.f, 0.f});
    toolMenu->setID("tool-menu"_spr);
    m_mainLayer->addChild(toolMenu, 3);

    m_search = TextInput::create(C::SEARCH_WIDTH, loc("assets.search_hint").c_str(), "bigFont.fnt");
    if (m_search) {
        m_search->setCommonFilter(CommonFilter::Uint);
        m_search->setMaxCharCount(4);
        m_search->setTextAlign(TextInputAlign::Left);
        m_search->setScale(C::SEARCH_SCALE);
        m_search->setAnchorPoint({0.5f, 0.5f});
        m_search->setPosition({colX + C::SEARCH_WIDTH * C::SEARCH_SCALE * 0.5f,
                               previewTop - 14.f});
        m_search->setCallback(paimon::ui::safeTextInputCallback<CaptureAssetBrowserPopup>(
            this, &CaptureAssetBrowserPopup::onSearchChanged));
        m_mainLayer->addChild(m_search, 3);

        auto* clearSpr = paimon::SpriteHelper::safeCreateWithFrameName("GJ_deleteIcon_001.png");
        if (clearSpr) {
            clearSpr->setScale(0.42f);
            auto* clearBtn = CCMenuItemSpriteExtra::create(
                clearSpr, this, menu_selector(CaptureAssetBrowserPopup::onClearSearchBtn));
            clearBtn->setPosition({colX + C::SEARCH_WIDTH * C::SEARCH_SCALE + 14.f,
                                   previewTop - 14.f});
            clearBtn->setID("clear-search"_spr);
            toolMenu->addChild(clearBtn);
        }
    }

    m_statsLabel = CCLabelBMFont::create("", "bigFont.fnt");
    m_statsLabel->setScale(0.26f);
    m_statsLabel->setAnchorPoint({0.f, 0.5f});
    m_statsLabel->setPosition({colX, previewTop - 40.f});
    m_statsLabel->setColor({200, 220, 255});
    m_mainLayer->addChild(m_statsLabel, 3);

    {
        auto* spr = ButtonSprite::create(
            loc("assets.collapse_all").c_str(), 96, true, "bigFont.fnt",
            "GJ_button_04.png", 20.f, 0.3f);
        if (spr) {
            m_collapseLabel = spr->getChildByType<CCLabelBMFont>(0);
            auto* btn = CCMenuItemSpriteExtra::create(
                spr, this, menu_selector(CaptureAssetBrowserPopup::onCollapseAllBtn));
            btn->setPosition({colX + 48.f, previewTop - E::PREVIEW_H + 12.f});
            btn->setID("collapse-all"_spr);
            PaimonButtonHighlighter::registerButton(btn);
            toolMenu->addChild(btn);
        }
    }

    m_emptyLabel = CCLabelBMFont::create(loc("assets.no_matches").c_str(), "bigFont.fnt");
    m_emptyLabel->setScale(0.32f);
    m_emptyLabel->setOpacity(160);
    m_emptyLabel->setVisible(false);
    m_emptyLabel->setPosition({content.width * 0.5f,
        (previewTop - E::PREVIEW_H - E::LIST_GAP_BELOW_HEADER + E::LIST_BOT) * 0.5f});
    m_mainLayer->addChild(m_emptyLabel, 4);

    buildList();
    updateStats();

    auto btnMenu = CCMenu::create();
    btnMenu->setPosition({content.width * 0.5f, 19.f});
    btnMenu->setID("bottom-buttons"_spr);

    auto addActionButton = [&](char const* key, char const* texture, SEL_MenuHandler handler) {
        auto* spr = ButtonSprite::create(
            loc(key).c_str(), 62, true, "bigFont.fnt", texture, 22.f, 0.33f);
        if (!spr) return;
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, handler);
        PaimonButtonHighlighter::registerButton(btn);
        btnMenu->addChild(btn);
    };

    addActionButton("assets.restore_all", "GJ_button_01.png",
        menu_selector(CaptureAssetBrowserPopup::onRestoreAllBtn));
    addActionButton("assets.hide_all", "GJ_button_05.png",
        menu_selector(CaptureAssetBrowserPopup::onHideAllBtn));
    addActionButton("assets.show_all", "GJ_button_02.png",
        menu_selector(CaptureAssetBrowserPopup::onShowAllBtn));
    addActionButton("assets.done", "GJ_button_02.png",
        menu_selector(CaptureAssetBrowserPopup::onDoneBtn));

    btnMenu->alignItemsHorizontallyWithPadding(6.f);
    m_mainLayer->addChild(btnMenu);

    paimon::markDynamicPopup(this);
    return true;
}

void CaptureAssetBrowserPopup::onClose(CCObject* sender) {
    paimon::ui::detachGeodeTextInput(m_search);
    Popup::onClose(sender);
}

void CaptureAssetBrowserPopup::keyBackClicked() {
    paimon::ui::detachGeodeTextInput(m_search);
    Popup::keyBackClicked();
}

void CaptureAssetBrowserPopup::onExit() {
    paimon::ui::detachGeodeTextInput(m_search);
    Popup::onExit();
}

bool CaptureAssetBrowserPopup::playLayerStillValid() const {
    auto* pl = PlayLayer::get();
    if (!pl) return false;
    auto scanned = m_scannedPlayLayer.lock();
    return scanned && scanned.data() == static_cast<CCNode*>(pl);
}

void CaptureAssetBrowserPopup::scanObjects() {
    auto* pl = PlayLayer::get();
    if (!pl || !pl->m_objects) return;

    m_scannedPlayLayer = pl;

    std::unordered_map<int, int> idToGroupIdx;

    for (auto* obj : CCArrayExt<GameObject*>(pl->m_objects)) {
        if (!obj) continue;

        int oid = obj->m_objectID;
        auto it = idToGroupIdx.find(oid);
        if (it == idToGroupIdx.end()) {
            AssetGroup group;
            group.objectID = oid;
            group.categoryKey = categoryForObjectID(oid);
            group.count = 1;
            group.visible = obj->isVisible();
            group.representativeFrame = obj->displayFrame();
            if (group.representativeFrame) group.representativeFrame->retain();
            group.objects.push_back(obj);
            idToGroupIdx[oid] = static_cast<int>(m_groups.size());
            m_groups.push_back(std::move(group));
        } else {
            auto& group = m_groups[it->second];
            group.count++;
            group.objects.push_back(obj);
            // A type counts as visible while any of its instances is on screen.
            if (obj->isVisible()) group.visible = true;
        }
    }

    std::sort(m_groups.begin(), m_groups.end(), [](AssetGroup const& a, AssetGroup const& b) {
        return a.count > b.count;
    });

    static const std::vector<std::string> catOrder = {
        "assets.cat_blocks", "assets.cat_spikes", "assets.cat_deco",
        "assets.cat_portals", "assets.cat_special", "assets.cat_triggers",
        "assets.cat_other"
    };

    std::unordered_map<std::string, int> catKeyToIdx;
    for (int gi = 0; gi < static_cast<int>(m_groups.size()); ++gi) {
        auto const& key = m_groups[gi].categoryKey;
        auto cit = catKeyToIdx.find(key);
        if (cit == catKeyToIdx.end()) {
            CategoryHeader hdr;
            hdr.name = Localization::get().getString(key);
            hdr.groupIndices.push_back(gi);
            catKeyToIdx[key] = static_cast<int>(m_categories.size());
            m_categories.push_back(std::move(hdr));
        } else {
            m_categories[cit->second].groupIndices.push_back(gi);
        }
    }

    std::sort(m_categories.begin(), m_categories.end(),
        [&](CategoryHeader const& a, CategoryHeader const& b) {
            auto keyOf = [&](CategoryHeader const& c) {
                return c.groupIndices.empty() ? std::string{} : m_groups[c.groupIndices[0]].categoryKey;
            };
            auto posA = std::find(catOrder.begin(), catOrder.end(), keyOf(a));
            auto posB = std::find(catOrder.begin(), catOrder.end(), keyOf(b));
            return (posA - catOrder.begin()) < (posB - catOrder.begin());
        });

    log::info("[AssetBrowser] Scanned {} object types, {} categories",
        m_groups.size(), m_categories.size());
}

bool CaptureAssetBrowserPopup::groupMatchesSearch(int groupIdx) const {
    if (m_searchQuery.empty()) return true;
    if (groupIdx < 0 || groupIdx >= static_cast<int>(m_groups.size())) return false;
    return std::to_string(m_groups[groupIdx].objectID).find(m_searchQuery) != std::string::npos;
}

bool CaptureAssetBrowserPopup::categoryHasMatches(int catIdx) const {
    if (catIdx < 0 || catIdx >= static_cast<int>(m_categories.size())) return false;
    for (int gi : m_categories[catIdx].groupIndices) {
        if (groupMatchesSearch(gi)) return true;
    }
    return false;
}

CaptureAssetBrowserPopup::TriState CaptureAssetBrowserPopup::categoryState(int catIdx) const {
    if (catIdx < 0 || catIdx >= static_cast<int>(m_categories.size())) return TriState::Visible;

    bool anyVisible = false;
    bool anyHidden  = false;
    for (int gi : m_categories[catIdx].groupIndices) {
        if (m_groups[gi].visible) anyVisible = true;
        else                      anyHidden  = true;
    }
    if (anyVisible && anyHidden) return TriState::Partial;
    return anyVisible ? TriState::Visible : TriState::Hidden;
}

void CaptureAssetBrowserPopup::buildList() {
    namespace C = paimon::capture::assets;
    namespace E = paimon::capture::editor;

    float savedScroll = 0.f;
    bool hadScroll = false;
    if (m_scrollView && m_scrollView->m_contentLayer) {
        savedScroll = m_scrollView->m_contentLayer->getPositionY();
        hadScroll = true;
    }

    if (m_listRoot) {
        m_listRoot->removeFromParentAndCleanup(true);
        m_listRoot = nullptr;
        m_scrollView = nullptr;
    }

    for (auto& g : m_groups) { g.toggler = nullptr; g.label = nullptr; }
    for (auto& c : m_categories) { c.toggler = nullptr; c.label = nullptr; }

    auto content = m_mainLayer->getContentSize();

    const float listW   = content.width - E::SIDE_PAD * 2;
    const float rowH    = C::ROW_HEIGHT;
    const float listTop = content.height - E::HEADER_TOP_PAD - E::PREVIEW_H - E::LIST_GAP_BELOW_HEADER;
    const float listBot = E::LIST_BOT;
    const float viewH   = listTop - listBot;
    const float viewX   = E::SIDE_PAD;

    // Flatten to visual rows first: collapsed categories and search misses drop
    // out here, so the scroll height always matches what is drawn.
    struct VisualRow { int categoryIdx; int groupIdx; };
    std::vector<VisualRow> rows;
    for (int ci = 0; ci < static_cast<int>(m_categories.size()); ++ci) {
        if (!categoryHasMatches(ci)) continue;
        rows.push_back({ci, -1});
        if (m_categories[ci].collapsed && m_searchQuery.empty()) continue;
        for (int gi : m_categories[ci].groupIndices) {
            if (groupMatchesSearch(gi)) rows.push_back({ci, gi});
        }
    }

    if (m_emptyLabel) m_emptyLabel->setVisible(rows.empty());

    m_listRoot = CCNode::create();
    m_listRoot->setID("asset-list-root"_spr);
    m_mainLayer->addChild(m_listRoot, 2);

    auto panel = paimon::SpriteHelper::createDarkPanel(listW, viewH, 80);
    panel->setPosition({viewX, listBot});
    m_listRoot->addChild(panel, 0);

    float totalH = std::max(viewH, static_cast<float>(rows.size()) * rowH);

    m_scrollView = ScrollLayer::create({listW, viewH});
    m_scrollView->setPosition({viewX, listBot});
    m_scrollView->m_contentLayer->setContentSize({listW, totalH});

    for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
        int const catIdx   = rows[row].categoryIdx;
        int const groupIdx = rows[row].groupIdx;
        float const y = totalH - rowH - row * rowH;

        auto rowNode = CCNode::create();
        rowNode->setContentSize({listW, rowH});
        rowNode->setPosition({0.f, y});
        rowNode->setAnchorPoint({0.f, 0.f});

        auto rowMenu = ClippedMenu::create(m_scrollView);
        rowMenu->setContentSize({listW, rowH});
        rowMenu->setPosition({0.f, 0.f});
        rowMenu->setAnchorPoint({0.f, 0.f});

        if (groupIdx < 0) {
            auto& cat = m_categories[catIdx];

            if (auto* bg = paimon::capture::ui::makeRowFill(
                    listW, rowH, {kAccent.r, kAccent.g, kAccent.b, E::GROUP_BG_ALPHA})) {
                rowNode->addChild(bg, -2);
            }
            if (auto* accent = paimon::capture::ui::makeRowFill(
                    E::GROUP_ACCENT_WIDTH, rowH - 4.f,
                    {kAccent.r, kAccent.g, kAccent.b, E::GROUP_ACCENT_ALPHA})) {
                accent->setPosition({3.f, 2.f});
                rowNode->addChild(accent, -1);
            }

            bool const expanded = !cat.collapsed || !m_searchQuery.empty();
            if (auto* arrow = paimon::capture::ui::makeDisclosure(expanded, E::ARROW_SCALE)) {
                arrow->setPosition({E::ARROW_X, rowH * 0.5f});
                rowNode->addChild(arrow, 2);
            }

            int totalCount = 0;
            int hiddenTypes = 0;
            for (int gi : cat.groupIndices) {
                totalCount += m_groups[gi].count;
                if (!m_groups[gi].visible) ++hiddenTypes;
            }

            std::string headerText = cat.name + "  (" + std::to_string(totalCount) + ")";
            if (hiddenTypes > 0) {
                headerText += "  -" + std::to_string(hiddenTypes);
            }
            auto label = CCLabelBMFont::create(headerText.c_str(), "bigFont.fnt");
            label->limitLabelWidth(listW - C::CHECK_X_FROM_RIGHT - 44.f,
                                   C::LABEL_SCALE_HEADER, 0.18f);
            label->setAnchorPoint({0.f, 0.5f});
            label->setPosition({22.f, rowH * 0.5f});
            rowNode->addChild(label, 2);
            cat.label = label;

            // Tap anywhere on the header (except the checkbox) to fold it.
            if (auto* hit = paimon::capture::ui::makeRowHitArea(
                    listW - 34.f, rowH, this,
                    menu_selector(CaptureAssetBrowserPopup::onToggleCollapse), catIdx)) {
                hit->setPosition({(listW - 34.f) * 0.5f, rowH * 0.5f});
                rowMenu->addChild(hit);
            }

            auto const state = categoryState(catIdx);
            if (auto* toggler = paimon::capture::ui::makeCheck(
                    C::CHECK_SCALE_HEADER, this,
                    menu_selector(CaptureAssetBrowserPopup::onToggleCategory),
                    catIdx, state != TriState::Hidden,
                    state == TriState::Partial ? kPartialTint : kTextOn)) {
                toggler->setPosition({listW - C::CHECK_X_FROM_RIGHT, rowH * 0.5f});
                rowMenu->addChild(toggler);
                cat.toggler = toggler;
            }

            label->setColor(state == TriState::Hidden ? kHeaderOff : kHeaderOn);
            rowNode->addChild(rowMenu, 3);
            m_scrollView->m_contentLayer->addChild(rowNode);
            continue;
        }

        auto& group = m_groups[groupIdx];

        if (row % 2 == 0) {
            if (auto* bg = paimon::capture::ui::makeRowFill(listW, rowH, {255, 255, 255, E::ALT_ROW_ALPHA})) {
                rowNode->addChild(bg, -1);
            }
        }

        bool spriteAdded = false;
        if (group.representativeFrame) {
            if (auto* preview = CCSprite::createWithSpriteFrame(group.representativeFrame)) {
                auto cs = preview->getContentSize();
                float maxDim = std::max(cs.width, cs.height);
                if (maxDim > 0.f) preview->setScale(C::SPRITE_SIZE / maxDim);
                preview->setPosition({C::SPRITE_X, rowH * 0.5f});
                preview->setAnchorPoint({0.5f, 0.5f});
                if (!group.visible) preview->setOpacity(90);
                rowNode->addChild(preview, 1);
                spriteAdded = true;
            }
        }
        if (!spriteAdded) {
            auto fallback = CCLayerColor::create({180, 180, 180, 200});
            fallback->setContentSize({C::SPRITE_SIZE * 0.6f, C::SPRITE_SIZE * 0.6f});
            fallback->ignoreAnchorPointForPosition(false);
            fallback->setAnchorPoint({0.5f, 0.5f});
            fallback->setPosition({C::SPRITE_X, rowH * 0.5f});
            rowNode->addChild(fallback, 1);
        }

        auto label = CCLabelBMFont::create(
            fmt::format("ID {}", group.objectID).c_str(), "bigFont.fnt");
        label->setScale(C::LABEL_SCALE_ROW);
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({C::LABEL_X, rowH * 0.5f});
        label->setColor(group.visible ? kTextOn : kTextOff);
        rowNode->addChild(label, 2);
        group.label = label;

        auto countLabel = CCLabelBMFont::create(
            fmt::format("x{}", group.count).c_str(), "bigFont.fnt");
        countLabel->setScale(C::COUNT_SCALE);
        countLabel->setAnchorPoint({1.f, 0.5f});
        countLabel->setPosition({listW - C::COUNT_X_FROM_RIGHT, rowH * 0.5f});
        countLabel->setColor({180, 220, 255});
        rowNode->addChild(countLabel, 2);

        if (auto* soloSpr = ButtonSprite::create(
                loc("assets.solo").c_str(), 30, true, "bigFont.fnt", "GJ_button_04.png", 16.f, 0.24f)) {
            auto* soloBtn = CCMenuItemSpriteExtra::create(
                soloSpr, this, menu_selector(CaptureAssetBrowserPopup::onSoloGroup));
            soloBtn->setTag(groupIdx);
            soloBtn->setPosition({listW - C::SOLO_X_FROM_RIGHT, rowH * 0.5f});
            rowMenu->addChild(soloBtn);
        }

        if (auto* toggler = paimon::capture::ui::makeCheck(
                C::CHECK_SCALE, this, menu_selector(CaptureAssetBrowserPopup::onToggleGroup),
                groupIdx, group.visible)) {
            toggler->setPosition({listW - C::CHECK_X_FROM_RIGHT, rowH * 0.5f});
            rowMenu->addChild(toggler);
            group.toggler = toggler;
        }

        rowNode->addChild(rowMenu, 3);
        m_scrollView->m_contentLayer->addChild(rowNode);
    }

    m_scrollView->scrollToTop();
    if (hadScroll && totalH > viewH) {
        float minY = viewH - totalH;
        m_scrollView->m_contentLayer->setPositionY(std::clamp(savedScroll, minY, 0.f));
    }
    m_listRoot->addChild(m_scrollView, 2);
}

void CaptureAssetBrowserPopup::refreshPreview() {
    if (m_miniPreview) m_miniPreview->requestRefresh();
}

void CaptureAssetBrowserPopup::updateStats() {
    if (!m_statsLabel) return;

    int total = 0, hidden = 0, matches = 0;
    for (int gi = 0; gi < static_cast<int>(m_groups.size()); ++gi) {
        auto const& g = m_groups[gi];
        total += g.count;
        if (!g.visible) hidden += g.count;
        if (groupMatchesSearch(gi)) ++matches;
    }

    std::string text = loc("assets.stats_hidden") + " " + std::to_string(hidden)
                     + " / " + std::to_string(total);
    if (!m_searchQuery.empty()) {
        text += "   " + loc("assets.stats_matches") + " " + std::to_string(matches);
    }
    m_statsLabel->setString(text.c_str());
}

void CaptureAssetBrowserPopup::updateRowVisuals(int groupIdx) {
    if (groupIdx < 0 || groupIdx >= static_cast<int>(m_groups.size())) return;
    auto& group = m_groups[groupIdx];

    if (group.toggler && group.toggler->isToggled() != group.visible) {
        group.toggler->toggle(group.visible);
    }
    if (group.label) {
        group.label->setColor(group.visible ? kTextOn : kTextOff);
    }
}

void CaptureAssetBrowserPopup::updateCategoryVisuals(int catIdx) {
    if (catIdx < 0 || catIdx >= static_cast<int>(m_categories.size())) return;
    auto& cat = m_categories[catIdx];

    auto const state = categoryState(catIdx);
    if (cat.toggler) {
        bool const shouldBeOn = state != TriState::Hidden;
        if (cat.toggler->isToggled() != shouldBeOn) cat.toggler->toggle(shouldBeOn);
        if (auto* onButton = cat.toggler->m_onButton) {
            if (auto* spr = typeinfo_cast<CCSprite*>(onButton->getNormalImage())) {
                spr->setColor(state == TriState::Partial ? kPartialTint : kTextOn);
            }
        }
    }
    if (cat.label) {
        cat.label->setColor(state == TriState::Hidden ? kHeaderOff : kHeaderOn);

        int totalCount = 0, hiddenTypes = 0;
        for (int gi : cat.groupIndices) {
            totalCount += m_groups[gi].count;
            if (!m_groups[gi].visible) ++hiddenTypes;
        }
        std::string headerText = cat.name + "  (" + std::to_string(totalCount) + ")";
        if (hiddenTypes > 0) headerText += "  -" + std::to_string(hiddenTypes);
        cat.label->setString(headerText.c_str());
    }
}

void CaptureAssetBrowserPopup::snapshotGroup(int groupIdx) {
    if (groupIdx < 0 || groupIdx >= static_cast<int>(m_groups.size())) return;
    auto& group = m_groups[groupIdx];
    if (!s_snapshottedIDs.insert(group.objectID).second) return;

    s_originalAssetVisibilities.reserve(
        s_originalAssetVisibilities.size() + group.objects.size());
    for (auto* obj : group.objects) {
        if (obj) s_originalAssetVisibilities.push_back({obj, obj->isVisible()});
    }
}

void CaptureAssetBrowserPopup::setGroupVisible(int groupIdx, bool visible) {
    if (groupIdx < 0 || groupIdx >= static_cast<int>(m_groups.size())) return;
    if (!playLayerStillValid()) return;

    snapshotGroup(groupIdx);

    auto& group = m_groups[groupIdx];
    group.visible = visible;
    for (auto* obj : group.objects) {
        if (obj) obj->setVisible(visible);
    }
}

void CaptureAssetBrowserPopup::setCategoryVisible(int catIdx, bool visible) {
    if (catIdx < 0 || catIdx >= static_cast<int>(m_categories.size())) return;

    for (int gi : m_categories[catIdx].groupIndices) {
        if (!groupMatchesSearch(gi)) continue;
        setGroupVisible(gi, visible);
        updateRowVisuals(gi);
    }
    updateCategoryVisuals(catIdx);
}

void CaptureAssetBrowserPopup::setMatchingVisible(bool visible) {
    for (int gi = 0; gi < static_cast<int>(m_groups.size()); ++gi) {
        if (!groupMatchesSearch(gi)) continue;
        setGroupVisible(gi, visible);
        updateRowVisuals(gi);
    }
    for (int ci = 0; ci < static_cast<int>(m_categories.size()); ++ci) {
        updateCategoryVisuals(ci);
    }
}

void CaptureAssetBrowserPopup::soloGroup(int groupIdx) {
    if (groupIdx < 0 || groupIdx >= static_cast<int>(m_groups.size())) return;

    // Solo again on the only visible type means "bring everything back".
    bool alreadySolo = m_groups[groupIdx].visible;
    for (int gi = 0; alreadySolo && gi < static_cast<int>(m_groups.size()); ++gi) {
        if (gi != groupIdx && m_groups[gi].visible) alreadySolo = false;
    }

    for (int gi = 0; gi < static_cast<int>(m_groups.size()); ++gi) {
        setGroupVisible(gi, alreadySolo ? true : gi == groupIdx);
        updateRowVisuals(gi);
    }
    for (int ci = 0; ci < static_cast<int>(m_categories.size()); ++ci) {
        updateCategoryVisuals(ci);
    }
}

void CaptureAssetBrowserPopup::refreshGroupStatesFromScene() {
    if (!playLayerStillValid()) return;

    for (auto& group : m_groups) {
        bool anyVisible = false;
        for (auto* obj : group.objects) {
            if (obj && obj->isVisible()) { anyVisible = true; break; }
        }
        group.visible = anyVisible;
    }
}

void CaptureAssetBrowserPopup::onToggleGroup(CCObject* sender) {
    auto* toggler = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggler) return;

    int gi = toggler->getTag();
    if (gi < 0 || gi >= static_cast<int>(m_groups.size())) return;

    bool newVisible = toggler->isToggled();
    setGroupVisible(gi, newVisible);
    updateRowVisuals(gi);

    for (int ci = 0; ci < static_cast<int>(m_categories.size()); ++ci) {
        auto const& cat = m_categories[ci];
        if (std::find(cat.groupIndices.begin(), cat.groupIndices.end(), gi) != cat.groupIndices.end()) {
            updateCategoryVisuals(ci);
            break;
        }
    }

    log::info("[AssetBrowser] Object ID {} -> {}", m_groups[gi].objectID,
        newVisible ? "visible" : "hidden");

    updateStats();
    refreshPreview();
}

void CaptureAssetBrowserPopup::onToggleCategory(CCObject* sender) {
    auto* toggler = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggler) return;

    int catIdx = toggler->getTag();
    if (catIdx < 0 || catIdx >= static_cast<int>(m_categories.size())) return;

    setCategoryVisible(catIdx, toggler->isToggled());
    updateStats();
    refreshPreview();
}

void CaptureAssetBrowserPopup::onToggleCollapse(CCObject* sender) {
    auto* node = typeinfo_cast<CCNode*>(sender);
    if (!node) return;

    int catIdx = node->getTag();
    if (catIdx < 0 || catIdx >= static_cast<int>(m_categories.size())) return;
    if (!m_searchQuery.empty()) return; // search already forces every match open

    m_categories[catIdx].collapsed = !m_categories[catIdx].collapsed;

    // Rebuilding destroys the menu that is dispatching this touch.
    Ref<CaptureAssetBrowserPopup> self = this;
    Loader::get()->queueInMainThread([self]() {
        if (paimon::isRuntimeShuttingDown()) return;
        if (!self || !self->getParent()) return;
        self->buildList();
    });
}

void CaptureAssetBrowserPopup::onSoloGroup(CCObject* sender) {
    auto* node = typeinfo_cast<CCNode*>(sender);
    if (!node) return;

    soloGroup(node->getTag());
    updateStats();
    refreshPreview();
}

void CaptureAssetBrowserPopup::onCollapseAllBtn(CCObject*) {
    m_allCollapsed = !m_allCollapsed;
    for (auto& cat : m_categories) cat.collapsed = m_allCollapsed;
    if (m_collapseLabel) {
        m_collapseLabel->setString(
            loc(m_allCollapsed ? "assets.expand_all" : "assets.collapse_all").c_str());
    }

    Ref<CaptureAssetBrowserPopup> self = this;
    Loader::get()->queueInMainThread([self]() {
        if (paimon::isRuntimeShuttingDown()) return;
        if (!self || !self->getParent()) return;
        self->buildList();
    });
}

void CaptureAssetBrowserPopup::onClearSearchBtn(CCObject*) {
    if (m_search) m_search->setString("");
    if (m_searchQuery.empty()) return;
    m_searchQuery.clear();
    buildList();
    updateStats();
}

void CaptureAssetBrowserPopup::onSearchChanged(std::string const& text) {
    if (text == m_searchQuery) return;
    m_searchQuery = text;
    buildList();
    updateStats();
}

void CaptureAssetBrowserPopup::onDoneBtn(CCObject*) {
    auto previewRef = m_previewPopup.lock();
    this->onClose(nullptr);

    if (previewRef) previewRef->liveRecapture(true);
}

void CaptureAssetBrowserPopup::onRestoreAllBtn(CCObject*) {
    paimon::capture::restoreVisibility(s_originalAssetVisibilities);
    s_originalAssetVisibilities.clear();
    s_snapshottedIDs.clear();

    refreshGroupStatesFromScene();

    buildList();
    updateStats();
    refreshPreview();

    PaimonNotify::create(loc("assets.restored").c_str(), NotificationIcon::Success)->show();
}

void CaptureAssetBrowserPopup::onShowAllBtn(CCObject*) {
    setMatchingVisible(true);
    updateStats();
    refreshPreview();
}

void CaptureAssetBrowserPopup::onHideAllBtn(CCObject*) {
    setMatchingVisible(false);
    updateStats();
    refreshPreview();
}
