#include "MusicTagsPopup.hpp"

#include "NewgroundsBrowserPopup.hpp"
#include "../services/MenuMusicLibrary.hpp"
#include "../../../utils/DynamicPopupRegistry.hpp"
#include "../../../utils/SpriteHelper.hpp"

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
    if (!Popup::init(width, height)) return false;
    paimon::markDynamicPopup(this);
    this->setTitle("Music Browser");

    auto size = m_mainLayer->getContentSize();
    auto subtitle = CCLabelBMFont::create(
        "Choose a catalog, then browse everything or filter by tags.",
        "chatFont.fnt"
    );
    if (subtitle) {
        subtitle->setScale(0.43f);
        subtitle->setPosition({size.width / 2.f, size.height - 42.f});
        subtitle->setColor({210, 220, 240});
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
        constexpr float cardH = 70.f;
        auto card = paimon::SpriteHelper::createDarkPanel(cardW, cardH, 150, 7.f);
        if (!card) return;
        card->setAnchorPoint({0.f, 0.f});
        card->setPosition({x, 86.f});
        card->setID(id);

        auto title = CCLabelBMFont::create(titleText, "goldFont.fnt");
        if (title) {
            title->setScale(0.48f);
            title->setPosition({cardW / 2.f, 55.f});
            card->addChild(title, 2);
        }

        auto detail = CCLabelBMFont::create(description, "chatFont.fnt");
        if (detail) {
            detail->setScale(0.34f);
            detail->setPosition({cardW / 2.f, 39.f});
            detail->setColor({185, 200, 225});
            card->addChild(detail, 2);
        }

        auto menu = CCMenu::create();
        menu->setPosition({cardW / 2.f, 18.f});
        auto allSprite = ButtonSprite::create(
            "All Songs", 76, true, "bigFont.fnt", "GJ_button_01.png", 18.f, 0.42f
        );
        if (allSprite) {
            auto button = CCMenuItemSpriteExtra::create(allSprite, this, allHandler);
            button->setPosition({-43.f, 0.f});
            menu->addChild(button);
        }
        auto tagsSprite = ButtonSprite::create(
            "Tags", 68, true, "bigFont.fnt", "GJ_button_04.png", 18.f, 0.42f
        );
        if (tagsSprite) {
            auto button = CCMenuItemSpriteExtra::create(tagsSprite, this, tagsHandler);
            button->setPosition({43.f, 0.f});
            menu->addChild(button);
        }
        card->addChild(menu, 3);
        m_mainLayer->addChild(card, 3);
    };

    addNativeCard(
        15.f, "GD Library", "Geometry Dash music library",
        menu_selector(MusicTagsPopup::onGeometryDashAll),
        menu_selector(MusicTagsPopup::onGeometryDashTags),
        "gd-library-card"
    );
    addNativeCard(
        211.f, "NCS", "NoCopyrightSounds catalog",
        menu_selector(MusicTagsPopup::onNCSAll),
        menu_selector(MusicTagsPopup::onNCSTags),
        "ncs-card"
    );

    auto newgroundsCard = paimon::SpriteHelper::createDarkPanel(380.f, 55.f, 150, 7.f);
    if (newgroundsCard) {
        newgroundsCard->setAnchorPoint({0.f, 0.f});
        newgroundsCard->setPosition({15.f, 21.f});
        newgroundsCard->setID("newgrounds-card");

        auto title = CCLabelBMFont::create("Newgrounds", "goldFont.fnt");
        if (title) {
            title->setScale(0.46f);
            title->setAnchorPoint({0.f, 0.5f});
            title->setPosition({14.f, 36.f});
            newgroundsCard->addChild(title, 2);
        }
        auto detail = CCLabelBMFont::create(
            "Latest songs, genre tags and search inside the mod",
            "chatFont.fnt"
        );
        if (detail) {
            detail->setScale(0.34f);
            detail->setAnchorPoint({0.f, 0.5f});
            detail->setPosition({14.f, 17.f});
            detail->setColor({185, 200, 225});
            newgroundsCard->addChild(detail, 2);
        }

        auto sprite = ButtonSprite::create(
            "Browse", 85, true, "bigFont.fnt", "GJ_button_05.png", 20.f, 0.48f
        );
        if (sprite) {
            auto button = CCMenuItemSpriteExtra::create(
                sprite, this, menu_selector(MusicTagsPopup::onNewgrounds)
            );
            auto menu = CCMenu::create();
            menu->setPosition({328.f, 27.f});
            menu->addChild(button);
            newgroundsCard->addChild(menu, 3);
        }
        m_mainLayer->addChild(newgroundsCard, 3);
    }

    return true;
}

void MusicTagsPopup::musicBrowserClosed(MusicBrowser*) {
    MenuMusicLibrary::get().syncDownloadedSongs();
}

void MusicTagsPopup::openMusicBrowser(GJSongType type, bool showTags) {
    auto browser = MusicBrowser::create(0, type);
    if (!browser) {
        Notification::create("Could not open the music browser.", NotificationIcon::Error)->show();
        return;
    }
    browser->m_delegate = this;
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
