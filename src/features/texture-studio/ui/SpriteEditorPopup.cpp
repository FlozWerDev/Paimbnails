#include "SpriteEditorPopup.hpp"

#include "../engine/UiSpriteCatalog.hpp"
#include "../persist/SlotPaths.hpp"
#include "../persist/SlotStore.hpp"
#include "../services/FramePixelCache.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/FileDialog.hpp"
#include "../../../utils/ThreadTracker.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <filesystem>
#include <system_error>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

constexpr float kPopupW = 440.f;
constexpr float kPopupH = 270.f;

// Columna izquierda (previews)
constexpr float kPreviewBox  = 88.f;
constexpr float kLeftMargin  = 16.f;

// Columna derecha (controles)
constexpr float kRightX = kLeftMargin + kPreviewBox + 16.f;

void fitSpriteIntoBox(CCSprite* spr, float boxSize) {
    if (!spr) return;
    auto sz = spr->getContentSize();
    if (sz.width <= 0 || sz.height <= 0) return;
    float maxSide = boxSize - 10.f;
    float scale = std::min(maxSide / sz.width, maxSide / sz.height);
    spr->setScale(std::min(scale, 3.f));
}

CCSprite* makeSwatchSprite(ccColor3B color, float size) {
    auto* spr = CCSprite::create("square.png");
    if (!spr) spr = CCSprite::createWithSpriteFrameName("GJ_button_05.png");
    if (spr) {
        auto sz = spr->getContentSize();
        if (sz.width > 0 && sz.height > 0) {
            spr->setScale(size / std::max(sz.width, sz.height));
        }
        spr->setColor(color);
    }
    return spr;
}

}  // anonymous namespace

