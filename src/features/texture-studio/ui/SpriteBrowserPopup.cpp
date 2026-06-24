#include "SpriteBrowserPopup.hpp"

#include "../data/PlistParser.hpp"
#include "../data/SpritesheetReader.hpp"
#include "../engine/SpritePreviewRenderer.hpp"
#include "../persist/SlotPaths.hpp"
#include "../persist/SlotStore.hpp"
#include "../services/FramePixelCache.hpp"
#include "SpriteEditorPopup.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/ThreadTracker.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

constexpr float kPopupW = 480.f;
constexpr float kPopupH = 300.f;

constexpr int   kCols  = 6;
constexpr int   kRows  = 3;
constexpr int   kPerPage = kCols * kRows;
constexpr float kCellW = 72.f;
constexpr float kCellH = 70.f;
constexpr float kGridTopY = -64.f;   // offset desde TopLeft del mainLayer

std::string toLowerCopy(std::string const& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

}  // anonymous namespace

SpriteBrowserPopup* SpriteBrowserPopup::create(std::string slotId,
                                               std::function<void()> onClosed) {
    auto* ret = new SpriteBrowserPopup();
    if (ret->init(std::move(slotId), std::move(onClosed))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool SpriteBrowserPopup::init(std::string slotId, std::function<void()> onClosed) {
    if (!Popup::init(kPopupW, kPopupH)) return false;
    m_slotId   = std::move(slotId);
    m_onClosed = std::move(onClosed);

    auto loaded = SlotStore::get().loadSlot(m_slotId);
    if (!loaded) {
        Notification::create(("Cannot load slot: " + loaded.unwrapErr()).c_str(),
            NotificationIcon::Error, 3.0f)->show();
        return true;
    }
    m_project = loaded.unwrap();
    this->setTitle(("Sprites: " + m_project.name).c_str());
    if (m_title) m_title->limitLabelWidth(kPopupW - 120.f, 0.7f, 0.3f);

    if (auto* search = TextInput::create(150.f, "Search...")) {
        search->setMaxCharCount(32);
        search->setScale(0.8f);
        search->setID("search-input"_spr);
        search->setCallback([this](std::string const& text) {
            m_search = toLowerCopy(text);
            applyFilter();
        });
        m_mainLayer->addChildAtPosition(search, Anchor::TopLeft, {80.f, -40.f});
    }

    auto makeFilterBtn = [this](char const* label, float x, int mode)
            -> CCMenuItemSpriteExtra* {
        auto* spr = ButtonSprite::create(label, "bigFont.fnt", "GJ_button_04.png", 0.38f);
        if (!spr) return nullptr;
        auto* btn = CCMenuItemExt::createSpriteExtra(spr,
            [this, mode](CCMenuItemSpriteExtra*) {
                m_filterMode = mode;
                refreshFilterButtons();
                applyFilter();
            });
        if (btn && m_buttonMenu) {
            m_buttonMenu->addChildAtPosition(btn, Anchor::TopLeft, {x, -40.f});
        }
        return btn;
    };
    m_filterButtonsBtn = makeFilterBtn("Buttons", 218.f, 0);
    m_filterAllUiBtn   = makeFilterBtn("All UI",  296.f, 1);
    m_filterEditedBtn  = makeFilterBtn("Edited",  366.f, 2);

    // Host del grid
    auto* gridHost = CCNode::create();
    gridHost->setContentSize({kCols * kCellW, kRows * kCellH});
    gridHost->setAnchorPoint({0.5f, 1.f});
    gridHost->setID("grid-host"_spr);
    m_mainLayer->addChildAtPosition(gridHost, Anchor::Top, {0.f, kGridTopY});
    m_gridHost = gridHost;

    if (m_buttonMenu) {
        if (auto* prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
            prevSpr->setScale(0.55f);
            if (auto* prevBtn = CCMenuItemExt::createSpriteExtra(prevSpr,
                    [this](CCMenuItemSpriteExtra*) {
                        if (m_page > 0) { --m_page; rebuildGrid(); }
                    })) {
                prevBtn->setID("prev-page-btn"_spr);
                m_buttonMenu->addChildAtPosition(prevBtn, Anchor::Bottom, {-60.f, 22.f});
            }
        }
        if (auto* nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_01_001.png")) {
            nextSpr->setScale(0.55f);
            nextSpr->setFlipX(true);
            if (auto* nextBtn = CCMenuItemExt::createSpriteExtra(nextSpr,
                    [this](CCMenuItemSpriteExtra*) {
                        int pages = std::max(1,
                            (static_cast<int>(m_filtered.size()) + kPerPage - 1) / kPerPage);
                        if (m_page + 1 < pages) { ++m_page; rebuildGrid(); }
                    })) {
                nextBtn->setID("next-page-btn"_spr);
                m_buttonMenu->addChildAtPosition(nextBtn, Anchor::Bottom, {60.f, 22.f});
            }
        }
    }
    if (auto* pageLbl = CCLabelBMFont::create("1 / 1", "bigFont.fnt")) {
        pageLbl->setScale(0.4f);
        pageLbl->setID("page-label"_spr);
        m_mainLayer->addChildAtPosition(pageLbl, Anchor::Bottom, {0.f, 22.f});
        m_pageLbl = pageLbl;
    }
    if (auto* countLbl = CCLabelBMFont::create("", "bigFont.fnt")) {
        countLbl->setScale(0.3f);
        countLbl->setColor({170, 170, 180});
        countLbl->setAnchorPoint({0.f, 0.5f});
        countLbl->setID("count-label"_spr);
        m_mainLayer->addChildAtPosition(countLbl, Anchor::BottomLeft, {16.f, 22.f});
        m_countLbl = countLbl;
    }

    buildEntries();
    refreshFilterButtons();
    applyFilter();
    return true;
}

void SpriteBrowserPopup::onClose(CCObject* sender) {
    m_closed->store(true, std::memory_order_release);
    m_renderGeneration->fetch_add(1, std::memory_order_acq_rel);
    if (m_onClosed) m_onClosed();
    Popup::onClose(sender);
}

void SpriteBrowserPopup::buildEntries() {
    m_all.clear();

    for (int i = 0; i < static_cast<int>(m_project.sheets.size()); ++i) {
        auto const& sheet = m_project.sheets[i];
        auto parsed = PlistParser::parseFile(
            std::filesystem::path(sheet.sourcePlistPath));
        if (!parsed) {
            log::warn("[texture-studio] browser: cannot parse {}: {}",
                sheet.sourcePlistPath, parsed.unwrapErr());
            continue;
        }
        for (auto const& frame : parsed.unwrap().frames) {
            auto kind = UiSpriteCatalog::classify(frame.name, sheet.baseName);
            if (kind != SpriteKind::Button && kind != SpriteKind::MenuUi) continue;
            Entry e;
            e.frameName  = frame.name;
            e.sheetIndex = i;
            e.kind       = kind;
            m_all.push_back(std::move(e));
        }
    }

    std::sort(m_all.begin(), m_all.end(), [](Entry const& a, Entry const& b) {
        if (a.kind != b.kind) return a.kind == SpriteKind::Button;
        return a.frameName < b.frameName;
    });
}

void SpriteBrowserPopup::applyFilter() {
    m_filtered.clear();
    for (int i = 0; i < static_cast<int>(m_all.size()); ++i) {
        auto const& e = m_all[i];

        if (m_filterMode == 0 && e.kind != SpriteKind::Button) continue;
        if (m_filterMode == 2 &&
            m_project.spriteSettings.find(e.frameName) == m_project.spriteSettings.end()) {
            continue;
        }

        if (!m_search.empty()) {
            if (toLowerCopy(e.frameName).find(m_search) == std::string::npos) {
                continue;
            }
        }
        m_filtered.push_back(i);
    }
    m_page = 0;
    rebuildGrid();
}

void SpriteBrowserPopup::rebuildGrid() {
    if (!m_gridHost) return;
    int generation = m_renderGeneration->fetch_add(1, std::memory_order_acq_rel) + 1;
    m_gridHost->removeAllChildren();
    m_gridMenu = nullptr;

    int total = static_cast<int>(m_filtered.size());
    int pages = std::max(1, (total + kPerPage - 1) / kPerPage);
    m_page = std::clamp(m_page, 0, pages - 1);

    if (m_pageLbl) {
        m_pageLbl->setString(
            (std::to_string(m_page + 1) + " / " + std::to_string(pages)).c_str());
    }
    if (m_countLbl) {
        int edited = 0;
        for (auto const& [k, v] : m_project.spriteSettings) {
            if (v.hasAny()) ++edited;
        }
        m_countLbl->setString(
            (std::to_string(total) + " sprites, " +
             std::to_string(edited) + " edited").c_str());
    }

    if (total == 0) {
        if (auto* empty = CCLabelBMFont::create(
                m_filterMode == 2 ? "No edited sprites yet." : "No sprites match.",
                "bigFont.fnt")) {
            empty->setScale(0.45f);
            empty->setColor({170, 170, 180});
            m_gridHost->addChildAtPosition(empty, Anchor::Center);
        }
        return;
    }

    auto* menu = CCMenu::create();
    if (!menu) return;
    menu->setContentSize(m_gridHost->getContentSize());
    m_gridHost->addChildAtPosition(menu, Anchor::Center);
    m_gridMenu = menu;

    int start = m_page * kPerPage;
    int end   = std::min(total, start + kPerPage);
    float gridH = m_gridHost->getContentSize().height;

    for (int slot = 0; slot < end - start; ++slot) {
        auto const& entry = m_all[m_filtered[start + slot]];
        int col = slot % kCols;
        int row = slot / kCols;

        auto* cell = CCNode::create();
        cell->setContentSize({kCellW - 6.f, kCellH - 6.f});

        if (auto* bg = CCScale9Sprite::create("GJ_square01.png")) {
            bg->setContentSize(cell->getContentSize());
            bg->setColor(entry.kind == SpriteKind::Button
                ? ccColor3B{40, 44, 56} : ccColor3B{36, 36, 42});
            cell->addChildAtPosition(bg, Anchor::Center);
        }

        CCSprite* thumb = CCSprite::create("square.png");
        if (thumb) {
            thumb->setColor({72, 75, 84});
            thumb->setOpacity(150);
            thumb->setTag(100);
            auto sz = thumb->getContentSize();
            if (sz.width > 0 && sz.height > 0) {
                float maxSide = 38.f;
                thumb->setScale(std::min(
                    {maxSide / sz.width, maxSide / sz.height, 2.f}));
            }
            cell->addChildAtPosition(thumb, Anchor::Center, {0.f, 6.f});
        }
        if (auto* loading = CCLabelBMFont::create("...", "bigFont.fnt")) {
            loading->setScale(0.3f);
            loading->setTag(102);
            cell->addChildAtPosition(loading, Anchor::Center, {0.f, 6.f});
        }

        std::string shortName = entry.frameName;
        if (auto pos = shortName.rfind("_001.png"); pos != std::string::npos) {
            shortName.resize(pos);
        } else if (auto pos2 = shortName.rfind(".png"); pos2 != std::string::npos) {
            shortName.resize(pos2);
        }
        if (auto* nameLbl = CCLabelBMFont::create(shortName.c_str(), "chatFont.fnt")) {
            nameLbl->limitLabelWidth(kCellW - 12.f, 0.45f, 0.1f);
            cell->addChildAtPosition(nameLbl, Anchor::Bottom, {0.f, 8.f});
        }
        auto settingIt = m_project.spriteSettings.find(entry.frameName);
        if (settingIt != m_project.spriteSettings.end() && settingIt->second.hasAny()) {
            auto const& s = settingIt->second;
            char const* badgeText = s.skip ? "S" : (s.hasCustomImage ? "I" : "C");
            ccColor3B badgeColor = s.skip ? ccColor3B{235, 90, 90}
                                 : (s.hasCustomImage ? ccColor3B{190, 120, 255}
                                                     : ccColor3B{120, 230, 130});
            if (auto* badge = CCLabelBMFont::create(badgeText, "bigFont.fnt")) {
                badge->setScale(0.32f);
                badge->setColor(badgeColor);
                cell->addChildAtPosition(badge, Anchor::TopRight, {-7.f, -8.f});
            }
        }

        Entry entryCopy = entry;
        auto* item = CCMenuItemExt::createSpriteExtra(cell,
            [this, entryCopy](CCMenuItemSpriteExtra*) {
                this->openEditor(entryCopy);
            });
        if (!item) continue;

        item->setTag(slot + 1);

        item->setPosition({(col + 0.5f) * kCellW,
                           gridH - (row + 0.5f) * kCellH});
        menu->addChild(item);
    }

    std::vector<Entry> pageEntries;
    pageEntries.reserve(end - start);
    for (int i = start; i < end; ++i) pageEntries.push_back(m_all[m_filtered[i]]);
    requestTintedThumbnails(std::move(pageEntries), generation);
}

void SpriteBrowserPopup::requestTintedThumbnails(std::vector<Entry> entries,
                                                  int generation) {
    if (entries.empty()) return;

    WeakRef<SpriteBrowserPopup> weakSelf(this);
    auto renderGeneration = m_renderGeneration;
    auto closed = m_closed;
    auto project = m_project;
    auto slotId = m_slotId;

    paimon::ThreadTracker::get().spawn(
        [weakSelf, renderGeneration, closed, project, slotId,
         entries = std::move(entries), generation]() mutable {
        for (int slot = 0; slot < static_cast<int>(entries.size()); ++slot) {
            if (paimon::isRuntimeShuttingDown() ||
                closed->load(std::memory_order_acquire) ||
                renderGeneration->load(std::memory_order_acquire) != generation) {
                return;
            }

            auto const& entry = entries[slot];
            if (entry.sheetIndex < 0 ||
                entry.sheetIndex >= static_cast<int>(project.sheets.size())) continue;
            auto const& sheet = project.sheets[entry.sheetIndex];
            auto dataRes = FramePixelCache::get().frameData(
                std::filesystem::path(sheet.sourcePlistPath),
                std::filesystem::path(sheet.sourcePngPath), entry.frameName);
            if (!dataRes) continue;
            auto data = std::move(dataRes).unwrap();

            SpritePreviewResult preview;
            SpritePreviewOptions options;
            options.brightness = project.brightness;
            options.alternativeGlowOverlay = project.alternativeGlowOverlay;
            options.colors.color1 = project.color1;
            options.colors.color2 = project.color2;
            options.colors.glow = project.colorGlow;

            auto settingIt = project.spriteSettings.find(entry.frameName);
            SpriteSetting setting;
            if (settingIt != project.spriteSettings.end()) setting = settingIt->second;
            bool shouldTint = UiSpriteCatalog::shouldTint(entry.kind, project.tintScope);

            if (setting.skip || (!setting.useCustomColors && !shouldTint)) {
                preview.image = data.pixels;
            } else if (setting.hasCustomImage) {
                auto custom = ImageBuffer::loadFromFile(
                    SlotPaths::spriteImageFile(slotId, entry.frameName));
                preview.image = custom
                    ? SpritePreviewRenderer::renderCustomImage(
                        custom.unwrap(), data.pixels.width(), data.pixels.height())
                    : data.pixels;
            } else {
                if (setting.useCustomColors) {
                    options.colors.color1 = setting.color1;
                    options.colors.color2 = setting.color2;
                    options.colors.glow = setting.colorGlow;
                }
                preview = SpritePreviewRenderer::renderTintedWithStats(data.pixels, options);
            }
            preview.image = SpritesheetReader::composeLogicalFrame(preview.image, data.info);

            auto result = std::make_shared<SpritePreviewResult>(std::move(preview));
            Loader::get()->queueInMainThread(
                [weakSelf, renderGeneration, closed, generation, slot, result]() {
                if (paimon::isRuntimeShuttingDown() ||
                    closed->load(std::memory_order_acquire) ||
                    renderGeneration->load(std::memory_order_acquire) != generation) return;
                auto self = weakSelf.lock();
                if (!self || !self->getParent()) return;
                self->applyTintedThumbnail(slot, generation, result);
            });
        }
    });
}

void SpriteBrowserPopup::applyTintedThumbnail(
    int slot, int generation, std::shared_ptr<SpritePreviewResult> result) {
    if (!result || result->image.empty() || !m_gridMenu ||
        m_renderGeneration->load(std::memory_order_acquire) != generation) return;

    auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(m_gridMenu->getChildByTag(slot + 1));
    if (!item) return;
    auto* cell = item->getNormalImage();
    if (!cell) return;
    if (auto* old = cell->getChildByTag(100)) old->removeFromParent();
    if (auto* loading = cell->getChildByTag(102)) loading->removeFromParent();

    if (auto* sprite = SpritePreviewRenderer::createSprite(result->image)) {
        auto size = sprite->getContentSize();
        if (size.width > 0.f && size.height > 0.f) {
            sprite->setScale(std::min({38.f / size.width, 38.f / size.height, 2.f}));
        }
        sprite->setTag(100);
        cell->addChildAtPosition(sprite, Anchor::Center, {0.f, 6.f});
    }

    char const* badgeText = result->stats.needsReview ? "!" :
        (result->stats.color1Coverage >= 0.05f ? "+" : "-");
    ccColor3B badgeColor = result->stats.needsReview ? ccColor3B{255, 190, 70} :
        (result->stats.color1Coverage >= 0.05f ? ccColor3B{110, 235, 125}
                                               : ccColor3B{145, 145, 155});
    if (auto* badge = CCLabelBMFont::create(badgeText, "bigFont.fnt")) {
        badge->setScale(0.28f);
        badge->setColor(badgeColor);
        badge->setTag(101);
        cell->addChildAtPosition(badge, Anchor::TopLeft, {7.f, -8.f});
    }
}

void SpriteBrowserPopup::refreshFilterButtons() {
    auto highlight = [](CCMenuItemSpriteExtra* btn, bool active) {
        if (!btn) return;
        if (auto* spr = typeinfo_cast<ButtonSprite*>(btn->getNormalImage())) {
            spr->updateBGImage(active ? "GJ_button_01.png" : "GJ_button_04.png");
        }
    };
    highlight(m_filterButtonsBtn, m_filterMode == 0);
    highlight(m_filterAllUiBtn,   m_filterMode == 1);
    highlight(m_filterEditedBtn,  m_filterMode == 2);
}

void SpriteBrowserPopup::openEditor(Entry const& entry) {
    if (entry.sheetIndex < 0 ||
        entry.sheetIndex >= static_cast<int>(m_project.sheets.size())) {
        return;
    }
    auto sheetRef = m_project.sheets[entry.sheetIndex];

    auto* editor = SpriteEditorPopup::create(
        m_slotId, sheetRef, entry.frameName,
        [this]() {
            auto loaded = SlotStore::get().loadSlot(m_slotId);
            if (loaded) m_project = loaded.unwrap();
            rebuildGrid();
        });
    if (editor) editor->show();
}

}  // namespace paimon::texture_studio
