#include "MusicTagsPopup.hpp"

#include "NewgroundsBrowserPopup.hpp"
#include "../services/MenuMusicLibrary.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include "../../../ui/PaimonUI.hpp"

#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/MusicBrowser.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/ui/Notification.hpp>

using namespace geode::prelude;

namespace paimon::menumusic {

MusicTagsPopup* MusicTagsPopup::create() {
    auto ret = new MusicTagsPopup();
    if (ret && ret->init(410.f, 230.f)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool MusicTagsPopup::init(float width, float height) {
    if (!PaimonPopup::init(width, height)) return false;
    paimon::markDynamicPopup(this);
    this->setTitle("Music Browser");
    this->addInfoButton("Music Browser",
        "Pick a catalog to explore. <cg>All Songs</c> opens the full browser; "
        "<co>Tags</c> jumps straight to the genre/tag filters. <cy>Newgrounds</c> "
        "lets you search and download songs without leaving the mod.");

    auto size = m_mainLayer->getContentSize();
    auto subtitle = paimon::ui::makeText(
        "Choose a catalog, then browse everything or filter by tags.",
        size.width - 40.f, 0.43f, paimon::ui::palette::muted, kCCTextAlignmentCenter);
    if (subtitle) {
        subtitle->setPosition({size.width / 2.f, size.height - 42.f});
        m_mainLayer->addChild(subtitle, 3);
    }

    auto addNativeCard = [this](
        float x,
        char const* titleText,
        char const* description,
        SEL_MenuHandler allHandler,
        SEL_MenuHandler tagsHandler,
        char const* id
    ) {
        constexpr float cardW = 184.f;
        constexpr float cardH = 72.f;
        auto card = paimon::ui::makePanel({cardW, cardH}, titleText);
        if (!card) return;
        card->setPosition({x, 84.f});
        card->setID(id);

        auto detail = paimon::ui::makeText(description, cardW - 16.f, 0.34f,
            paimon::ui::palette::muted, kCCTextAlignmentCenter);
        if (detail) {
            detail->setPosition({cardW / 2.f, cardH - paimon::ui::kPanelHeader - 8.f});
            card->addChild(detail, 2);
        }

        auto menu = CCMenu::create();
        menu->setPosition({cardW / 2.f, 16.f});
        if (auto* button = paimon::ui::makeButton("All Songs",
                [this, allHandler] { (this->*allHandler)(nullptr); },
                paimon::ui::Btn::Green, 80.f, 0.42f, "bigFont.fnt")) {
            button->setPosition({-44.f, 0.f});
            menu->addChild(button);
        }
        if (auto* button = paimon::ui::makeButton("Tags",
                [this, tagsHandler] { (this->*tagsHandler)(nullptr); },
                paimon::ui::Btn::Cyan, 70.f, 0.42f, "bigFont.fnt")) {
            button->setPosition({44.f, 0.f});
            menu->addChild(button);
        }
        card->addChild(menu, 3);
        m_mainLayer->addChild(card, 3);
    };

    addNativeCard(
        15.f, "GD Library", "Geometry Dash music library",
        menu_selector(MusicTagsPopup::onGeometryDashAll),
        menu_selector(MusicTagsPopup::onGeometryDashTags),
        "gd-library-card"_spr
    );
    addNativeCard(
        211.f, "NCS", "NoCopyrightSounds catalog",
        menu_selector(MusicTagsPopup::onNCSAll),
        menu_selector(MusicTagsPopup::onNCSTags),
        "ncs-card"_spr
    );

    auto newgroundsCard = paimon::ui::makePanel({380.f, 58.f}, nullptr);
    if (newgroundsCard) {
        newgroundsCard->setPosition({15.f, 20.f});
        newgroundsCard->setID("newgrounds-card"_spr);

        auto title = paimon::ui::makeTitle("Newgrounds", 160.f, 0.46f);
        if (title) {
            title->setAnchorPoint({0.f, 0.5f});
            title->setPosition({14.f, 40.f});
            newgroundsCard->addChild(title, 2);
        }
        auto detail = paimon::ui::makeLabel(
            "Latest songs, genre tags and search inside the mod",
            300.f, 0.34f, paimon::ui::palette::muted);
        if (detail) {
            detail->setAnchorPoint({0.f, 0.5f});
            detail->setPosition({14.f, 20.f});
            newgroundsCard->addChild(detail, 2);
        }

        auto menu = CCMenu::create();
        menu->setPosition({328.f, 29.f});
        if (auto* button = paimon::ui::makeButton("Browse",
                [this] { this->onNewgrounds(nullptr); },
                paimon::ui::Btn::Pink, 88.f, 0.48f, "bigFont.fnt")) {
            menu->addChild(button);
        }
        newgroundsCard->addChild(menu, 3);
        m_mainLayer->addChild(newgroundsCard, 3);
    }

    return true;
}

void MusicTagsPopup::musicBrowserClosed(MusicBrowser* browser) {
    if (auto ref = m_browser.lock()) {
        if (typeinfo_cast<MusicBrowser*>(ref.data()) == browser) m_browser = nullptr;
    }
    MenuMusicLibrary::get().syncDownloadedSongs();
}

void MusicTagsPopup::onExit() {
    if (auto ref = m_browser.lock()) {
        if (auto* browser = typeinfo_cast<MusicBrowser*>(ref.data())) {
            if (browser->m_delegate == this) browser->m_delegate = nullptr;
        }
    }
    Popup::onExit();
}

void MusicTagsPopup::openMusicBrowser(GJSongType type, bool showTags) {
    auto browser = MusicBrowser::create(0, type);
    if (!browser) {
        Notification::create("Could not open the music browser.", NotificationIcon::Error)->show();
        return;
    }
    browser->m_delegate = this;
    m_browser = WeakRef<CCNode>(browser);
    browser->show();

    if (!showTags) return;
    auto weakBrowser = WeakRef<CCNode>(browser);
    Loader::get()->queueInMainThread([weakBrowser] {
        auto ref = weakBrowser.lock();
        auto liveBrowser = ref ? typeinfo_cast<MusicBrowser*>(ref.data()) : nullptr;
        if (liveBrowser && liveBrowser->m_searchResult) {
            liveBrowser->onTagFilters(nullptr);
        }
    });
}

void MusicTagsPopup::onGeometryDashAll(CCObject*) {
    openMusicBrowser(GJSongType::Music, false);
}

void MusicTagsPopup::onGeometryDashTags(CCObject*) {
    openMusicBrowser(GJSongType::Music, true);
}

void MusicTagsPopup::onNCSAll(CCObject*) {
    openMusicBrowser(GJSongType::NCS, false);
}

void MusicTagsPopup::onNCSTags(CCObject*) {
    openMusicBrowser(GJSongType::NCS, true);
}

void MusicTagsPopup::onNewgrounds(CCObject*) {
    if (auto popup = NewgroundsBrowserPopup::create()) popup->show();
}

} // namespace paimon::menumusic
