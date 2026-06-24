#include "ModPreviewGalleryPopup.hpp"
#include <Geode/Geode.hpp>
#include <algorithm>

using namespace geode::prelude;

namespace paimon::mod_previews {

bool ModPreviewGalleryPopup::init(int page, int count, std::string urlBase) {
    if (!Popup::init(380.f, 250.f)) return false;

    m_page = page;
    m_count = std::max(count, 1);
    m_urlBase = std::move(urlBase);

    this->setTitle("Preview");

    auto nav = CCMenu::create();
    nav->setContentSize(m_mainLayer->getContentSize());
    nav->ignoreAnchorPointForPosition(false);
    nav->setAnchorPoint({0.5f, 0.5f});
    m_mainLayer->addChildAtPosition(nav, Anchor::Center);

    auto prevSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
    auto prevBtn = CCMenuItemSpriteExtra::create(prevSpr, this, menu_selector(ModPreviewGalleryPopup::onPrev));
    nav->addChildAtPosition(prevBtn, Anchor::Left, {16, 0});

    auto nextSpr = CCSprite::createWithSpriteFrameName("GJ_arrow_02_001.png");
    nextSpr->setFlipX(true);
    auto nextBtn = CCMenuItemSpriteExtra::create(nextSpr, this, menu_selector(ModPreviewGalleryPopup::onNext));
    nav->addChildAtPosition(nextBtn, Anchor::Right, {-16, 0});

    m_label = CCLabelBMFont::create("", "goldFont.fnt");
    m_label->setAnchorPoint({1.f, 1.f});
    m_label->setScale(0.4f);
    m_mainLayer->addChildAtPosition(m_label, Anchor::TopRight, {-8, -8});

    showImage(m_page);
    return true;
}

void ModPreviewGalleryPopup::showImage(int page) {
    if (m_current) m_current->setVisible(false);

    LazySprite* spr;
    if (auto it = m_cache.find(page); it != m_cache.end()) {
        spr = it->second;
        spr->setVisible(true);
        onLoad(spr);
    } else {
        spr = LazySprite::create({100, 50});
        m_cache[page] = spr;
        m_mainLayer->addChildAtPosition(spr, Anchor::Center, {0, -10});
        spr->setLoadCallback([this, spr](Result<> res) {
            if (res.isOk()) this->onLoad(spr);
        });
        spr->loadFromUrl(m_urlBase + std::to_string(page) + ".png");
    }

    m_current = spr;
    m_label->setString(fmt::format("Image {}/{}", page, m_count).c_str());
}

void ModPreviewGalleryPopup::onLoad(LazySprite* spr) {
    float scale = std::min(340.f / spr->getContentWidth(), 210.f / spr->getContentHeight());
    spr->setScale(scale);
    spr->setVisible(spr == m_current);
}

void ModPreviewGalleryPopup::onPrev(CCObject*) {
    m_page = (m_page <= 1) ? m_count : m_page - 1;
    showImage(m_page);
}

void ModPreviewGalleryPopup::onNext(CCObject*) {
    m_page = (m_page >= m_count) ? 1 : m_page + 1;
    showImage(m_page);
}

ModPreviewGalleryPopup* ModPreviewGalleryPopup::create(int page, int count, std::string urlBase) {
    auto ret = new ModPreviewGalleryPopup();
    if (ret->init(page, count, std::move(urlBase))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

} // namespace paimon::mod_previews
