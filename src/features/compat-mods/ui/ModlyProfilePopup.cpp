#include "ModlyProfilePopup.hpp"
#include "ModlyModPopup.hpp"
#include "ModlyUIHelpers.hpp"
#include "../services/ModlyRepo.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/Scrollbar.hpp>
#include <Geode/ui/TextArea.hpp>

using namespace geode::prelude;

namespace paimon::compat_mods {

namespace {
    constexpr float kWidth = 420.f;
    constexpr float kHeight = 300.f;
    constexpr float kBannerW = 380.f;
    constexpr float kBannerH = 58.f;
    constexpr float kAvatar = 46.f;
    constexpr float kListW = 380.f;
    constexpr float kListH = 108.f;
    constexpr float kRowH = 34.f;
}

bool ModlyProfilePopup::init(ModlyUser const& user) {
    if (!Popup::init(kWidth, kHeight)) return false;
    paimon::markDynamicPopup(this);

    m_user = user;
    m_projects = ModlyRepo::get().modsByAuthor(user.uid);
    this->setTitle(Localization::get().getString("modly.profile_title").c_str());

    buildHeader();
    buildProjects();
    return true;
}

void ModlyProfilePopup::buildHeader() {
    auto& repo = ModlyRepo::get();
    auto& loc = Localization::get();

    // The site falls back to a gradient built from the name when there is no
    // banner; the placeholder colour here plays the same role.
    auto nameColor = avatarColor(m_user.name);
    auto banner = createImageSlot(
        m_user.hasBanner ? repo.bannerUrl(m_user) : "",
        kBannerW, kBannerH, 8.f,
        {static_cast<GLubyte>(nameColor.r * 0.6f),
         static_cast<GLubyte>(nameColor.g * 0.6f),
         static_cast<GLubyte>(nameColor.b * 0.6f), 255});
    banner->setPosition({kWidth / 2.f, kHeight - 78.f});
    m_mainLayer->addChild(banner, 1);

    auto avatar = createAvatar(repo.photoUrl(m_user), m_user.hasPhoto, m_user.name, kAvatar);
    avatar->setPosition({54.f, kHeight - 100.f});
    m_mainLayer->addChild(avatar, 3);

    float textX = 86.f;
    float nameY = kHeight - 92.f;

    auto nameLabel = CCLabelBMFont::create(
        m_user.name.empty() ? loc.getString("modly.unknown_author").c_str() : m_user.name.c_str(),
        "bigFont.fnt");
    nameLabel->setScale(0.5f);
    nameLabel->setAnchorPoint({0.f, 0.5f});
    nameLabel->setPosition({textX, nameY});
    fitLabelWidth(nameLabel, 220.f);
    m_mainLayer->addChild(nameLabel, 3);

    float cursorX = textX + nameLabel->getContentSize().width * nameLabel->getScale() + 6.f;
    if (auto* seal = createRankSeal(m_user, 17.f)) {
        seal->setPosition({cursorX, nameY});
        m_mainLayer->addChild(seal, 3);
    }

    float tagX = textX;
    float tagY = kHeight - 112.f;
    for (auto const& tag : m_user.tags) {
        auto pill = createPill(translateTag(tag), {90, 95, 130}, 0.3f);
        if (tagX + pill->getContentSize().width > kWidth - 30.f) break;
        pill->setPosition({tagX, tagY});
        m_mainLayer->addChild(pill, 3);
        tagX += pill->getContentSize().width + 5.f;
    }

    std::string description = m_user.description.empty()
        ? loc.getString("modly.no_bio")
        : m_user.description;
    auto bio = SimpleTextArea::create(description, "chatFont.fnt", 0.5f, kBannerW - 8.f);
    bio->setAnchorPoint({0.f, 1.f});
    bio->setMaxLines(2);
    bio->setColor({255, 255, 255, m_user.description.empty() ? GLubyte{140} : GLubyte{215}});
    bio->setPosition({(kWidth - kBannerW) / 2.f + 4.f, kHeight - 126.f});
    m_mainLayer->addChild(bio, 3);

    auto section = CCLabelBMFont::create(
        fmt::format("{} ({})", loc.getString("modly.projects"), m_projects.size()).c_str(),
        "goldFont.fnt");
    section->setScale(0.42f);
    section->setAnchorPoint({0.f, 0.5f});
    section->setPosition({(kWidth - kListW) / 2.f, 148.f});
    m_mainLayer->addChild(section, 3);
}

void ModlyProfilePopup::buildProjects() {
    auto& loc = Localization::get();

    if (m_projects.empty()) {
        auto empty = CCLabelBMFont::create(loc.getString("modly.no_projects").c_str(), "chatFont.fnt");
        empty->setScale(0.5f);
        empty->setOpacity(150);
        empty->setPosition({kWidth / 2.f, 26.f + kListH / 2.f});
        fitLabelWidth(empty, kListW);
        m_mainLayer->addChild(empty, 3);
        return;
    }

    auto& repo = ModlyRepo::get();

    auto scroll = ScrollLayer::create({kListW, kListH});
    scroll->setPosition({(kWidth - kListW) / 2.f, 26.f});
    m_mainLayer->addChild(scroll, 2);

    auto menu = CCMenu::create();
    menu->setPosition(CCPointZero);

    float totalH = std::max(static_cast<float>(m_projects.size()) * kRowH, kListH);
    scroll->m_contentLayer->setContentSize({kListW, totalH});
    scroll->m_contentLayer->setPositionY(kListH - totalH);
    menu->setContentSize({kListW, totalH});
    scroll->m_contentLayer->addChild(menu, 2);

    for (size_t i = 0; i < m_projects.size(); ++i) {
        auto const& mod = m_projects[i];
        float rowY = totalH - static_cast<float>(i + 1) * kRowH;

        auto row = CCNode::create();
        row->setContentSize({kListW, kRowH});

        auto bg = paimon::SpriteHelper::createRoundedRect(
            kListW, kRowH - 2.f, 5.f, {1.f, 1.f, 1.f, i % 2 == 0 ? 0.09f : 0.05f});
        if (bg) {
            bg->setPosition({0.f, 1.f});
            row->addChild(bg, 0);
        }

        auto logo = createAvatar(repo.logoUrl(mod), mod.hasLogo, mod.name, 24.f, 5.f);
        logo->setPosition({20.f, kRowH / 2.f});
        row->addChild(logo, 1);

        auto name = CCLabelBMFont::create(mod.name.c_str(), "bigFont.fnt");
        name->setScale(0.38f);
        name->setAnchorPoint({0.f, 0.5f});
        name->setPosition({40.f, kRowH / 2.f + 6.f});
        fitLabelWidth(name, kListW - 60.f);
        row->addChild(name, 1);

        auto meta = CCLabelBMFont::create(
            fmt::format("{} - v{} - {} {}",
                loc.getString(mod.isPack() ? "modly.type_pack" : "modly.type_mod"),
                mod.version, mod.downloads, loc.getString("modly.downloads")).c_str(),
            "chatFont.fnt");
        meta->setScale(0.4f);
        meta->setAnchorPoint({0.f, 0.5f});
        meta->setOpacity(170);
        meta->setPosition({40.f, kRowH / 2.f - 7.f});
        fitLabelWidth(meta, kListW - 60.f);
        row->addChild(meta, 1);

        auto btn = CCMenuItemSpriteExtra::create(row, this, menu_selector(ModlyProfilePopup::onProject));
        btn->setTag(static_cast<int>(i));
        btn->setPosition({kListW / 2.f, rowY + kRowH / 2.f});
        menu->addChild(btn);
    }

    scroll->moveToTop();

    if (auto* bar = Scrollbar::create(scroll)) {
        bar->setContentSize({8.f, kListH - 8.f});
        bar->setPosition({(kWidth + kListW) / 2.f + 9.f, 26.f + kListH / 2.f});
        m_mainLayer->addChild(bar, 3);
    }
}

void ModlyProfilePopup::onProject(CCObject* sender) {
    auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    int index = btn->getTag();
    if (index < 0 || index >= static_cast<int>(m_projects.size())) return;
    if (auto* popup = ModlyModPopup::create(m_projects[index])) popup->show();
}

ModlyProfilePopup* ModlyProfilePopup::create(ModlyUser const& user) {
    auto ret = new ModlyProfilePopup();
    if (ret->init(user)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

} // namespace paimon::compat_mods
