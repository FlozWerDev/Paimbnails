#include "ModlyModPopup.hpp"
#include "ModlyCommentsPopup.hpp"
#include "ModlyProfilePopup.hpp"
#include "ModlyUIHelpers.hpp"
#include "../services/ModlyRepo.hpp"
#include "../../mod-previews/ui/ModPreviewGalleryPopup.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/Localization.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/ui/Scrollbar.hpp>
#include <Geode/ui/TextArea.hpp>
#include <Geode/utils/web.hpp>

using namespace geode::prelude;

namespace paimon::compat_mods {

namespace {
    constexpr float kWidth = 440.f;
    constexpr float kHeight = 300.f;
    constexpr float kScrollW = 396.f;
    constexpr float kScrollH = 132.f;
    constexpr float kPreviewW = 78.f;
    constexpr float kPreviewH = 44.f;
}

bool ModlyModPopup::init(ModlyMod const& mod) {
    if (!Popup::init(kWidth, kHeight)) return false;
    paimon::markDynamicPopup(this);

    m_mod = mod;
    this->setTitle(m_mod.name.c_str());

    buildHeader();
    buildBody();
    buildButtons();
    return true;
}

void ModlyModPopup::buildHeader() {
    auto& repo = ModlyRepo::get();
    auto& loc = Localization::get();

    auto logo = createAvatar(repo.logoUrl(m_mod), m_mod.hasLogo, m_mod.name, 44.f, 9.f);
    logo->setPosition({36.f, kHeight - 66.f});
    m_mainLayer->addChild(logo, 2);

    float textX = 66.f;
    float rowY = kHeight - 54.f;

    auto version = CCLabelBMFont::create(fmt::format("v{}", m_mod.version).c_str(), "goldFont.fnt");
    version->setScale(0.42f);
    version->setAnchorPoint({0.f, 0.5f});
    version->setPosition({textX, rowY});
    m_mainLayer->addChild(version, 2);

    float pillX = textX + version->getContentSize().width * 0.42f + 8.f;

    auto addPill = [&](std::string const& text, ccColor3B color) {
        auto pill = createPill(text, color);
        pill->setPosition({pillX, rowY});
        m_mainLayer->addChild(pill, 2);
        pillX += pill->getContentSize().width + 5.f;
    };

    if (m_mod.state == "alpha") addPill("ALPHA", {235, 120, 60});
    else if (m_mod.state == "beta") addPill("BETA", {120, 110, 235});
    if (!m_mod.gdps.empty()) addPill(fmt::format("GDPS: {}", m_mod.gdps), {60, 160, 180});
    if (m_mod.isPack()) addPill(loc.getString("modly.type_pack"), {180, 110, 190});

    // Author shortcut: avatar plus name, opening the developer profile.
    auto const* author = repo.user(m_mod.authorUid);
    std::string authorName = author && !author->name.empty() ? author->name : m_mod.authorName;
    if (authorName.empty()) authorName = loc.getString("modly.unknown_author");

    auto authorMenu = CCMenu::create();
    authorMenu->setPosition(CCPointZero);
    authorMenu->setContentSize({kWidth, kHeight});
    m_mainLayer->addChild(authorMenu, 3);

    auto authorRow = CCNode::create();
    float avatarSize = 18.f;
    float rowMid = avatarSize / 2.f;

    auto avatar = createAvatar(
        author ? repo.photoUrl(*author) : "",
        author && author->hasPhoto,
        authorName, avatarSize);
    avatar->setPosition({rowMid, rowMid});
    authorRow->addChild(avatar, 1);

    auto nameLabel = CCLabelBMFont::create(
        fmt::format("{} {}", loc.getString("modly.by"), authorName).c_str(), "bigFont.fnt");
    nameLabel->setScale(0.36f);
    nameLabel->setAnchorPoint({0.f, 0.5f});
    nameLabel->setPosition({avatarSize + 5.f, rowMid});
    fitLabelWidth(nameLabel, 150.f);
    authorRow->addChild(nameLabel, 1);

    float rowW = avatarSize + 5.f + nameLabel->getScaledContentSize().width;
    if (author) {
        if (auto* seal = createRankSeal(*author, 13.f)) {
            seal->setPosition({rowW + 4.f, rowMid});
            authorRow->addChild(seal, 1);
            rowW += 17.f;
        }
    }
    authorRow->setContentSize({rowW, avatarSize});

    auto authorBtn = CCMenuItemSpriteExtra::create(authorRow, this, menu_selector(ModlyModPopup::onAuthor));
    authorBtn->setAnchorPoint({0.f, 0.5f});
    authorBtn->setPosition({textX, kHeight - 78.f});
    authorBtn->setEnabled(author != nullptr);
    authorMenu->addChild(authorBtn);

    auto meta = CCLabelBMFont::create(
        fmt::format("{} - {} {}",
            formatModlyDate(m_mod.date),
            m_mod.downloads,
            loc.getString("modly.downloads")).c_str(),
        "chatFont.fnt");
    meta->setScale(0.5f);
    meta->setAnchorPoint({1.f, 0.5f});
    meta->setOpacity(190);
    meta->setPosition({kWidth - 24.f, kHeight - 78.f});
    m_mainLayer->addChild(meta, 2);
}

void ModlyModPopup::buildBody() {
    auto& loc = Localization::get();

    auto scroll = ScrollLayer::create({kScrollW, kScrollH});
    scroll->setPosition({(kWidth - kScrollW) / 2.f, 68.f});
    m_mainLayer->addChild(scroll, 2);

    auto content = scroll->m_contentLayer;
    float innerW = kScrollW - 16.f;
    float y = 0.f;

    auto* previews = buildPreviewStrip(innerW);
    if (previews) {
        y += previews->getContentSize().height + 10.f;
    }

    std::string description = m_mod.description.empty()
        ? loc.getString("modly.no_description")
        : m_mod.description;
    auto text = SimpleTextArea::create(description, "chatFont.fnt", 0.52f, innerW);
    text->setAnchorPoint({0.f, 0.f});
    text->setColor({255, 255, 255, m_mod.description.empty() ? GLubyte{150} : GLubyte{225}});

    float textH = text->getContentSize().height;
    float totalH = std::max(y + textH + 12.f, kScrollH);

    content->setContentSize({kScrollW, totalH});
    content->setPositionY(kScrollH - totalH);

    text->setPosition({8.f, totalH - textH - 6.f});
    content->addChild(text);

    if (previews) {
        previews->setPosition({8.f, totalH - textH - 16.f - previews->getContentSize().height});
        content->addChild(previews);
    }

    scroll->moveToTop();

    if (auto* bar = Scrollbar::create(scroll)) {
        bar->setContentSize({8.f, kScrollH - 8.f});
        bar->setPosition({kWidth / 2.f + kScrollW / 2.f + 9.f, 68.f + kScrollH / 2.f});
        m_mainLayer->addChild(bar, 3);
    }
}

CCNode* ModlyModPopup::buildPreviewStrip(float width) {
    if (m_mod.previewCount <= 0) return nullptr;

    auto strip = CCNode::create();
    auto menu = CCMenu::create();
    menu->setPosition(CCPointZero);

    float x = 0.f;
    int shown = 0;
    for (int i = 1; i <= m_mod.previewCount; ++i) {
        if (x + kPreviewW > width) break;

        auto slot = createImageSlot(ModlyRepo::get().previewUrl(m_mod, i),
                                    kPreviewW, kPreviewH, 5.f, {18, 20, 30, 255});
        auto btn = CCMenuItemSpriteExtra::create(slot, this, menu_selector(ModlyModPopup::onPreview));
        btn->setTag(i);
        btn->setPosition({x + kPreviewW / 2.f, kPreviewH / 2.f});
        menu->addChild(btn);

        x += kPreviewW + 6.f;
        ++shown;
    }

    menu->setContentSize({x, kPreviewH});
    strip->addChild(menu);

    // Tells the reader there are more shots than the row can hold.
    if (shown < m_mod.previewCount) {
        auto more = CCLabelBMFont::create(fmt::format("+{}", m_mod.previewCount - shown).c_str(), "bigFont.fnt");
        more->setScale(0.32f);
        more->setAnchorPoint({0.f, 0.5f});
        more->setOpacity(170);
        more->setPosition({x + 2.f, kPreviewH / 2.f});
        strip->addChild(more);
    }

    strip->setContentSize({width, kPreviewH});
    return strip;
}

void ModlyModPopup::buildButtons() {
    auto& loc = Localization::get();

    auto menu = CCMenu::create();
    menu->setPosition({kWidth / 2.f, 38.f});
    m_mainLayer->addChild(menu, 4);

    std::vector<CCNode*> buttons;

    auto downloadSpr = ButtonSprite::create(
        loc.getString("modly.download").c_str(), "goldFont.fnt", "GJ_button_01.png", 0.7f);
    buttons.push_back(CCMenuItemSpriteExtra::create(downloadSpr, this, menu_selector(ModlyModPopup::onDownload)));

    if (!m_mod.repo.empty()) {
        auto spr = ButtonSprite::create("GitHub", "bigFont.fnt", "GJ_button_04.png", 0.6f);
        buttons.push_back(CCMenuItemSpriteExtra::create(spr, this, menu_selector(ModlyModPopup::onRepo)));
    }
    if (!m_mod.discord.empty()) {
        auto spr = ButtonSprite::create("Discord", "bigFont.fnt", "GJ_button_04.png", 0.6f);
        buttons.push_back(CCMenuItemSpriteExtra::create(spr, this, menu_selector(ModlyModPopup::onDiscord)));
    }
    if (!m_mod.kofi.empty()) {
        auto spr = ButtonSprite::create("Ko-fi", "bigFont.fnt", "GJ_button_05.png", 0.6f);
        buttons.push_back(CCMenuItemSpriteExtra::create(spr, this, menu_selector(ModlyModPopup::onKofi)));
    }

    auto commentsSpr = ButtonSprite::create(
        loc.getString("modly.comments").c_str(), "bigFont.fnt", "GJ_button_02.png", 0.6f);
    buttons.push_back(CCMenuItemSpriteExtra::create(commentsSpr, this, menu_selector(ModlyModPopup::onComments)));

    float gap = 8.f;
    float total = 0.f;
    for (auto* btn : buttons) total += btn->getContentSize().width;
    total += gap * static_cast<float>(buttons.size() - 1);

    float x = -total / 2.f;
    for (auto* btn : buttons) {
        float w = btn->getContentSize().width;
        btn->setPosition({x + w / 2.f, 0.f});
        menu->addChild(btn);
        x += w + gap;
    }
}

void ModlyModPopup::onPreview(CCObject* sender) {
    auto* btn = typeinfo_cast<CCMenuItemSpriteExtra*>(sender);
    if (!btn) return;
    auto* gallery = paimon::mod_previews::ModPreviewGalleryPopup::create(
        btn->getTag(), m_mod.previewCount, ModlyRepo::get().previewUrlBase(m_mod));
    if (gallery) gallery->show();
}

void ModlyModPopup::onAuthor(CCObject*) {
    auto const* author = ModlyRepo::get().user(m_mod.authorUid);
    if (!author) return;
    if (auto* popup = ModlyProfilePopup::create(*author)) popup->show();
}

void ModlyModPopup::onComments(CCObject*) {
    if (auto* popup = ModlyCommentsPopup::create(m_mod)) popup->show();
}

void ModlyModPopup::onDownload(CCObject*) {
    if (!m_mod.link.empty()) web::openLinkInBrowser(m_mod.link);
}

void ModlyModPopup::onRepo(CCObject*) {
    if (!m_mod.repo.empty()) web::openLinkInBrowser(m_mod.repo);
}

void ModlyModPopup::onDiscord(CCObject*) {
    if (!m_mod.discord.empty()) web::openLinkInBrowser(m_mod.discord);
}

void ModlyModPopup::onKofi(CCObject*) {
    if (!m_mod.kofi.empty()) web::openLinkInBrowser(m_mod.kofi);
}

ModlyModPopup* ModlyModPopup::create(ModlyMod const& mod) {
    auto ret = new ModlyModPopup();
    if (ret->init(mod)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

} // namespace paimon::compat_mods
