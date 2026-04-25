#include "CaptureAssetBrowserPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "CapturePreviewPopup.hpp"
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
#include <Geode/cocos/platform/CCGL.h>
#include <Geode/cocos/kazmath/include/kazmath/GL/matrix.h>
#include <Geode/cocos/kazmath/include/kazmath/mat4.h>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <cstring>
#include <fmt/format.h>

using namespace geode::prelude;
using namespace cocos2d;

// Lightweight static backup: which objectIDs were modified + which raw ptrs were originally hidden.
// These are only valid while PlayLayer is alive (cleared on onQuit via restoreAllAssets).
// Heap-allocated and intentionally leaked to avoid destruction-order crashes at DLL unload.
static auto& s_modifiedObjectIDs = *new std::unordered_set<int>();
static auto& s_originallyHidden  = *new std::unordered_set<GameObject*>();

// ─── helpers ──────────────────────────────────────────────────────

namespace {
    // CCMenu subclass that clips touch testing to scroll bounds
    // (same pattern as CaptureLayerEditorPopup)
    class ClippedMenu : public CCMenu {
    public:
        static ClippedMenu* create(CCNode* clipParent) {
            auto ret = new ClippedMenu();
            if (ret && ret->init()) {
                ret->m_clipParent = clipParent;
                ret->autorelease();
                return ret;
            }
            CC_SAFE_DELETE(ret);
            return nullptr;
        }

        bool ccTouchBegan(CCTouch* touch, CCEvent* event) override {
            if (m_clipParent) {
                auto worldPt = touch->getLocation();
                auto parentPos = m_clipParent->convertToWorldSpace({0.f, 0.f});
                auto parentSize = m_clipParent->getContentSize();
                auto parentScale = m_clipParent->getScale();
                float w = parentSize.width * parentScale;
                float h = parentSize.height * parentScale;
                if (worldPt.x < parentPos.x || worldPt.x > parentPos.x + w ||
                    worldPt.y < parentPos.y || worldPt.y > parentPos.y + h) {
                    return false;
                }
            }
            return CCMenu::ccTouchBegan(touch, event);
        }

    private:
        CCNode* m_clipParent = nullptr;
    };
}

// ─── category classification by objectID ──────────────────────────

