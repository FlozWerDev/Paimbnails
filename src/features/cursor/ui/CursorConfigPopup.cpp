#include "CursorConfigPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../services/CursorManager.hpp"
#include "../../../utils/PaimonNotification.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/ImageLoadHelper.hpp"
#include "../../../utils/InfoButton.hpp"
#include <Geode/binding/ButtonSprite.hpp>

using namespace geode::prelude;
using namespace cocos2d;

namespace {
bool scrollLayerWithWheel(ScrollLayer* scrollLayer, float x, float y) {
#if !defined(GEODE_IS_WINDOWS) && !defined(GEODE_IS_MACOS)
    return false;
#else
    if (!scrollLayer) return false;

    CCPoint mousePos = geode::cocos::getMousePos();
    CCRect scrollRect = scrollLayer->boundingBox();
    scrollRect.origin = scrollLayer->getParent()->convertToWorldSpace(scrollRect.origin);
    if (!scrollRect.containsPoint(mousePos)) return false;

    float scrollAmount = y;
    if (std::abs(scrollAmount) < 0.001f) {
        scrollAmount = -x;
    }

    auto* contentLayer = scrollLayer->m_contentLayer;
    if (!contentLayer) return false;

    float newY = contentLayer->getPositionY() - scrollAmount * 6.f;
    float minY = scrollLayer->getContentSize().height - contentLayer->getContentSize().height;
    float maxY = 0.f;
    if (minY > maxY) minY = maxY;

    contentLayer->setPositionY(std::max(minY, std::min(maxY, newY)));
    return true;
#endif
}
}

// ════════════════════════════════════════════════════════════
// create
// ════════════════════════════════════════════════════════════

