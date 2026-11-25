#include "ModPreviewGalleryPopup.hpp"
#include "../../../ui/PaimonUI.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace paimon::mod_previews {

namespace {

// viewer geometry (own layout, not derived from any other mod).
constexpr float kViewW = 400.f;
constexpr float kViewH = 260.f;
constexpr float kPhotoW = 360.f;
constexpr float kPhotoH = 200.f;

} // namespace

bool ModPreviewGalleryPopup::init(int index, int total, std::string base) {
    if (!PaimonPopup::init(kViewW, kViewH)) return false;
    paimon::markDynamicPopup(this);

    m_index = std::clamp(index, 1, std::max(total, 1));
    m_total = std::max(total, 1);
    m_base = std::move(base);

    this->setTitle("Mod Images");
    this->addInfoButton("Mod Images",
        "Galeria de capturas del mod. Usa las <cy>flechas</c> para pasar entre fotos; "
        "el contador de abajo indica la posicion actual.");

    auto* photoInset = paimon::ui::makeInset({kPhotoW + 8.f, kPhotoH + 8.f}, 110);
    m_mainLayer->addChildAtPosition(photoInset, Anchor::Center,
        {-(kPhotoW + 8.f) / 2.f, 12.f - (kPhotoH + 8.f) / 2.f});


    m_caption = CCLabelBMFont::create("", "bigFont.fnt");
    m_caption->setScale(0.5f);
    m_mainLayer->addChildAtPosition(m_caption, Anchor::Bottom, {0, 30});

    auto bar = CCMenu::create();
    bar->setContentSize({kViewW, 40.f});
    bar->ignoreAnchorPointForPosition(false);
    bar->setAnchorPoint({0.5f, 0.5f});
    m_mainLayer->addChildAtPosition(bar, Anchor::Bottom, {0, 14});

    auto mkArrow = [this](bool flip) {
        auto spr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
        if (flip) spr->setFlipX(true);
        auto btn = CCMenuItemSpriteExtra::create(
            spr, this, flip ? menu_selector(ModPreviewGalleryPopup::onFwd)
                            : menu_selector(ModPreviewGalleryPopup::onBack));
        btn->setScale(0.8f);
        return btn;
    };
    m_backBtn = mkArrow(false);
    m_fwdBtn = mkArrow(true);
    bar->addChildAtPosition(m_backBtn, Anchor::Left, {30, 0});
    bar->addChildAtPosition(m_fwdBtn, Anchor::Right, {-30, 0});

    openIndex(m_index);
    return true;
}

void ModPreviewGalleryPopup::openIndex(int index) {
    m_index = std::clamp(index, 1, m_total);
    m_gen++;
    int gen = m_gen;

    if (m_sprite) {
        m_sprite->cancelLoad();
        m_sprite->setLoadCallback(nullptr);
        m_sprite->removeFromParent();
        m_sprite = nullptr;
    }
    // A loaded LazySprite cannot be reused for another URL on Geode 5.11.
    auto* sprite = LazySprite::create({120, 60});
    if (!sprite) { refreshChrome(); return; }
    m_sprite = sprite;
    sprite->setVisible(false);
    m_mainLayer->addChildAtPosition(sprite, Anchor::Center, {0, 12});
    WeakRef<ModPreviewGalleryPopup> weakSelf = this;
    sprite->setLoadCallback([weakSelf, gen](Result<> res) {
        auto self = weakSelf.lock();
        if (!self || gen != self->m_gen || res.isErr()) return;
        auto* spr = self->m_sprite;
        if (!spr) return;
        float w = spr->getContentWidth();
        float h = spr->getContentHeight();
        if (w > 0.f && h > 0.f) {
            float fit = std::min(kPhotoW / w, kPhotoH / h);
            if (fit > 0.f && fit < 10.f) spr->setScale(fit);
        }
        spr->setVisible(true);
    });
    sprite->loadFromUrl(m_base + std::to_string(m_index) + ".png");
    refreshChrome();
}

void ModPreviewGalleryPopup::refreshChrome() {
    m_caption->setString(fmt::format("Photo {} of {}", m_index, m_total).c_str());
    m_backBtn->setEnabled(m_index > 1);
    m_fwdBtn->setEnabled(m_index < m_total);
    m_backBtn->setOpacity(m_index > 1 ? 255 : 100);
    m_fwdBtn->setOpacity(m_index < m_total ? 255 : 100);
}

void ModPreviewGalleryPopup::onBack(CCObject*) {
    if (m_index > 1) openIndex(m_index - 1);
}

void ModPreviewGalleryPopup::onFwd(CCObject*) {
    if (m_index < m_total) openIndex(m_index + 1);
}

ModPreviewGalleryPopup* ModPreviewGalleryPopup::create(int index, int total, std::string base) {
    auto ret = new ModPreviewGalleryPopup();
    if (ret->init(index, total, std::move(base))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

} // namespace paimon::mod_previews