std::string CaptureAssetBrowserPopup::categoryForObjectID(int id) {
    // Portals
    if ((id >= 10 && id <= 13) || id == 45 || id == 46 ||
        id == 47 || id == 99 || id == 101 ||
        (id >= 286 && id <= 287) ||
        (id >= 660 && id <= 661) ||
        (id >= 745 && id <= 747) ||
        (id >= 749 && id <= 750) ||
        id == 1331 || id == 1334) {
        return "assets.cat_portals";
    }

    // Triggers (900+, 1000+ range)
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

    // Spikes
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

    // Blocks/solids (main building blocks)
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

    // Special (orbs, pads, pickups, etc.)
    if ((id >= 43 && id <= 44) || id == 48 ||
        (id >= 100 && id <= 118) ||
        (id >= 200 && id <= 246) ||
        (id >= 1022 && id <= 1048) ||
        (id >= 1330 && id <= 1345) ||
        (id >= 1594 && id <= 1599)) {
        return "assets.cat_special";
    }

    // Decoration (wide range of deco IDs)
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

// ─── static API ───────────────────────────────────────────────────

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

void CaptureAssetBrowserPopup::restoreAllAssets() {
    auto* pl = PlayLayer::get();
    if (pl && pl->m_objects && !s_modifiedObjectIDs.empty()) {
        for (auto* obj : CCArrayExt<GameObject*>(pl->m_objects)) {
            if (!obj) continue;
            if (s_modifiedObjectIDs.count(obj->m_objectID)) {
                // Restore: visible unless it was originally hidden
                obj->setVisible(s_originallyHidden.count(obj) == 0);
            }
        }
    }
    s_modifiedObjectIDs.clear();
    s_originallyHidden.clear();
    log::info("[AssetBrowser] All assets restored to original visibility");
}

// ─── init ─────────────────────────────────────────────────────────

bool CaptureAssetBrowserPopup::init() {
    namespace C = paimon::capture::assets;

    if (!Popup::init(C::POPUP_WIDTH, C::POPUP_HEIGHT)) return false;
    this->setTitle(Localization::get().getString("assets.title").c_str());

    auto content = m_mainLayer->getContentSize();

    scanViewportObjects();

    if (m_groups.empty()) {
        auto noLabel = CCLabelBMFont::create(
            Localization::get().getString("assets.no_objects").c_str(),
            "bigFont.fnt"
        );
        noLabel->setScale(0.4f);
        noLabel->setPosition({content.width * 0.5f, content.height * 0.5f});
        m_mainLayer->addChild(noLabel);
        return true;
    }

    // ── mini preview ──────────────────────────────────────────────
    const float previewW = C::MINI_PREVIEW_W;
    const float previewH = C::MINI_PREVIEW_H;
    const float previewY = content.height - C::MINI_PREVIEW_TOP_PAD - previewH * 0.5f;

    auto previewBg = CCLayerColor::create({0, 0, 0, 200});
    previewBg->setContentSize({previewW + 4.f, previewH + 4.f});
    previewBg->ignoreAnchorPointForPosition(false);
    previewBg->setAnchorPoint({0.5f, 0.5f});
    previewBg->setPosition({content.width * 0.5f, previewY});
    m_mainLayer->addChild(previewBg, 0);

    m_miniPreview = CCSprite::create();
    m_miniPreview->setContentSize({previewW, previewH});
    m_miniPreview->setPosition({content.width * 0.5f, previewY});
    m_mainLayer->addChild(m_miniPreview, 1);

    updateMiniPreview();

    // ── object list ───────────────────────────────────────────────
    buildList();

    // ── bottom buttons ────────────────────────────────────────────
    auto btnMenu = CCMenu::create();
    btnMenu->setPosition({content.width * 0.5f, 20.f});
    btnMenu->setID("bottom-buttons"_spr);

    auto restoreSpr = ButtonSprite::create(
        Localization::get().getString("assets.restore_all").c_str(),
        60, true, "bigFont.fnt", "GJ_button_01.png", 22.f, 0.35f
    );
    if (restoreSpr) {
        auto btn = CCMenuItemSpriteExtra::create(
            restoreSpr, this,
            menu_selector(CaptureAssetBrowserPopup::onRestoreAllBtn)
        );
        PaimonButtonHighlighter::registerButton(btn);
        btnMenu->addChild(btn);
    }

    auto hideAllSpr = ButtonSprite::create(
        Localization::get().getString("assets.hide_all").c_str(),
        60, true, "bigFont.fnt", "GJ_button_05.png", 22.f, 0.35f
    );
    if (hideAllSpr) {
        auto btn = CCMenuItemSpriteExtra::create(
            hideAllSpr, this,
            menu_selector(CaptureAssetBrowserPopup::onHideAllBtn)
        );
        PaimonButtonHighlighter::registerButton(btn);
        btnMenu->addChild(btn);
    }

    auto showAllSpr = ButtonSprite::create(
        Localization::get().getString("assets.show_all").c_str(),
        60, true, "bigFont.fnt", "GJ_button_02.png", 22.f, 0.35f
    );
    if (showAllSpr) {
        auto btn = CCMenuItemSpriteExtra::create(
            showAllSpr, this,
            menu_selector(CaptureAssetBrowserPopup::onShowAllBtn)
        );
        PaimonButtonHighlighter::registerButton(btn);
        btnMenu->addChild(btn);
    }

    auto doneSpr = ButtonSprite::create(
        Localization::get().getString("assets.done").c_str(),
        60, true, "bigFont.fnt", "GJ_button_02.png", 22.f, 0.35f
    );
    if (doneSpr) {
        auto btn = CCMenuItemSpriteExtra::create(
            doneSpr, this,
            menu_selector(CaptureAssetBrowserPopup::onDoneBtn)
        );
        PaimonButtonHighlighter::registerButton(btn);
        btnMenu->addChild(btn);
    }

    btnMenu->alignItemsHorizontallyWithPadding(6.f);
    m_mainLayer->addChild(btnMenu);

    paimon::markDynamicPopup(this);
    return true;
}

void CaptureAssetBrowserPopup::onClose(CCObject* sender) {
    Popup::onClose(sender);
}

void CaptureAssetBrowserPopup::keyBackClicked() {
    Popup::keyBackClicked();
}

void CaptureAssetBrowserPopup::onExit() {
    for (auto& g : m_groups) {
        if (g.representativeFrame) {
            g.representativeFrame->release();
            g.representativeFrame = nullptr;
        }
    }
    Popup::onExit();
}

// ─── viewport scan ────────────────────────────────────────────────

void CaptureAssetBrowserPopup::scanViewportObjects() {
    namespace C = paimon::capture::assets;

    auto* pl = PlayLayer::get();
    if (!pl) return;

    // Scan ALL level objects (no viewport filter — captures everything in the level)
    std::unordered_map<int, int> idToGroupIdx; // objectID -> index in m_groups
    bool needRecordOriginals = s_modifiedObjectIDs.empty();

    if (!pl->m_objects) return;

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
            group.originalVisible = obj->isVisible();
            // Grab one representative frame for the preview sprite (retained)
            group.representativeFrame = obj->displayFrame();
            if (group.representativeFrame) group.representativeFrame->retain();
            idToGroupIdx[oid] = static_cast<int>(m_groups.size());
            m_groups.push_back(std::move(group));
        } else {
            m_groups[it->second].count++;
        }

        if (needRecordOriginals) {
            s_modifiedObjectIDs.insert(oid);
            if (!obj->isVisible()) {
                s_originallyHidden.insert(obj);
            }
        }
    }

    // Sort groups by count descending
    std::sort(m_groups.begin(), m_groups.end(), [](const AssetGroup& a, const AssetGroup& b) {
        return a.count > b.count;
    });

    // Rebuild category structure
    std::map<std::string, int> catKeyToIdx;
    // Desired category order
    static const std::vector<std::string> catOrder = {
        "assets.cat_blocks", "assets.cat_spikes", "assets.cat_deco",
        "assets.cat_portals", "assets.cat_special", "assets.cat_triggers",
        "assets.cat_other"
    };

    for (int gi = 0; gi < static_cast<int>(m_groups.size()); ++gi) {
        auto& key = m_groups[gi].categoryKey;
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

    // Sort categories by the predefined order
    std::sort(m_categories.begin(), m_categories.end(), [&](const CategoryHeader& a, const CategoryHeader& b) {
        // Use the first group's categoryKey to determine order
        auto keyA = a.groupIndices.empty() ? "" : m_groups[a.groupIndices[0]].categoryKey;
        auto keyB = b.groupIndices.empty() ? "" : m_groups[b.groupIndices[0]].categoryKey;
        auto posA = std::find(catOrder.begin(), catOrder.end(), keyA);
        auto posB = std::find(catOrder.begin(), catOrder.end(), keyB);
        return (posA - catOrder.begin()) < (posB - catOrder.begin());
    });

    log::info("[AssetBrowser] Scanned {} object types, {} categories",
        m_groups.size(), m_categories.size());
}