SpriteEditorPopup* SpriteEditorPopup::create(std::string slotId,
                                             ProjectSheetRef sheetRef,
                                             std::string frameName,
                                             SavedCallback onSaved) {
    auto* ret = new SpriteEditorPopup();
    if (ret->init(std::move(slotId), std::move(sheetRef),
                  std::move(frameName), std::move(onSaved))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool SpriteEditorPopup::init(std::string slotId, ProjectSheetRef sheetRef,
                             std::string frameName, SavedCallback onSaved) {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    m_slotId    = std::move(slotId);
    m_sheetRef  = std::move(sheetRef);
    m_frameName = std::move(frameName);
    m_onSaved   = std::move(onSaved);

    auto loaded = SlotStore::get().loadSlot(m_slotId);
    if (!loaded) {
        Notification::create(("Cannot load slot: " + loaded.unwrapErr()).c_str(),
            NotificationIcon::Error, 3.0f)->show();
        return true;
    }
    m_project = loaded.unwrap();

    auto it = m_project.spriteSettings.find(m_frameName);
    if (it != m_project.spriteSettings.end()) {
        m_setting = it->second;
    } else {
        m_setting.color1    = m_project.color1;
        m_setting.color2    = m_project.color2;
        m_setting.colorGlow = m_project.colorGlow;
    }
    if (m_setting.skip)                 m_mode = TintMode::Skip;
    else if (m_setting.useCustomColors) m_mode = TintMode::Custom;
    else                                m_mode = TintMode::Global;

    // Título = nombre del frame (recortado si es largo).
    this->setTitle(m_frameName.c_str());
    if (m_title) m_title->limitLabelWidth(kPopupW - 90.f, 0.65f, 0.3f);

    buildPreviewColumn();
    buildControls();
    refreshModeUi();
    updateHint();
    startPixelLoad();

    return true;
}

void SpriteEditorPopup::buildPreviewColumn() {
    auto makeBox = [this](float yOffset, char const* caption,
                          CCNode*& hostOut) {
        auto* host = CCNode::create();
        host->setContentSize({kPreviewBox, kPreviewBox});
        host->setAnchorPoint({0.f, 1.f});
        m_mainLayer->addChildAtPosition(host, Anchor::TopLeft,
            {kLeftMargin, yOffset});

        if (auto* frame = CCScale9Sprite::create("GJ_square01.png")) {
            frame->setContentSize({kPreviewBox, kPreviewBox});
            frame->setColor({26, 26, 32});
            host->addChildAtPosition(frame, Anchor::Center);
        }
        if (auto* lbl = CCLabelBMFont::create(caption, "bigFont.fnt")) {
            lbl->setScale(0.3f);
            lbl->setColor({170, 170, 180});
            host->addChildAtPosition(lbl, Anchor::Top, {0.f, 9.f});
        }
        if (auto* loading = CCLabelBMFont::create("...", "bigFont.fnt")) {
            loading->setScale(0.4f);
            loading->setColor({120, 120, 130});
            loading->setID("loading-label");
            host->addChildAtPosition(loading, Anchor::Center);
        }
        hostOut = host;
    };

    // Original arriba, Result debajo.
    makeBox(-36.f, "Original", m_originalHost);
    makeBox(-36.f - kPreviewBox - 14.f, "Result", m_resultHost);
}

void SpriteEditorPopup::buildControls() {
    if (!m_buttonMenu) return;
    const float rightW = kPopupW - kRightX - 16.f;

    constexpr float kModeY = -44.f;
    if (auto* lbl = CCLabelBMFont::create("Tint mode", "goldFont.fnt")) {
        lbl->setScale(0.45f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setID("tint-mode-label"_spr);
        m_mainLayer->addChildAtPosition(lbl, Anchor::TopLeft, {kRightX, kModeY});
    }

    auto makeModeBtn = [this](char const* label, float x, TintMode mode)
            -> CCMenuItemSpriteExtra* {
        auto* spr = ButtonSprite::create(label, "bigFont.fnt", "GJ_button_04.png", 0.42f);
        if (!spr) return nullptr;
        auto* btn = CCMenuItemExt::createSpriteExtra(spr,
            [this, mode](CCMenuItemSpriteExtra*) {
                m_mode = mode;
                m_setting.skip            = (mode == TintMode::Skip);
                m_setting.useCustomColors = (mode == TintMode::Custom);
                refreshModeUi();
                updateHint();
                scheduleResultRender();
            });
        if (btn) {
            m_buttonMenu->addChildAtPosition(btn, Anchor::TopLeft, {x, -66.f});
        }
        return btn;
    };
    m_modeGlobalBtn = makeModeBtn("Global", kRightX + 34.f,  TintMode::Global);
    m_modeCustomBtn = makeModeBtn("Custom", kRightX + 102.f, TintMode::Custom);
    m_modeSkipBtn   = makeModeBtn("Skip",   kRightX + 170.f, TintMode::Skip);

    constexpr float kColorsY = -96.f;
    if (auto* lbl = CCLabelBMFont::create("Colors", "goldFont.fnt")) {
        lbl->setScale(0.45f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setID("colors-label"_spr);
        m_mainLayer->addChildAtPosition(lbl, Anchor::TopLeft, {kRightX, kColorsY});
    }

    constexpr float kSwatch = 22.f;
    auto makeSwatchBtn = [this](float x, int which, ccColor3B color,
                                char const* caption, CCSprite*& swatchOut) {
        auto* swatch = makeSwatchSprite(color, kSwatch);
        if (!swatch) return;
        auto* btn = CCMenuItemExt::createSpriteExtra(swatch,
            [this, which](CCMenuItemSpriteExtra*) {
                if (m_mode == TintMode::Custom) this->openSwatchPicker(which);
            });
        if (!btn) return;
        swatchOut = swatch;
        m_buttonMenu->addChildAtPosition(btn, Anchor::TopLeft, {x, -118.f});
        if (auto* cap = CCLabelBMFont::create(caption, "bigFont.fnt")) {
            cap->setScale(0.26f);
            cap->setColor({170, 170, 180});
            m_mainLayer->addChildAtPosition(cap, Anchor::TopLeft, {x, -134.f});
        }
    };
    makeSwatchBtn(kRightX + 18.f, 0, m_setting.color1,    "C1",   m_swatch1);
    makeSwatchBtn(kRightX + 60.f, 1, m_setting.color2,    "C2",   m_swatch2);
    makeSwatchBtn(kRightX + 102.f, 2, m_setting.colorGlow, "Glow", m_swatchGlow);

    // Fila 3: imagen de reemplazo
    constexpr float kImageY = -156.f;
    if (auto* lbl = CCLabelBMFont::create("Replace image", "goldFont.fnt")) {
        lbl->setScale(0.45f);
        lbl->setAnchorPoint({0.f, 0.5f});
        lbl->setID("replace-image-label"_spr);
        m_mainLayer->addChildAtPosition(lbl, Anchor::TopLeft, {kRightX, kImageY});
    }

    if (auto* pickSpr = ButtonSprite::create("Pick...", "bigFont.fnt", "GJ_button_05.png", 0.42f)) {
        if (auto* pickBtn = CCMenuItemExt::createSpriteExtra(pickSpr,
                [this](CCMenuItemSpriteExtra*) { this->onPickImage(); })) {
            pickBtn->setID("pick-image-btn"_spr);
            m_buttonMenu->addChildAtPosition(pickBtn, Anchor::TopLeft,
                {kRightX + 34.f, -178.f});
        }
    }
    if (auto* clearSpr = ButtonSprite::create("Clear", "bigFont.fnt", "GJ_button_06.png", 0.42f)) {
        if (auto* clearBtn = CCMenuItemExt::createSpriteExtra(clearSpr,
                [this](CCMenuItemSpriteExtra*) { this->onClearImage(); })) {
            clearBtn->setID("clear-image-btn"_spr);
            m_buttonMenu->addChildAtPosition(clearBtn, Anchor::TopLeft,
                {kRightX + 98.f, -178.f});
        }
    }
    if (auto* imgLbl = CCLabelBMFont::create("none", "bigFont.fnt")) {
        imgLbl->setScale(0.3f);
        imgLbl->setColor({170, 170, 180});
        imgLbl->setAnchorPoint({0.f, 0.5f});
        imgLbl->setID("image-status-label"_spr);
        m_mainLayer->addChildAtPosition(imgLbl, Anchor::TopLeft,
            {kRightX + 132.f, -178.f});
        m_imageLbl = imgLbl;
        if (m_setting.hasCustomImage) m_imageLbl->setString("custom image set");
    }

    // Hint de comportamiento efectivo
    if (auto* hint = CCLabelBMFont::create("", "bigFont.fnt")) {
        hint->setScale(0.3f);
        hint->setColor({150, 210, 160});
        hint->setAnchorPoint({0.f, 0.5f});
        hint->setID("hint-label"_spr);
        m_mainLayer->addChildAtPosition(hint, Anchor::TopLeft,
            {kRightX, -204.f});
        m_hintLbl = hint;
    }

    if (auto* resetSpr = ButtonSprite::create("Reset", "bigFont.fnt", "GJ_button_06.png", 0.5f)) {
        if (auto* resetBtn = CCMenuItemExt::createSpriteExtra(resetSpr,
                [this](CCMenuItemSpriteExtra*) { this->onReset(); })) {
            resetBtn->setID("reset-btn"_spr);
            m_buttonMenu->addChildAtPosition(resetBtn, Anchor::BottomLeft, {52.f, 22.f});
        }
    }
    if (auto* cmpSpr = ButtonSprite::create("Compare", "bigFont.fnt", "GJ_button_05.png", 0.42f)) {
        if (auto* cmpBtn = CCMenuItemExt::createSpriteExtra(cmpSpr,
                [this](CCMenuItemSpriteExtra* s) { this->onToggleCompare(s); })) {
            cmpBtn->setID("compare-btn"_spr);
            m_buttonMenu->addChildAtPosition(cmpBtn, Anchor::BottomLeft, {124.f, 22.f});
            m_compareBtn = cmpBtn;
        }
    }
    if (auto* cancelSpr = ButtonSprite::create("Cancel", "bigFont.fnt", "GJ_button_05.png", 0.5f)) {
        if (auto* cancelBtn = CCMenuItemExt::createSpriteExtra(cancelSpr,
                [this](CCMenuItemSpriteExtra*) { this->onClose(nullptr); })) {
            cancelBtn->setID("cancel-btn"_spr);
            m_buttonMenu->addChildAtPosition(cancelBtn, Anchor::Bottom, {0.f, 22.f});
        }
    }
    if (auto* applySpr = ButtonSprite::create("Apply", "goldFont.fnt", "GJ_button_01.png", 0.55f)) {
        if (auto* applyBtn = CCMenuItemExt::createSpriteExtra(applySpr,
                [this](CCMenuItemSpriteExtra*) { this->onApply(); })) {
            applyBtn->setID("apply-btn"_spr);
            m_buttonMenu->addChildAtPosition(applyBtn, Anchor::BottomRight, {-52.f, 22.f});
        }
    }
}

void SpriteEditorPopup::refreshModeUi() {
    auto highlight = [](CCMenuItemSpriteExtra* btn, bool active) {
        if (!btn) return;
        if (auto* spr = typeinfo_cast<ButtonSprite*>(btn->getNormalImage())) {
            spr->updateBGImage(active ? "GJ_button_01.png" : "GJ_button_04.png");
        }
    };
    highlight(m_modeGlobalBtn, m_mode == TintMode::Global);
    highlight(m_modeCustomBtn, m_mode == TintMode::Custom);
    highlight(m_modeSkipBtn,   m_mode == TintMode::Skip);
    GLubyte op = (m_mode == TintMode::Custom) ? 255 : 90;
    if (m_swatch1)    m_swatch1->setOpacity(op);
    if (m_swatch2)    m_swatch2->setOpacity(op);
    if (m_swatchGlow) m_swatchGlow->setOpacity(op);
}

void SpriteEditorPopup::updateSwatches() {
    if (m_swatch1)    m_swatch1->setColor(m_setting.color1);
    if (m_swatch2)    m_swatch2->setColor(m_setting.color2);
    if (m_swatchGlow) m_swatchGlow->setColor(m_setting.colorGlow);
}

void SpriteEditorPopup::updateHint() {
    if (!m_hintLbl) return;
    char const* text = "";
    if (m_mode == TintMode::Skip) {
        text = "This sprite will keep its vanilla look.";
    } else if (m_setting.hasCustomImage) {
        text = "Your image replaces this sprite (colors ignored).";
    } else if (m_mode == TintMode::Custom) {
        text = "Tinted with this sprite's own colors.";
    } else {
        auto kind = UiSpriteCatalog::classify(m_frameName, m_sheetRef.baseName);
        text = UiSpriteCatalog::shouldTint(kind, m_project.tintScope)
            ? "Tinted with the pack's global colors."
            : "Outside tint scope: stays vanilla unless customized.";
    }
    m_hintLbl->setString(text);
    m_hintLbl->limitLabelWidth(kPopupW - kRightX - 18.f, 0.3f, 0.15f);
}

void SpriteEditorPopup::openSwatchPicker(int which) {
    ccColor3B current = (which == 0) ? m_setting.color1
                       : (which == 1) ? m_setting.color2
                                      : m_setting.colorGlow;
    auto* popup = ColorPickPopup::create(ccColor4B{current.r, current.g, current.b, 255});
    if (!popup) return;
    popup->setCallback([this, which](ccColor4B const& picked) {
        ccColor3B c{picked.r, picked.g, picked.b};
        if (which == 0)      m_setting.color1    = c;
        else if (which == 1) m_setting.color2    = c;
        else                 m_setting.colorGlow = c;
        updateSwatches();
        scheduleResultRender();
    });
    popup->show();
}

void SpriteEditorPopup::setOriginalSprite(CCSprite* spr) {
    if (!m_originalHost || !spr) return;
    if (m_originalSpr) m_originalSpr->removeFromParent();
    if (auto* loading = m_originalHost->getChildByID("loading-label")) {
        loading->removeFromParent();
    }
    fitSpriteIntoBox(spr, kPreviewBox);
    m_originalHost->addChildAtPosition(spr, Anchor::Center, {0.f, -3.f});
    m_originalSpr = spr;
}

void SpriteEditorPopup::setResultSprite(CCSprite* spr) {
    if (!m_resultHost || !spr) return;
    if (m_resultSpr) m_resultSpr->removeFromParent();
    if (auto* loading = m_resultHost->getChildByID("loading-label")) {
        loading->removeFromParent();
    }
    fitSpriteIntoBox(spr, kPreviewBox);
    m_resultHost->addChildAtPosition(spr, Anchor::Center, {0.f, -3.f});
    m_resultSpr = spr;
    // In Compare mode the original is shown in this box instead.
    m_resultSpr->setVisible(!m_compareShowsOriginal);
}

void SpriteEditorPopup::startPixelLoad() {
    WeakRef<SpriteEditorPopup> weakSelf(this);
    std::filesystem::path plist(m_sheetRef.sourcePlistPath);
    std::filesystem::path png(m_sheetRef.sourcePngPath);
    std::string frame = m_frameName;
    bool wantCustomImage = m_setting.hasCustomImage;
    auto customImagePath = SlotPaths::spriteImageFile(m_slotId, m_frameName);

    paimon::ThreadTracker::get().spawn(
        [weakSelf, plist, png, frame, wantCustomImage, customImagePath]() {
            if (paimon::isRuntimeShuttingDown()) return;

            auto pixelsRes = FramePixelCache::get().framePixels(plist, png, frame);
            auto pixels = std::make_shared<ImageBuffer>(
                pixelsRes ? std::move(pixelsRes).unwrap() : ImageBuffer());

            std::shared_ptr<ImageBuffer> customImg;
            if (wantCustomImage) {
                auto imgRes = ImageBuffer::loadFromFile(customImagePath);
                if (imgRes) {
                    customImg = std::make_shared<ImageBuffer>(std::move(imgRes).unwrap());
                }
            }

            Loader::get()->queueInMainThread([weakSelf, pixels, customImg]() {
                if (paimon::isRuntimeShuttingDown()) return;
                auto self = weakSelf.lock();
                if (!self || !self->getParent()) return;

                if (pixels->empty()) {
                    Notification::create("Could not read sprite pixels from the sheet.",
                        NotificationIcon::Error, 2.5f)->show();
                    return;
                }
                self->m_framePixels = pixels;
                self->m_customImage = customImg;
                if (auto* spr = SpritePreviewRenderer::createSprite(*pixels)) {
                    self->setOriginalSprite(spr);
                }
                self->scheduleResultRender();
            });
        });
}

void SpriteEditorPopup::scheduleResultRender() {
    if (!m_framePixels || m_framePixels->empty()) return;

    int gen = m_renderGen->fetch_add(1, std::memory_order_acq_rel) + 1;

    WeakRef<SpriteEditorPopup> weakSelf(this);
    auto renderGen = m_renderGen;
    auto pixels    = m_framePixels;
    auto customImg = m_customImage;
    TintMode mode  = m_mode;
    SpriteSetting setting = m_setting;

    SpritePreviewOptions opts;
    opts.brightness             = m_project.brightness;
    opts.alternativeGlowOverlay = m_project.alternativeGlowOverlay;
    opts.colors.color1 = m_project.color1;
    opts.colors.color2 = m_project.color2;
    opts.colors.glow   = m_project.colorGlow;

    bool globalWouldTint = UiSpriteCatalog::shouldTint(
        UiSpriteCatalog::classify(m_frameName, m_sheetRef.baseName),
        m_project.tintScope);

    paimon::ThreadTracker::get().spawn([weakSelf, renderGen, gen, pixels,
                                        customImg, mode, setting, opts,
                                        globalWouldTint]() {
        if (paimon::isRuntimeShuttingDown()) return;

        ImageBuffer result;
        if (mode == TintMode::Skip) {
            result = *pixels;
        } else if (setting.hasCustomImage && customImg && !customImg->empty()) {
            result = SpritePreviewRenderer::renderCustomImage(
                *customImg, pixels->width(), pixels->height());
        } else if (mode == TintMode::Custom) {
            SpritePreviewOptions custom = opts;
            custom.colors.color1 = setting.color1;
            custom.colors.color2 = setting.color2;
            custom.colors.glow   = setting.colorGlow;
            result = SpritePreviewRenderer::renderTinted(*pixels, custom);
        } else if (globalWouldTint) {
            result = SpritePreviewRenderer::renderTinted(*pixels, opts);
        } else {
            result = *pixels;
        }

        auto resultPtr = std::make_shared<ImageBuffer>(std::move(result));
        Loader::get()->queueInMainThread([weakSelf, renderGen, gen, resultPtr]() {
            if (paimon::isRuntimeShuttingDown()) return;
            if (renderGen->load(std::memory_order_acquire) != gen) return;
            auto self = weakSelf.lock();
            if (!self || !self->getParent()) return;
            if (auto* spr = SpritePreviewRenderer::createSprite(*resultPtr)) {
                self->setResultSprite(spr);
            }
        });
    });
}

void SpriteEditorPopup::onToggleCompare(CCObject*) {
    if (!m_resultHost) return;
    if (!m_framePixels || m_framePixels->empty()) {
        Notification::create("Preview still loading; try again in a moment.",
            NotificationIcon::Warning, 1.5f)->show();
        return;
    }
    m_compareShowsOriginal = !m_compareShowsOriginal;

    // Lazily build the "original inside the Result box" sprite the first
    // time Compare is turned on. It mirrors the trimmed source pixels, so it
    // lines up exactly with the tinted result it overlays.
    if (m_compareShowsOriginal && !m_resultCompareSpr) {
        if (auto* spr = SpritePreviewRenderer::createSprite(*m_framePixels)) {
            fitSpriteIntoBox(spr, kPreviewBox);
            m_resultHost->addChildAtPosition(spr, Anchor::Center, {0.f, -3.f});
            m_resultCompareSpr = spr;
        }
    }
    if (m_resultCompareSpr) m_resultCompareSpr->setVisible(m_compareShowsOriginal);
    if (m_resultSpr)        m_resultSpr->setVisible(!m_compareShowsOriginal);

    // Highlight the button while it's showing the original.
    if (m_compareBtn) {
        if (auto* spr = typeinfo_cast<ButtonSprite*>(m_compareBtn->getNormalImage())) {
            spr->updateBGImage(m_compareShowsOriginal
                ? "GJ_button_01.png" : "GJ_button_05.png");
        }
    }
}

void SpriteEditorPopup::onPickImage() {
    WeakRef<SpriteEditorPopup> weakSelf(this);
    pt::pickImage([weakSelf](geode::Result<std::optional<std::filesystem::path>> result) {
        auto self = weakSelf.lock();
        if (!self) return;
        auto pathOpt = std::move(result).unwrapOr(std::nullopt);
        if (!pathOpt || pathOpt->empty()) return;

        std::filesystem::path srcPath = *pathOpt;
        auto dstPath = SlotPaths::spriteImageFile(self->m_slotId, self->m_frameName);

        paimon::ThreadTracker::get().spawn([weakSelf, srcPath, dstPath]() {
            if (paimon::isRuntimeShuttingDown()) return;

            auto imgRes = ImageBuffer::loadFromFile(srcPath);
            std::shared_ptr<ImageBuffer> img;
            std::string err;
            if (imgRes) {
                img = std::make_shared<ImageBuffer>(std::move(imgRes).unwrap());
                std::error_code ec;
                std::filesystem::create_directories(dstPath.parent_path(), ec);
                if (auto wr = img->saveToPng(dstPath); !wr) {
                    err = wr.unwrapErr();
                    img.reset();
                }
            } else {
                err = imgRes.unwrapErr();
            }

            Loader::get()->queueInMainThread([weakSelf, img, err]() {
                if (paimon::isRuntimeShuttingDown()) return;
                auto self = weakSelf.lock();
                if (!self || !self->getParent()) return;
                if (!img) {
                    Notification::create(("Image import failed: " + err).c_str(),
                        NotificationIcon::Error, 3.0f)->show();
                    return;
                }
                self->m_customImage = img;
                self->m_setting.hasCustomImage = true;
                self->m_imageMarkedForDelete = false;
                if (self->m_imageLbl) self->m_imageLbl->setString("custom image set");
                self->updateHint();
                self->scheduleResultRender();
            });
        });
    });
}

void SpriteEditorPopup::onClearImage() {
    if (!m_setting.hasCustomImage) return;
    m_setting.hasCustomImage = false;
    m_customImage.reset();
    m_imageMarkedForDelete = true;
    if (m_imageLbl) m_imageLbl->setString("none");
    updateHint();
    scheduleResultRender();
}

void SpriteEditorPopup::onReset() {
    m_setting = SpriteSetting{};
    m_setting.color1    = m_project.color1;
    m_setting.color2    = m_project.color2;
    m_setting.colorGlow = m_project.colorGlow;
    m_mode = TintMode::Global;
    m_customImage.reset();
    m_imageMarkedForDelete = true;
    if (m_imageLbl) m_imageLbl->setString("none");
    updateSwatches();
    refreshModeUi();
    updateHint();
    scheduleResultRender();
}

void SpriteEditorPopup::onApply() {
    auto loaded = SlotStore::get().loadSlot(m_slotId);
    if (!loaded) {
        Notification::create(("Slot load failed: " + loaded.unwrapErr()).c_str(),
            NotificationIcon::Error, 3.0f)->show();
        return;
    }
    auto project = loaded.unwrap();

    if (m_setting.hasAny()) {
        project.spriteSettings[m_frameName] = m_setting;
    } else {
        project.spriteSettings.erase(m_frameName);
    }
    project.modifiedAt = nowUnixMs();

    if (m_imageMarkedForDelete && !m_setting.hasCustomImage) {
        std::error_code ec;
        std::filesystem::remove(
            SlotPaths::spriteImageFile(m_slotId, m_frameName), ec);
    }

    auto r = SlotStore::get().saveSlot(project);
    if (!r) {
        Notification::create(("Save failed: " + r.unwrapErr()).c_str(),
            NotificationIcon::Error, 3.0f)->show();
        return;
    }

    Notification::create("Sprite settings saved.", NotificationIcon::Success, 1.2f)->show();
    if (m_onSaved) m_onSaved();
    this->onClose(nullptr);
}

}  // namespace paimon::texture_studio
