#include "SlotsGridView.hpp"

#include "../persist/SlotStore.hpp"
#include "../data/SpritesheetReader.hpp"
#include "../engine/SpritePreviewRenderer.hpp"
#include "../services/FramePixelCache.hpp"
#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/ThreadTracker.hpp"

#include <Geode/Geode.hpp>

#include <ctime>
#include <cstdio>

using namespace geode::prelude;

namespace paimon::texture_studio {

namespace {

constexpr float kCardW = 174.f;
constexpr float kCardH = 110.f;
constexpr float kCardGap = 12.f;

std::string formatRelativeTime(std::int64_t ms) {
    if (ms <= 0) return "never";
    auto now = static_cast<std::int64_t>(std::time(nullptr)) * 1000;
    auto diff = now - ms;
    if (diff < 0) return "just now";
    auto secs = diff / 1000;
    if (secs < 60)    return std::to_string(secs) + "s ago";
    auto mins = secs / 60;
    if (mins < 60)    return std::to_string(mins) + "m ago";
    auto hours = mins / 60;
    if (hours < 24)   return std::to_string(hours) + "h ago";
    auto days = hours / 24;
    if (days < 30)    return std::to_string(days) + "d ago";
    return "long ago";
}

}  // anonymous namespace

SlotsGridView* SlotsGridView::create(float width, float height,
                                     SlotActionCallback onApply,
                                     SlotActionCallback onEdit,
                                     SlotActionCallback onDelete,
                                     std::function<void()> onNewPack) {
    auto* ret = new SlotsGridView();
    if (ret->init(width, height,
                  std::move(onApply),
                  std::move(onEdit),
                  std::move(onDelete),
                  std::move(onNewPack))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool SlotsGridView::init(float width, float height,
                         SlotActionCallback onApply,
                         SlotActionCallback onEdit,
                         SlotActionCallback onDelete,
                         std::function<void()> onNewPack) {
    if (!CCNode::init()) return false;
    m_onApply    = std::move(onApply);
    m_onEdit     = std::move(onEdit);
    m_onDelete   = std::move(onDelete);
    m_onNewPack  = std::move(onNewPack);
    m_widgetWidth  = width;
    m_widgetHeight = height;
    this->setContentSize({width, height});

    auto* scroll = ScrollLayer::create({width, height});
    if (!scroll) return false;
    scroll->setAnchorPoint({0.f, 0.f});
    scroll->setPosition({0.f, 0.f});
    this->addChild(scroll);
    m_contentLayer = scroll->m_contentLayer;
    if (!m_contentLayer) return false;

    refresh();
    return true;
}

void SlotsGridView::refresh() {
    if (!m_contentLayer) return;
    int generation = m_thumbnailGeneration->fetch_add(1, std::memory_order_acq_rel) + 1;
    m_contentLayer->removeAllChildren();

    SlotStore::get().loadIndex();
    auto const& list = SlotStore::get().list();

    int columns = std::max(1, static_cast<int>((m_widgetWidth + kCardGap) / (kCardW + kCardGap)));
    int rowsRequired = (static_cast<int>(list.size()) + 1 + columns - 1) / columns;
    float totalH = std::max(m_widgetHeight, rowsRequired * (kCardH + kCardGap) + kCardGap);
    m_contentLayer->setContentHeight(totalH);

    auto placeCard = [&](CCNode* card, int idx) {
        if (!card) return;
        int col = idx % columns;
        int row = idx / columns;
        float x = kCardGap + col * (kCardW + kCardGap) + kCardW / 2.f;
        float y = totalH - kCardGap - row * (kCardH + kCardGap) - kCardH / 2.f;
        card->setAnchorPoint({0.5f, 0.5f});
        card->setPosition({x, y});
        m_contentLayer->addChild(card);
    };

    int i = 0;
    std::vector<std::pair<int, TextureProject>> thumbnailJobs;
    for (auto const& entry : list) {
        if (auto* card = makeSlotCard(entry.id, entry.name, entry.modifiedAt, entry.hasBuiltOnce)) {
            int cardTag = 1000 + i;
            card->setTag(cardTag);
            placeCard(card, i++);
            auto loaded = SlotStore::get().loadSlot(entry.id);
            if (loaded) {
                auto project = loaded.unwrap();
                thumbnailJobs.emplace_back(cardTag, std::move(project));
            }
        }
    }
    if (auto* card = makeNewPackCard()) {
        placeCard(card, i);
    }
    requestThumbnails(std::move(thumbnailJobs), generation);
}

CCNode* SlotsGridView::makeSlotCard(std::string const& id,
                                    std::string const& name,
                                    std::int64_t modifiedAt,
                                    bool hasBuiltOnce) {
    auto* card = CCNode::create();
    if (!card) return nullptr;
    card->setContentSize({kCardW, kCardH});

    if (auto* bg = CCScale9Sprite::create("GJ_square01.png")) {
        bg->setContentSize({kCardW, kCardH});
        bg->setColor({46, 46, 54});
        card->addChildAtPosition(bg, Anchor::Center);
    }

    if (auto* nameLbl = CCLabelBMFont::create(name.empty() ? "(unnamed)" : name.c_str(), "bigFont.fnt")) {
        nameLbl->setScale(0.5f);
        nameLbl->limitLabelWidth(kCardW - 58.f, 0.5f, 0.25f);
        card->addChildAtPosition(nameLbl, Anchor::Top, {18.f, -16.f});
    }

    if (auto* metaLbl = CCLabelBMFont::create(
            (formatRelativeTime(modifiedAt) + (hasBuiltOnce ? "   Built" : "   Draft")).c_str(),
            "bigFont.fnt")) {
        metaLbl->setScale(0.32f);
        metaLbl->setColor(hasBuiltOnce ? ccColor3B{120, 210, 130} : ccColor3B{170, 170, 178});
        card->addChildAtPosition(metaLbl, Anchor::Top, {18.f, -34.f});
    }

    auto* previewHost = CCNode::create();
    previewHost->setContentSize({38.f, 38.f});
    previewHost->setTag(200);
    card->addChildAtPosition(previewHost, Anchor::TopLeft, {24.f, -27.f});
    if (auto* placeholder = CCSprite::create("square.png")) {
        placeholder->setColor({78, 80, 90});
        placeholder->setOpacity(150);
        placeholder->setScale(32.f / std::max(1.f, placeholder->getContentSize().width));
        placeholder->setTag(201);
        previewHost->addChildAtPosition(placeholder, Anchor::Center);
    }

    // CCMenu keeps ignoreAnchorPointForPosition=true: its position is the
    // menu centre and children are offsets from that centre. Avoid
    // addChildAtPosition here — AnchorLayout flips the flag and piles every
    // item on top of each other (Edit/Delete stacked).
    auto* menu = CCMenu::create();
    if (!menu) return card;
    menu->setPosition({kCardW * 0.5f, kCardH * 0.5f});

    // Local Y of the card's bottom edge, relative to the menu centre.
    constexpr float kBottom = -kCardH * 0.5f;

    if (auto* applySpr = ButtonSprite::create("Apply", "goldFont.fnt", "GJ_button_01.png", 0.42f)) {
        if (auto* applyBtn = CCMenuItemExt::createSpriteExtra(applySpr,
                [this, id](CCMenuItemSpriteExtra*) { if (m_onApply) m_onApply(id); })) {
            applyBtn->setPosition({0.f, kBottom + 50.f});
            menu->addChild(applyBtn);
        }
    }

    auto makeMini = [&](char const* label, char const* sprite,
                        std::function<void()> action) -> CCMenuItemSpriteExtra* {
        auto* spr = ButtonSprite::create(label, "bigFont.fnt", sprite, 0.34f);
        if (!spr) return nullptr;
        return CCMenuItemExt::createSpriteExtra(spr,
            [action = std::move(action)](CCMenuItemSpriteExtra*) { if (action) action(); });
    };

    // Side-by-side at the bottom; centres ~100 px apart so the sprites don't touch.
    if (auto* editBtn = makeMini("Edit", "GJ_button_04.png",
            [this, id]() { if (m_onEdit) m_onEdit(id); })) {
        editBtn->setPosition({-50.f, kBottom + 18.f});
        menu->addChild(editBtn);
    }
    if (auto* delBtn = makeMini("Delete", "GJ_button_06.png",
            [this, id]() { if (m_onDelete) m_onDelete(id); })) {
        delBtn->setPosition({50.f, kBottom + 18.f});
        menu->addChild(delBtn);
    }

    card->addChild(menu);
    return card;
}

void SlotsGridView::requestThumbnails(
    std::vector<std::pair<int, TextureProject>> jobs, int generation) {
    if (jobs.empty()) return;
    WeakRef<SlotsGridView> weakSelf(this);
    auto currentGeneration = m_thumbnailGeneration;
    paimon::ThreadTracker::get().spawn(
        [weakSelf, currentGeneration, generation, jobs = std::move(jobs)]() mutable {
        for (auto& [cardTag, project] : jobs) {
            if (paimon::isRuntimeShuttingDown() ||
                currentGeneration->load(std::memory_order_acquire) != generation) return;
            if (!ensureRepresentativeFrame(project)) continue;
            int sheetIndex = project.representativeSheetIndex;
            if (sheetIndex < 0 || sheetIndex >= static_cast<int>(project.sheets.size())) continue;
            auto const& sheet = project.sheets[sheetIndex];
            auto data = FramePixelCache::get().frameData(
                std::filesystem::path(sheet.sourcePlistPath),
                std::filesystem::path(sheet.sourcePngPath), project.representativeFrame);
            if (!data) continue;
            auto frame = std::move(data).unwrap();
            SpritePreviewOptions options;
            options.colors.color1 = project.color1;
            options.colors.color2 = project.color2;
            options.colors.glow = project.colorGlow;
            options.brightness = project.brightness;
            options.alternativeGlowOverlay = project.alternativeGlowOverlay;
            options.maskSoftness     = project.maskSoftness;
            options.clusterPrecision = project.clusterPrecision;
            options.edgeCleanup      = project.edgeCleanup;
            options.outlineProtect   = project.outlineProtect;
            options.saturation       = project.saturation;
            options.contrast         = project.contrast;
            auto tinted = SpritePreviewRenderer::renderTinted(frame.pixels, options);
            auto image = std::make_shared<ImageBuffer>(
                SpritesheetReader::composeLogicalFrame(tinted, frame.info));
            Loader::get()->queueInMainThread(
                [weakSelf, currentGeneration, generation, cardTag, image]() {
                if (paimon::isRuntimeShuttingDown() ||
                    currentGeneration->load(std::memory_order_acquire) != generation) return;
                auto self = weakSelf.lock();
                if (!self || !self->getParent()) return;
                self->applyThumbnail(cardTag, generation, image);
            });
        }
    });
}

void SlotsGridView::applyThumbnail(int cardTag, int generation,
                                   std::shared_ptr<ImageBuffer> image) {
    if (!image || image->empty() || !m_contentLayer ||
        m_thumbnailGeneration->load(std::memory_order_acquire) != generation) return;
    auto* card = m_contentLayer->getChildByTag(cardTag);
    auto* host = card ? card->getChildByTag(200) : nullptr;
    if (!host) return;
    if (auto* placeholder = host->getChildByTag(201)) placeholder->removeFromParent();
    if (auto* sprite = SpritePreviewRenderer::createSprite(*image)) {
        auto size = sprite->getContentSize();
        if (size.width > 0.f && size.height > 0.f) {
            sprite->setScale(std::min({34.f / size.width, 34.f / size.height, 2.f}));
        }
        sprite->setTag(201);
        host->addChildAtPosition(sprite, Anchor::Center);
    }
}

CCNode* SlotsGridView::makeNewPackCard() {
    auto* card = CCNode::create();
    if (!card) return nullptr;
    card->setContentSize({kCardW, kCardH});

    auto* menu = CCMenu::create();
    if (!menu) return card;
    menu->setContentSize({kCardW, kCardH});

    if (auto* bg = CCScale9Sprite::create("GJ_square01.png")) {
        bg->setContentSize({kCardW, kCardH});
        bg->setColor({34, 58, 38});
        if (auto* btn = CCMenuItemExt::createSpriteExtra(bg,
                [this](CCMenuItemSpriteExtra*) { if (m_onNewPack) m_onNewPack(); })) {
            menu->addChildAtPosition(btn, Anchor::Center);
        }
    }
    card->addChildAtPosition(menu, Anchor::Center);

    if (auto* plusLbl = CCLabelBMFont::create("+", "bigFont.fnt")) {
        plusLbl->setScale(1.2f);
        plusLbl->setColor({150, 220, 150});
        card->addChildAtPosition(plusLbl, Anchor::Center, {0.f, 14.f});
    }
    if (auto* tlbl = CCLabelBMFont::create("New Pack", "bigFont.fnt")) {
        tlbl->setScale(0.42f);
        tlbl->setColor({170, 230, 170});
        card->addChildAtPosition(tlbl, Anchor::Center, {0.f, -24.f});
    }

    return card;
}

}  // namespace paimon::texture_studio