// ─── build list ───────────────────────────────────────────────────

void CaptureAssetBrowserPopup::buildList() {
    namespace C = paimon::capture::assets;

    if (m_listRoot) {
        m_listRoot->removeFromParentAndCleanup(true);
        m_listRoot = nullptr;
        m_scrollView = nullptr;
    }

    m_groupTogglers.clear();
    m_groupTogglers.resize(m_groups.size(), nullptr);

    auto content = m_mainLayer->getContentSize();

    const float listW   = content.width - C::LIST_PAD_X * 2;
    const float rowH    = C::ROW_HEIGHT;
    const float previewH = C::MINI_PREVIEW_H;
    const float previewY = content.height - C::MINI_PREVIEW_TOP_PAD - previewH * 0.5f;
    const float listTop  = previewY - previewH * 0.5f - C::LIST_GAP_BELOW_PREVIEW;
    const float listBot  = C::LIST_BOT;
    const float viewH    = listTop - listBot;
    const float viewX    = (content.width - listW) * 0.5f;

    // Count total rows: category headers + group rows
    int totalRows = 0;
    for (auto& cat : m_categories) {
        totalRows += 1 + static_cast<int>(cat.groupIndices.size());
    }

    m_listRoot = CCNode::create();
    m_listRoot->setID("asset-list-root"_spr);
    m_mainLayer->addChild(m_listRoot, 2);

    // Dark background
    auto panel = paimon::SpriteHelper::createDarkPanel(listW, viewH, 80);
    panel->setPosition({viewX, listBot});
    m_listRoot->addChild(panel, 0);

    float totalH = std::max(viewH, totalRows * rowH);

    m_scrollView = ScrollLayer::create({listW, viewH});
    m_scrollView->setPosition({viewX, listBot});
    m_scrollView->m_contentLayer->setContentSize({listW, totalH});

    int row = 0;
    for (int catIdx = 0; catIdx < static_cast<int>(m_categories.size()); ++catIdx) {
        auto& cat = m_categories[catIdx];
        float y = totalH - rowH - row * rowH;

        // ── Category header row ──────────────────────────────────
        {
            auto rowNode = CCNode::create();
            rowNode->setContentSize({listW, rowH});
            rowNode->setPosition({0.f, y});
            rowNode->setAnchorPoint({0.f, 0.f});

            // Header background
            auto bg = CCLayerColor::create({255, 215, 90, C::HEADER_BG_ALPHA});
            bg->setContentSize({listW, rowH});
            bg->setAnchorPoint({0.f, 0.f});
            bg->ignoreAnchorPointForPosition(false);
            bg->setPosition({0.f, 0.f});
            rowNode->addChild(bg, -2);

            // Accent bar
            auto accent = CCLayerColor::create({255, 215, 90, C::HEADER_ACCENT_ALPHA});
            accent->setContentSize({C::HEADER_ACCENT_WIDTH, rowH - 4.f});
            accent->ignoreAnchorPointForPosition(false);
            accent->setAnchorPoint({0.f, 0.5f});
            accent->setPosition({4.f, rowH * 0.5f});
            rowNode->addChild(accent, -1);

            // Category toggle
            auto catMenu = ClippedMenu::create(m_scrollView);
            catMenu->setContentSize({listW, rowH});
            catMenu->setPosition({0.f, 0.f});
            catMenu->setAnchorPoint({0.f, 0.f});

            auto onSpr  = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
            auto offSpr = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
            if (onSpr && offSpr) {
                onSpr->setScale(0.52f);
                offSpr->setScale(0.52f);
                auto toggler = CCMenuItemToggler::create(
                    offSpr, onSpr,
                    this, menu_selector(CaptureAssetBrowserPopup::onToggleCategory)
                );
                // Use tag = catIdx + 10000 to distinguish from group toggles
                toggler->setTag(catIdx + 10000);
                toggler->toggle(cat.visible);
                toggler->setPosition({C::PREVIEW_X - 6.f, rowH * 0.5f});
                catMenu->addChild(toggler);
            }
            rowNode->addChild(catMenu, 1);

            // Category label
            int totalCount = 0;
            for (int gi : cat.groupIndices) totalCount += m_groups[gi].count;

            std::string headerText = cat.name + "  (" + std::to_string(totalCount) + ")";
            auto label = CCLabelBMFont::create(headerText.c_str(), "bigFont.fnt");
            label->setScale(C::LABEL_SCALE_HEADER);
            label->setAnchorPoint({0.f, 0.5f});
            label->setPosition({C::LABEL_X - 12.f, rowH * 0.5f});
            label->setColor(cat.visible ? ccColor3B{255, 226, 120} : ccColor3B{120, 110, 80});
            rowNode->addChild(label, 2);

            m_scrollView->m_contentLayer->addChild(rowNode);
            row++;
        }

        // ── Group rows under this category ───────────────────────
        for (int gi : cat.groupIndices) {
            auto& group = m_groups[gi];
            y = totalH - rowH - row * rowH;

            auto rowNode = CCNode::create();
            rowNode->setContentSize({listW, rowH});
            rowNode->setPosition({0.f, y});
            rowNode->setAnchorPoint({0.f, 0.f});

            // Alternating row tint
            if (row % 2 == 0) {
                auto bg = CCLayerColor::create({255, 255, 255, C::ALT_ROW_ALPHA});
                bg->setContentSize({listW, rowH});
                bg->setAnchorPoint({0.f, 0.f});
                bg->ignoreAnchorPointForPosition(false);
                bg->setPosition({0.f, 0.f});
                rowNode->addChild(bg, -1);
            }

            // ── Sprite preview ───────────────────────────────────
            bool spriteAdded = false;
            if (group.representativeFrame) {
                auto* preview = CCSprite::createWithSpriteFrame(group.representativeFrame);
                if (preview) {
                    auto cs = preview->getContentSize();
                    float maxDim = std::max(cs.width, cs.height);
                    if (maxDim > 0.f) {
                        preview->setScale(C::PREVIEW_SIZE / maxDim);
                    }
                    preview->setPosition({C::PREVIEW_X, rowH * 0.5f});
                    preview->setAnchorPoint({0.5f, 0.5f});
                    rowNode->addChild(preview, 1);
                    spriteAdded = true;
                }
            }

            if (!spriteAdded) {
                // Fallback: small colored square
                auto fallback = CCLayerColor::create({180, 180, 180, 200});
                fallback->setContentSize({C::PREVIEW_SIZE * 0.6f, C::PREVIEW_SIZE * 0.6f});
                fallback->ignoreAnchorPointForPosition(false);
                fallback->setAnchorPoint({0.5f, 0.5f});
                fallback->setPosition({C::PREVIEW_X, rowH * 0.5f});
                rowNode->addChild(fallback, 1);
            }

            // ── Object ID label ──────────────────────────────────
            std::string idLabel = "ID " + std::to_string(group.objectID);
            auto label = CCLabelBMFont::create(idLabel.c_str(), "bigFont.fnt");
            label->setScale(C::LABEL_SCALE_ROW);
            label->setAnchorPoint({0.f, 0.5f});
            label->setPosition({C::LABEL_X, rowH * 0.5f});
            label->setColor(group.visible ? ccColor3B{255, 255, 255} : ccColor3B{130, 130, 130});
            rowNode->addChild(label, 2);

            // ── Count badge ──────────────────────────────────────
            std::string countStr = fmt::format("x{}", group.count);
            auto countLabel = CCLabelBMFont::create(countStr.c_str(), "bigFont.fnt");
            countLabel->setScale(C::COUNT_SCALE);
            countLabel->setAnchorPoint({1.f, 0.5f});
            countLabel->setPosition({listW + C::COUNT_X_OFF, rowH * 0.5f});
            countLabel->setColor({180, 220, 255});
            rowNode->addChild(countLabel, 2);

            // ── Visibility toggle ────────────────────────────────
            auto rowMenu = ClippedMenu::create(m_scrollView);
            rowMenu->setContentSize({listW, rowH});
            rowMenu->setPosition({0.f, 0.f});
            rowMenu->setAnchorPoint({0.f, 0.f});

            auto onSpr  = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
            auto offSpr = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
            if (onSpr && offSpr) {
                onSpr->setScale(C::CHECK_SCALE);
                offSpr->setScale(C::CHECK_SCALE);

                auto toggler = CCMenuItemToggler::create(
                    offSpr, onSpr,
                    this, menu_selector(CaptureAssetBrowserPopup::onToggleGroup)
                );
                toggler->setTag(gi);
                toggler->toggle(group.visible);
                toggler->setPosition({listW + C::CHECK_X_OFF, rowH * 0.5f});
                rowMenu->addChild(toggler);

                m_groupTogglers[gi] = toggler;
            }

            rowNode->addChild(rowMenu, 3);
            m_scrollView->m_contentLayer->addChild(rowNode);
            row++;
        }
    }

    m_scrollView->scrollToTop();
    m_listRoot->addChild(m_scrollView, 2);
}