CursorConfigPopup* CursorConfigPopup::create() {
    auto ret = new CursorConfigPopup();
    if (ret && ret->init()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

// ════════════════════════════════════════════════════════════
// init
// ════════════════════════════════════════════════════════════

bool CursorConfigPopup::init() {
    if (!Popup::init(420.f, 290.f)) return false;

    this->setTitle("Custom Cursor");
    this->setMouseEnabled(true);

    auto content = m_mainLayer->getContentSize();

    // cleanup invalid images on open
    int cleaned = CursorManager::get().cleanupInvalidImages();
    if (cleaned > 0) {
        log::info("[CursorConfig] Cleaned up {} invalid image files from gallery", cleaned);
    }

    // ── tab layers ──
    m_galleryTab = CCNode::create();
    m_galleryTab->setID("cursor-gallery-tab"_spr);
    m_galleryTab->setContentSize(content);
    m_mainLayer->addChild(m_galleryTab, 5);

    m_settingsTab = CCNode::create();
    m_settingsTab->setID("cursor-settings-tab"_spr);
    m_settingsTab->setContentSize(content);
    m_settingsTab->setVisible(false);
    m_mainLayer->addChild(m_settingsTab, 5);

    createTabButtons();
    buildGalleryTab();
    buildSettingsTab();

    paimon::markDynamicPopup(this);
    return true;
}

void CursorConfigPopup::onExit() {
    this->unschedule(schedule_selector(CursorConfigPopup::checkScrollPosition));
    if (m_scrollArrow) {
        m_scrollArrow->stopAllActions();
    }
    Popup::onExit();
}

void CursorConfigPopup::scrollWheel(float x, float y) {
    if (m_currentTab == 1 && scrollLayerWithWheel(m_scrollLayer, x, y)) return;
}

// ════════════════════════════════════════════════════════════
// tabs
// ════════════════════════════════════════════════════════════

void CursorConfigPopup::createTabButtons() {
    auto content = m_mainLayer->getContentSize();
    float topY = content.height - 38.f;
    float cx = content.width / 2.f;

    auto menu = CCMenu::create();
    menu->setID("cursor-tab-buttons-menu"_spr);
    menu->setPosition({0, 0});
    m_mainLayer->addChild(menu, 10);

    auto spr1 = ButtonSprite::create("Gallery");
    spr1->setScale(0.5f);
    auto tab1 = CCMenuItemSpriteExtra::create(spr1, this, menu_selector(CursorConfigPopup::onTabSwitch));
    tab1->setTag(0);
    tab1->setPosition({cx - 60.f, topY});
    menu->addChild(tab1);
    m_tabs.push_back(tab1);

    auto spr2 = ButtonSprite::create("Settings");
    spr2->setScale(0.5f);
    auto tab2 = CCMenuItemSpriteExtra::create(spr2, this, menu_selector(CursorConfigPopup::onTabSwitch));
    tab2->setTag(1);
    tab2->setPosition({cx + 60.f, topY});
    menu->addChild(tab2);
    m_tabs.push_back(tab2);

    onTabSwitch(tab1);
}

void CursorConfigPopup::onTabSwitch(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    m_currentTab = btn->getTag();

    m_galleryTab->setVisible(m_currentTab == 0);
    m_settingsTab->setVisible(m_currentTab == 1);

    for (auto* tab : m_tabs) {
        auto spr = typeinfo_cast<ButtonSprite*>(tab->getNormalImage());
        if (!spr) continue;
        if (tab->getTag() == m_currentTab) {
            spr->setColor({0, 255, 0});
            spr->setOpacity(255);
        } else {
            spr->setColor({255, 255, 255});
            spr->setOpacity(150);
        }
    }
}

// ════════════════════════════════════════════════════════════
// gallery tab
// ════════════════════════════════════════════════════════════

void CursorConfigPopup::buildGalleryTab() {
    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    // ── slot previews ──
    float slotSize = 60.f;
    float slotY = content.height - 90.f;
    float slotSpacing = 80.f;

    // Idle slot (left)
    m_idleSlotBg = CCLayerColor::create(ccc4(0, 180, 0, 160), slotSize, slotSize);
    m_idleSlotBg->setPosition({cx - slotSpacing - slotSize / 2.f, slotY - slotSize / 2.f});
    m_galleryTab->addChild(m_idleSlotBg);

    m_idleLabel = CCLabelBMFont::create("Idle", "bigFont.fnt");
    m_idleLabel->setScale(0.25f);
    m_idleLabel->setPosition({cx - slotSpacing, slotY - slotSize / 2.f - 10.f});
    m_galleryTab->addChild(m_idleLabel);

    // Move slot (right)
    m_moveSlotBg = CCLayerColor::create(ccc4(80, 80, 80, 120), slotSize, slotSize);
    m_moveSlotBg->setPosition({cx + slotSpacing - slotSize / 2.f, slotY - slotSize / 2.f});
    m_galleryTab->addChild(m_moveSlotBg);

    m_moveLabel = CCLabelBMFont::create("Move", "bigFont.fnt");
    m_moveLabel->setScale(0.25f);
    m_moveLabel->setPosition({cx + slotSpacing, slotY - slotSize / 2.f - 10.f});
    m_galleryTab->addChild(m_moveLabel);

    // slot tap buttons
    m_galleryMenu = CCMenu::create();
    m_galleryMenu->setID("cursor-gallery-menu"_spr);
    m_galleryMenu->setPosition({0, 0});
    m_galleryTab->addChild(m_galleryMenu, 10);

    auto idleArea = CCSprite::create();
    idleArea->setContentSize({slotSize, slotSize});
    idleArea->setOpacity(0);
    auto idleBtn = CCMenuItemSpriteExtra::create(idleArea, this, menu_selector(CursorConfigPopup::onActivateIdleSlot));
    idleBtn->setContentSize({slotSize, slotSize});
    idleBtn->setPosition({cx - slotSpacing, slotY});
    m_galleryMenu->addChild(idleBtn);

    auto moveArea = CCSprite::create();
    moveArea->setContentSize({slotSize, slotSize});
    moveArea->setOpacity(0);
    auto moveBtn = CCMenuItemSpriteExtra::create(moveArea, this, menu_selector(CursorConfigPopup::onActivateMoveSlot));
    moveBtn->setContentSize({slotSize, slotSize});
    moveBtn->setPosition({cx + slotSpacing, slotY});
    m_galleryMenu->addChild(moveBtn);

    // gallery container for thumbnails
    m_galleryContainer = CCNode::create();
    m_galleryContainer->setID("cursor-gallery-container"_spr);
    m_galleryContainer->setPosition({0, 0});
    m_galleryTab->addChild(m_galleryContainer);

    // bottom buttons
    auto addSpr = ButtonSprite::create("+ Add", "goldFont.fnt", "GJ_button_01.png", 0.7f);
    addSpr->setScale(0.55f);
    auto addBtn = CCMenuItemSpriteExtra::create(addSpr, this, menu_selector(CursorConfigPopup::onAddImage));
    addBtn->setPosition({cx - 60.f, 25.f});
    m_galleryMenu->addChild(addBtn);

    auto delAllSpr = ButtonSprite::create("Delete All", "goldFont.fnt", "GJ_button_06.png", 0.7f);
    delAllSpr->setScale(0.55f);
    auto delAllBtn = CCMenuItemSpriteExtra::create(delAllSpr, this, menu_selector(CursorConfigPopup::onDeleteAllImages));
    delAllBtn->setPosition({cx + 60.f, 25.f});
    m_galleryMenu->addChild(delAllBtn);

    refreshGallery();
}

void CursorConfigPopup::refreshGallery() {
    if (m_galleryContainer) {
        m_galleryContainer->removeAllChildren();
    }

    // remove old gallery buttons from menu (tag >= 100)
    auto toRemove = std::vector<CCNode*>();
    if (m_galleryMenu && m_galleryMenu->getChildren()) {
        for (auto* child : CCArrayExt<CCNode*>(m_galleryMenu->getChildren())) {
            if (child->getTag() >= 100) toRemove.push_back(child);
        }
    }
    for (auto* n : toRemove) n->removeFromParent();

    auto& cm = CursorManager::get();
    auto images = cm.getGalleryImages();
    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;

    float startX = 35.f;
    float startY = content.height - 155.f;
    float cellSize = 44.f;
    float padding = 5.f;
    int cols = static_cast<int>((content.width - 30.f) / (cellSize + padding));
    if (cols < 1) cols = 1;

    auto& cfg = cm.config();

    for (int i = 0; i < (int)images.size(); i++) {
        float col = static_cast<float>(i % cols);
        float row = static_cast<float>(i / cols);
        float x = startX + col * (cellSize + padding) + cellSize / 2.f;
        float y = startY - row * (cellSize + padding);

        // determine selection color
        bool isIdle = (images[i] == cfg.idleImage);
        bool isMove = (images[i] == cfg.moveImage);
        ccColor3B bgColor = ccc3(50, 50, 50);
        GLubyte bgOpacity = 100;
        if (isIdle && isMove) {
            bgColor = ccc3(255, 200, 0); bgOpacity = 180; // gold: both slots
        } else if (isIdle) {
            bgColor = ccc3(0, 200, 0); bgOpacity = 180;   // green: idle
        } else if (isMove) {
            bgColor = ccc3(0, 120, 255); bgOpacity = 180;  // blue: move
        }

        auto bg = paimon::SpriteHelper::createColorPanel(
            cellSize, cellSize, bgColor, bgOpacity);
        bg->setPosition({x - cellSize / 2, y - cellSize / 2});
        m_galleryContainer->addChild(bg);

        // thumbnail
        auto tex = cm.loadGalleryThumb(images[i]);
        if (tex) {
            auto thumbSpr = CCSprite::createWithTexture(tex);
            if (thumbSpr) {
                float maxDim = std::max(thumbSpr->getContentSize().width, thumbSpr->getContentSize().height);
                if (maxDim > 0) thumbSpr->setScale((cellSize - 6.f) / maxDim);
                thumbSpr->setPosition({x, y});
                m_galleryContainer->addChild(thumbSpr, 1);

                // GIF badge for animated images
                auto imgPath = cm.galleryDir() / images[i];
                if (ImageLoadHelper::isAnimatedImage(imgPath)) {
                    auto* gifLabel = CCLabelBMFont::create("GIF", "bigFont.fnt");
                    if (gifLabel) {
                        gifLabel->setScale(0.25f);
                        gifLabel->setOpacity(200);
                        gifLabel->setColor({255, 100, 100});
                        gifLabel->setPosition({x + cellSize / 2.f - 8.f, y - cellSize / 2.f + 6.f});
                        m_galleryContainer->addChild(gifLabel, 2);
                    }
                }
            }
            tex->release();
        }

        // select button (invisible overlay)
        auto selectArea = CCSprite::create();
        selectArea->setContentSize({cellSize, cellSize});
        selectArea->setOpacity(0);
        auto selectBtn = CCMenuItemSpriteExtra::create(selectArea, this, menu_selector(CursorConfigPopup::onSelectImage));
        selectBtn->setContentSize({cellSize, cellSize});
        selectBtn->setPosition({x, y});
        selectBtn->setTag(100 + i);
        auto* nameStr = CCString::create(images[i]);
        selectBtn->setUserObject(nameStr);
        m_galleryMenu->addChild(selectBtn);

        // delete btn (small X)
        auto xSpr = CCSprite::createWithSpriteFrameName("GJ_deleteIcon_001.png");
        if (xSpr) {
            xSpr->setScale(0.3f);
            auto xBtn = CCMenuItemSpriteExtra::create(xSpr, this, menu_selector(CursorConfigPopup::onDeleteImage));
            xBtn->setPosition({x + cellSize / 2.f - 4.f, y + cellSize / 2.f - 4.f});
            xBtn->setTag(500 + i);
            auto* nameStr2 = CCString::create(images[i]);
            xBtn->setUserObject(nameStr2);
            m_galleryMenu->addChild(xBtn);
        }
    }

    updateSlotPreviews();
}

void CursorConfigPopup::updateSlotPreviews() {
    auto& cm = CursorManager::get();
    auto& cfg = cm.config();
    auto content = m_mainLayer->getContentSize();
    float cx = content.width / 2.f;
    float slotSpacing = 80.f;
    float slotY = content.height - 90.f;
    float maxThumb = 50.f;

    // idle preview
    if (m_idlePreview) {
        m_idlePreview->removeFromParent();
        m_idlePreview = nullptr;
    }
    if (!cfg.idleImage.empty()) {
        auto tex = cm.loadGalleryThumb(cfg.idleImage);
        if (tex) {
            m_idlePreview = CCSprite::createWithTexture(tex);
            if (m_idlePreview) {
                float maxDim = std::max(m_idlePreview->getContentSize().width, m_idlePreview->getContentSize().height);
                if (maxDim > 0) m_idlePreview->setScale(maxThumb / maxDim);
                m_idlePreview->setPosition({cx - slotSpacing, slotY});
                m_galleryTab->addChild(m_idlePreview, 5);
            }
            tex->release();
        }
        m_idleLabel->setString("Idle");
    } else {
        m_idleLabel->setString("Idle (None)");
    }

    // move preview
    if (m_movePreview) {
        m_movePreview->removeFromParent();
        m_movePreview = nullptr;
    }
    if (!cfg.moveImage.empty()) {
        auto tex = cm.loadGalleryThumb(cfg.moveImage);
        if (tex) {
            m_movePreview = CCSprite::createWithTexture(tex);
            if (m_movePreview) {
                float maxDim = std::max(m_movePreview->getContentSize().width, m_movePreview->getContentSize().height);
                if (maxDim > 0) m_movePreview->setScale(maxThumb / maxDim);
                m_movePreview->setPosition({cx + slotSpacing, slotY});
                m_galleryTab->addChild(m_movePreview, 5);
            }
            tex->release();
        }
        m_moveLabel->setString("Move");
    } else {
        m_moveLabel->setString("Move (None)");
    }

    // highlight active slot
    if (m_activeSlot == ActiveSlot::Idle) {
        m_idleSlotBg->setColor(ccc3(0, 180, 0));
        m_idleSlotBg->setOpacity(160);
        m_moveSlotBg->setColor(ccc3(80, 80, 80));
        m_moveSlotBg->setOpacity(120);
    } else {
        m_idleSlotBg->setColor(ccc3(80, 80, 80));
        m_idleSlotBg->setOpacity(120);
        m_moveSlotBg->setColor(ccc3(0, 120, 255));
        m_moveSlotBg->setOpacity(160);
    }
}

void CursorConfigPopup::onActivateIdleSlot(CCObject*) {
    m_activeSlot = ActiveSlot::Idle;
    updateSlotPreviews();
}

void CursorConfigPopup::onActivateMoveSlot(CCObject*) {
    m_activeSlot = ActiveSlot::Move;
    updateSlotPreviews();
}

void CursorConfigPopup::onSelectImage(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto nameObj = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!nameObj) return;

    std::string filename = nameObj->getCString();

    if (m_activeSlot == ActiveSlot::Idle) {
        CursorManager::get().setIdleImage(filename);
        PaimonNotify::create("Idle cursor image set!", NotificationIcon::Success)->show();
    } else {
        CursorManager::get().setMoveImage(filename);
        PaimonNotify::create("Move cursor image set!", NotificationIcon::Success)->show();
    }
    refreshGallery();
}

void CursorConfigPopup::onDeleteImage(CCObject* sender) {
    auto btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto nameObj = typeinfo_cast<CCString*>(btn->getUserObject());
    if (!nameObj) return;

    std::string filename = nameObj->getCString();

    WeakRef<CursorConfigPopup> self = this;
    geode::createQuickPopup(
        "Delete Cursor Image",
        "Are you sure you want to <cr>delete</c> this image?\n<cy>" + filename + "</c>",
        "Cancel", "Delete",
        [self, filename](auto*, bool confirmed) {
            if (!confirmed) return;
            auto popup = self.lock();
            if (!popup || !popup->getParent()) return;
            CursorManager::get().removeFromGallery(filename);
            PaimonNotify::create("Image removed", NotificationIcon::Info)->show();
            static_cast<CursorConfigPopup*>(popup.data())->refreshGallery();
        }
    );
}

void CursorConfigPopup::onDeleteAllImages(CCObject*) {
    auto images = CursorManager::get().getGalleryImages();
    if (images.empty()) {
        PaimonNotify::create("Gallery is already empty", NotificationIcon::Info)->show();
        return;
    }

    std::string msg = fmt::format(
        "Are you sure you want to <cr>delete ALL</c> {} cursor images?\nThis cannot be undone!",
        images.size()
    );

    WeakRef<CursorConfigPopup> self = this;
    geode::createQuickPopup(
        "Delete All Cursor Images",
        msg,
        "Cancel", "Delete All",
        [self](auto*, bool confirmed) {
            if (!confirmed) return;
            auto popup = self.lock();
            if (!popup || !popup->getParent()) return;

            int cleaned = CursorManager::get().cleanupInvalidImages();
            CursorManager::get().removeAllFromGallery();

            std::string note = "All cursor images deleted!";
            if (cleaned > 0) {
                note += fmt::format(" ({} corrupted files removed)", cleaned);
            }
            PaimonNotify::create(note, NotificationIcon::Success)->show();
            static_cast<CursorConfigPopup*>(popup.data())->refreshGallery();
        }
    );
}

void CursorConfigPopup::onAddImage(CCObject*) {
    WeakRef<CursorConfigPopup> self = this;
    pt::pickImage([self](geode::Result<std::optional<std::filesystem::path>> result) {
        auto popup = self.lock();
        if (!popup) return;
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;

        auto filename = CursorManager::get().addToGallery(*pathOpt);
        if (!filename.empty()) {
            PaimonNotify::create("Image added to gallery!", NotificationIcon::Success)->show();
            auto& cfg = CursorManager::get().config();
            // auto-assign to active slot if empty
            if (static_cast<CursorConfigPopup*>(popup.data())->m_activeSlot == ActiveSlot::Idle) {
                if (cfg.idleImage.empty()) {
                    CursorManager::get().setIdleImage(filename);
                }
            } else {
                if (cfg.moveImage.empty()) {
                    CursorManager::get().setMoveImage(filename);
                }
            }
            static_cast<CursorConfigPopup*>(popup.data())->refreshGallery();
        } else {
            PaimonNotify::create("Failed to add image", NotificationIcon::Error)->show();
        }
    });
}

// ════════════════════════════════════════════════════════════
// settings tab
// ════════════════════════════════════════════════════════════

static float readSlider(Slider* s, float minV, float maxV) {
    float v = s->getThumb()->getValue();
    return minV + v * (maxV - minV);
}

void CursorConfigPopup::buildSettingsTab() {
    auto content = m_mainLayer->getContentSize();
    float scrollW = content.width - 16.f;
    float scrollH = content.height - 52.f;
    float totalH = 520.f;

    m_scrollLayer = ScrollLayer::create({scrollW, scrollH});
    m_scrollLayer->setPosition({8.f, 8.f});
    m_settingsTab->addChild(m_scrollLayer, 5);

    CCNode* sc = m_scrollLayer->m_contentLayer;
    sc->setContentSize({scrollW, totalH});

    auto scrollContent = CCLayer::create();
    scrollContent->setContentSize({scrollW, totalH});
    sc->addChild(scrollContent);
    sc = scrollContent;

    auto navMenu = CCMenu::create();
    navMenu->setPosition({0, 0});
    scrollContent->addChild(navMenu, 10);

    float cx = scrollW / 2.f;
    float y = totalH - 8.f;

    auto& cfg = CursorManager::get().config();

    // ── helpers ──
    auto addTitle = [&](char const* text, char const* info = nullptr) {
        auto label = CCLabelBMFont::create(text, "goldFont.fnt");
        label->setScale(0.4f);
        label->setPosition({cx, y});
        sc->addChild(label);

        if (info) {
            auto btn = PaimonInfo::createInfoBtn(text, info, this, 0.45f);
            if (btn) {
                float halfW = label->getContentSize().width * 0.4f / 2.f;
                btn->setPosition({cx + halfW + 10.f, y});
                navMenu->addChild(btn);
            }
        }
    };

    auto addSlider = [&](Slider*& slider, CCLabelBMFont*& label,
                         float val, float minV, float maxV,
                         SEL_MenuHandler cb, char const* fmt_str) {
        float norm = (maxV > minV) ? (val - minV) / (maxV - minV) : 0.f;
        slider = Slider::create(this, cb, 0.65f);
        slider->setPosition({cx, y});
        slider->setValue(norm);
        sc->addChild(slider);

        label = CCLabelBMFont::create(fmt::format(fmt::runtime(fmt_str), val).c_str(), "bigFont.fnt");
        label->setScale(0.35f);
        label->setPosition({cx + 95.f, y});
        sc->addChild(label);
    };

    auto addToggle = [&](char const* text, CCMenuItemToggler*& toggle,
                         bool value, SEL_MenuHandler cb, char const* info = nullptr) {
        auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(0.35f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({cx - 85.f, y});
        sc->addChild(lbl);

        if (info) {
            auto iBtn = PaimonInfo::createInfoBtn(text, info, this, 0.35f);
            if (iBtn) {
                float lblW = lbl->getContentSize().width * 0.35f;
                iBtn->setPosition({cx - 85.f + lblW + 7.f, y});
                navMenu->addChild(iBtn);
            }
        }

        toggle = CCMenuItemToggler::createWithStandardSprites(this, cb, 0.35f);
        toggle->setPosition({cx + 85.f, y});
        toggle->toggle(value);
        navMenu->addChild(toggle);
    };

    // ── General ──
    addTitle("General",
        "<cy>Enable Cursor</c>: turns the custom cursor <cg>ON</c> or <cr>OFF</c>.\n"
        "When enabled, the OS cursor is hidden and replaced with your custom image.\n"
        "<cy>Scale</c>: size of the cursor sprite (0.10 = tiny, 3.0 = huge).\n"
        "<cy>Opacity</c>: transparency of the cursor (0 = invisible, 255 = fully opaque).");
    y -= 18.f;

    addToggle("Enable Cursor", m_enableToggle, cfg.enabled,
        menu_selector(CursorConfigPopup::onEnableToggled),
        "Turns the custom cursor <cg>ON</c> or <cr>OFF</c>.\nThe OS cursor will be hidden when active.");
    y -= 22.f;

    addSlider(m_scaleSlider, m_scaleLabel, cfg.scale, CURSOR_SCALE_MIN, CURSOR_SCALE_MAX,
        menu_selector(CursorConfigPopup::onScaleChanged), "{:.2f}");
    y -= 18.f;

    addSlider(m_opacitySlider, m_opacityLabel, static_cast<float>(cfg.opacity), 0.f, 255.f,
        menu_selector(CursorConfigPopup::onOpacityChanged), "{:.0f}");
    y -= 24.f;

    // ── Trail ──
    addTitle("Trail Effect",
        "Leaves a glowing trail behind the cursor as it moves.\n"
        "<cy>Enable Trail</c>: shows/hides the trail.\n"
        "<cy>Presets</c>: 10 built-in trail styles with different colors and behavior.\n"
        "Use <cg>Edit Trail</c> to switch to custom mode and tweak values manually.");
    y -= 18.f;

    addToggle("Enable Trail", m_trailToggle, cfg.trailEnabled,
        menu_selector(CursorConfigPopup::onTrailToggled),
        "Shows a <cy>CCMotionStreak</c> trail behind the cursor.\nRequires cursor to be enabled.");
    y -= 22.f;

    // preset picker row
    {
        auto lbl = CCLabelBMFont::create("Trail Preset", "bigFont.fnt");
        lbl->setScale(0.35f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setPosition({cx - 85.f, y});
        sc->addChild(lbl);

        // prev arrow
        auto prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        if (prevSpr) {
            prevSpr->setScale(0.35f);
            auto prevBtn = CCMenuItemSpriteExtra::create(prevSpr, this, menu_selector(CursorConfigPopup::onPresetPrev));
            prevBtn->setPosition({cx + 30.f, y});
            navMenu->addChild(prevBtn);
        }

        // preset name label
        m_presetLabel = CCLabelBMFont::create("", "bigFont.fnt");
        m_presetLabel->setScale(0.22f);
        m_presetLabel->setPosition({cx + 60.f, y});
        sc->addChild(m_presetLabel);
        updatePresetLabel();

        // next arrow
        auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png");
        if (nextSpr) {
            nextSpr->setFlipX(true);
            nextSpr->setScale(0.35f);
            auto nextBtn = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(CursorConfigPopup::onPresetNext));
            nextBtn->setPosition({cx + 90.f, y});
            navMenu->addChild(nextBtn);
        }
    }
    y -= 22.f;

    // edit trail (custom mode) button
    {
        auto editSpr = ButtonSprite::create("Edit Trail", "bigFont.fnt", "GJ_button_04.png", 0.6f);
        editSpr->setScale(0.45f);
        auto editBtn = CCMenuItemSpriteExtra::create(editSpr, this, menu_selector(CursorConfigPopup::onEditTrail));
        editBtn->setPosition({cx, y});
        navMenu->addChild(editBtn);
    }
    y -= 26.f;

    // ── Visibility ──
    addTitle("Visibility",
        "The custom cursor appears on <cg>all screens</c> automatically.\n"
        "It hides automatically during gameplay if you disable the native cursor.");
    y -= 16.f;

    m_scrollLayer->moveToTop();

    // scroll arrow indicator
    auto scrollArrow = CCSprite::createWithSpriteFrameName("GJ_arrow_03_001.png");
    if (scrollArrow) {
        scrollArrow->setRotation(-90.f);
        scrollArrow->setScale(0.3f);
        scrollArrow->setOpacity(150);
        scrollArrow->setPosition({content.width / 2.f, 16.f});
        scrollArrow->setID("cursor-scroll-arrow"_spr);
        m_settingsTab->addChild(scrollArrow, 20);

        auto bounce = CCRepeatForever::create(CCSequence::create(
            CCMoveBy::create(0.5f, {0, 3.f}),
            CCMoveBy::create(0.5f, {0, -3.f}), nullptr));
        scrollArrow->runAction(bounce);
        m_scrollArrow = scrollArrow;
        this->unschedule(schedule_selector(CursorConfigPopup::checkScrollPosition));
        this->schedule(schedule_selector(CursorConfigPopup::checkScrollPosition), 0.2f);
    }
}

void CursorConfigPopup::checkScrollPosition(float dt) {
    if (!m_scrollArrow || !m_scrollLayer) return;
    float totalH = m_scrollLayer->m_contentLayer->getContentSize().height;
    float viewH = m_scrollLayer->getContentSize().height;
    float curY = m_scrollLayer->m_contentLayer->getPositionY();
    bool nearBottom = (curY <= -(totalH - viewH) + 20.f);

    if (nearBottom && m_scrollArrow->getOpacity() > 0) {
        m_scrollArrow->stopAllActions();
        m_scrollArrow->runAction(CCFadeTo::create(0.3f, 0));
    } else if (!nearBottom && m_scrollArrow->getOpacity() < 150) {
        m_scrollArrow->stopAllActions();
        m_scrollArrow->runAction(CCFadeTo::create(0.3f, 150));
        m_scrollArrow->runAction(CCRepeatForever::create(CCSequence::create(
            CCMoveBy::create(0.5f, {0, 3.f}),
            CCMoveBy::create(0.5f, {0, -3.f}), nullptr)));
    }
}

// ════════════════════════════════════════════════════════════
// apply live
// ════════════════════════════════════════════════════════════

void CursorConfigPopup::applyLive() {
    auto& cm = CursorManager::get();
    cm.applyConfigLive();

    auto scene = CCDirector::sharedDirector()->getRunningScene();
    if (cm.config().enabled) {
        if (!cm.isAttached() && scene) {
            cm.attachToScene(scene);
        }
    } else {
        cm.detachFromScene();
    }
}

// ════════════════════════════════════════════════════════════
// slider callbacks
// ════════════════════════════════════════════════════════════

void CursorConfigPopup::onScaleChanged(CCObject*) {
    if (!m_scaleSlider) return;
    auto& c = CursorManager::get().config();
    c.scale = readSlider(m_scaleSlider, CURSOR_SCALE_MIN, CURSOR_SCALE_MAX);
    if (m_scaleLabel) m_scaleLabel->setString(fmt::format("{:.2f}", c.scale).c_str());
    applyLive();
}

void CursorConfigPopup::onOpacityChanged(CCObject*) {
    if (!m_opacitySlider) return;
    float v = m_opacitySlider->getThumb()->getValue();
    auto& cfg = CursorManager::get().config();
    cfg.opacity = static_cast<int>(v * 255.f);
    cfg.opacity = std::max(0, std::min(255, cfg.opacity));
    if (m_opacityLabel) m_opacityLabel->setString(fmt::format("{}", cfg.opacity).c_str());
    applyLive();
}

// ════════════════════════════════════════════════════════════
// toggle callbacks
// ════════════════════════════════════════════════════════════

void CursorConfigPopup::onEnableToggled(CCObject*) {
    CursorManager::get().config().enabled = !m_enableToggle->isToggled();
    applyLive();
}

void CursorConfigPopup::onTrailToggled(CCObject*) {
    CursorManager::get().config().trailEnabled = !m_trailToggle->isToggled();
    applyLive();
}

// ════════════════════════════════════════════════════════════
// trail preset callbacks
// ════════════════════════════════════════════════════════════

void CursorConfigPopup::updatePresetLabel() {
    if (!m_presetLabel) return;
    auto& cfg = CursorManager::get().config();
    if (cfg.trailPreset < 0 || cfg.trailPreset >= CursorManager::TRAIL_PRESET_COUNT) {
        m_presetLabel->setString("Custom");
    } else {
        m_presetLabel->setString(CursorManager::TRAIL_PRESETS[cfg.trailPreset].name);
    }
}

static void applyPresetToConfig(CursorConfig& cfg) {
    if (cfg.trailPreset < 0 || cfg.trailPreset >= CursorManager::TRAIL_PRESET_COUNT) return;
    auto const& p = CursorManager::TRAIL_PRESETS[cfg.trailPreset];
    cfg.trailR        = p.color.r;
    cfg.trailG        = p.color.g;
    cfg.trailB        = p.color.b;
    cfg.trailLength   = p.length;
    cfg.trailWidth    = p.width;
    cfg.trailFadeType = p.fadeType;
    cfg.trailOpacity  = p.opacity;
}

void CursorConfigPopup::onPresetPrev(CCObject*) {
    auto& cfg = CursorManager::get().config();
    cfg.trailPreset--;
    if (cfg.trailPreset < -1) cfg.trailPreset = CursorManager::TRAIL_PRESET_COUNT - 1;
    if (cfg.trailPreset >= 0) applyPresetToConfig(cfg);
    updatePresetLabel();
    applyLive();
}

void CursorConfigPopup::onPresetNext(CCObject*) {
    auto& cfg = CursorManager::get().config();
    cfg.trailPreset++;
    if (cfg.trailPreset >= CursorManager::TRAIL_PRESET_COUNT) cfg.trailPreset = -1;
    if (cfg.trailPreset >= 0) applyPresetToConfig(cfg);
    updatePresetLabel();
    applyLive();
}

void CursorConfigPopup::onEditTrail(CCObject*) {
    auto& cfg = CursorManager::get().config();
    cfg.trailPreset = -1;
    updatePresetLabel();

    std::string info = fmt::format(
        "<cy>Current Trail Values</c>\n\n"
        "Color: R={} G={} B={}\n"
        "Length: {:.0f}  Width: {:.1f}\n"
        "Fade Type: {}\n"
        "Opacity: {}\n\n"
        "Modify these values by editing <cy>cursor_config.json</c>\n"
        "in your mod save directory.",
        cfg.trailR, cfg.trailG, cfg.trailB,
        cfg.trailLength, cfg.trailWidth,
        cfg.trailFadeType == 0 ? "Linear" : (cfg.trailFadeType == 1 ? "Sine" : "None"),
        cfg.trailOpacity
    );

    FLAlertLayer::create(
        nullptr,
        "Custom Trail",
        info,
        "OK", nullptr, 360.f
    )->show();

    applyLive();
}

// ════════════════════════════════════════════════════════════
// layer visibility toggle
// ════════════════════════════════════════════════════════════

void CursorConfigPopup::onLayerToggled(CCObject* sender) {
    auto toggle = typeinfo_cast<CCMenuItemToggler*>(sender);
    if (!toggle) return;
    auto* nameStr = typeinfo_cast<CCString*>(toggle->getUserObject());
    if (!nameStr) return;

    std::string layerName = nameStr->getCString();
    bool nowOn = !toggle->isToggled();

    auto& layers = CursorManager::get().config().visibleLayers;
    if (nowOn) {
        layers.insert(layerName);
    } else {
        layers.erase(layerName);
    }
    applyLive();
}