// ─── render mini-preview ──────────────────────────────────────────

void CaptureAssetBrowserPopup::updateMiniPreview() {
    namespace C = paimon::capture::assets;
    if (!m_miniPreview) return;

    auto* pl = PlayLayer::get();
    if (!pl) return;

    auto* director = CCDirector::sharedDirector();
    auto winSize = director->getWinSize();

    const int rtW = C::RT_WIDTH;
    const int rtH = C::RT_HEIGHT;

    auto* rt = CCRenderTexture::create(rtW, rtH, kCCTexture2DPixelFormat_RGBA8888);
    if (!rt) return;

    // Hide this popup + overlays temporarily
    bool selfWasVisible = this->isVisible();
    this->setVisible(false);

    auto* scene = director->getRunningScene();
    std::vector<std::pair<CCNode*, bool>> hiddenOverlays;
    if (scene) {
        for (auto* child : CCArrayExt<CCNode*>(scene->getChildren())) {
            if (!child || !child->isVisible() || child == pl) continue;
            if (typeinfo_cast<FLAlertLayer*>(child)) {
                hiddenOverlays.push_back({child, true});
                child->setVisible(false);
            } else {
                std::string cls = typeid(*child).name();
                if (cls.find("PauseLayer") != std::string::npos) {
                    hiddenOverlays.push_back({child, true});
                    child->setVisible(false);
                }
            }
        }
    }

    rt->begin();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    kmGLPushMatrix();
    float sx = static_cast<float>(rtW) / winSize.width;
    float sy = static_cast<float>(rtH) / winSize.height;
    kmMat4 scaleMat;
    std::memset(&scaleMat, 0, sizeof(scaleMat));
    scaleMat.mat[0]  = sx;
    scaleMat.mat[5]  = sy;
    scaleMat.mat[10] = 1.0f;
    scaleMat.mat[15] = 1.0f;
    kmGLMultMatrix(&scaleMat);
    pl->visit();
    kmGLPopMatrix();
    rt->end();

    for (auto& [node, _] : hiddenOverlays) node->setVisible(true);
    this->setVisible(selfWasVisible);

    auto* rtSprite = rt->getSprite();
    if (rtSprite) {
        auto* rtTex = rtSprite->getTexture();
        if (rtTex) {
            m_miniPreview->setTexture(rtTex);
            m_miniPreview->setTextureRect(CCRect(0, 0, rtW, rtH));
            m_miniPreview->setFlipY(true);
            float scaleToFit = std::min(C::MINI_PREVIEW_W / rtW, C::MINI_PREVIEW_H / rtH);
            m_miniPreview->setScale(scaleToFit);
        }
    }
}

// ─── visibility helpers ───────────────────────────────────────────

void CaptureAssetBrowserPopup::setGroupVisible(int groupIdx, bool visible) {
    if (groupIdx < 0 || groupIdx >= static_cast<int>(m_groups.size())) return;
    auto& group = m_groups[groupIdx];
    group.visible = visible;

    auto* pl = PlayLayer::get();
    if (!pl || !pl->m_objects) return;

    int oid = group.objectID;
    for (auto* obj : CCArrayExt<GameObject*>(pl->m_objects)) {
        if (obj && obj->m_objectID == oid) {
            obj->setVisible(visible);
        }
    }
}

void CaptureAssetBrowserPopup::setCategoryVisible(int catIdx, bool visible) {
    if (catIdx < 0 || catIdx >= static_cast<int>(m_categories.size())) return;
    auto& cat = m_categories[catIdx];
    cat.visible = visible;

    for (int gi : cat.groupIndices) {
        setGroupVisible(gi, visible);
        // Update toggler visual
        if (gi < static_cast<int>(m_groupTogglers.size()) && m_groupTogglers[gi]) {
            if (m_groupTogglers[gi]->isToggled() != visible) {
                m_groupTogglers[gi]->toggle(visible);
            }
        }
    }
}

// ─── callbacks ────────────────────────────────────────────────────

void CaptureAssetBrowserPopup::onToggleGroup(CCObject* sender) {
    auto* toggler = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggler) return;

    int gi = toggler->getTag();
    if (gi < 0 || gi >= static_cast<int>(m_groups.size())) return;

    bool newVisible = toggler->isToggled();
    setGroupVisible(gi, newVisible);

    log::info("[AssetBrowser] Object ID {} -> {}", m_groups[gi].objectID,
        newVisible ? "visible" : "hidden");

    updateMiniPreview();
}

void CaptureAssetBrowserPopup::onToggleCategory(CCObject* sender) {
    auto* toggler = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggler) return;

    int catIdx = toggler->getTag() - 10000;
    if (catIdx < 0 || catIdx >= static_cast<int>(m_categories.size())) return;

    bool newVisible = toggler->isToggled();
    setCategoryVisible(catIdx, newVisible);

    // Rebuild to update all visual states
    buildList();
    updateMiniPreview();
}

void CaptureAssetBrowserPopup::onDoneBtn(CCObject* sender) {
    // Trigger recapture on the parent preview popup
    if (auto popup = m_previewPopup.lock()) {
        popup->liveRecapture(true);
    }
    this->onClose(nullptr);
}

void CaptureAssetBrowserPopup::onRestoreAllBtn(CCObject* sender) {
    // Restore visibility by iterating m_objects for each modified objectID
    auto* pl = PlayLayer::get();
    if (pl && pl->m_objects) {
        for (auto* obj : CCArrayExt<GameObject*>(pl->m_objects)) {
            if (!obj) continue;
            // Check if this objectID belongs to any of our groups
            for (auto& group : m_groups) {
                if (obj->m_objectID == group.objectID) {
                    obj->setVisible(s_originallyHidden.count(obj) == 0);
                    break;
                }
            }
        }
    }

    for (auto& group : m_groups) {
        group.visible = group.originalVisible;
    }

    for (auto& cat : m_categories) {
        cat.visible = true;
    }

    buildList();
    updateMiniPreview();

    PaimonNotify::create(
        Localization::get().getString("assets.restored").c_str(),
        NotificationIcon::Success
    )->show();
}

void CaptureAssetBrowserPopup::onShowAllBtn(CCObject* sender) {
    for (int gi = 0; gi < static_cast<int>(m_groups.size()); ++gi) {
        setGroupVisible(gi, true);
    }
    for (auto& cat : m_categories) cat.visible = true;

    buildList();
    updateMiniPreview();
}

void CaptureAssetBrowserPopup::onHideAllBtn(CCObject* sender) {
    for (int gi = 0; gi < static_cast<int>(m_groups.size()); ++gi) {
        setGroupVisible(gi, false);
    }
    for (auto& cat : m_categories) cat.visible = false;

    buildList();
    updateMiniPreview();
}
