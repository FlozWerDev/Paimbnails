#include "NowPlayingCard.hpp"
#include "../services/MenuLoopManager.hpp"
#include "../../../utils/SpriteHelper.hpp"
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/utils/string.hpp>
#include <fmt/format.h>

using namespace geode::prelude;
using namespace paimon::menuloop;

NowPlayingCard* NowPlayingCard::create(const std::string& text) {
    auto ret = new NowPlayingCard();
    if (ret && ret->init(text)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

bool NowPlayingCard::init(const std::string& text) {
    if (!CCNode::init()) return false;

    auto screenSize = CCDirector::get()->getWinSize();

    // Background card — preferimos `geode::NineSlice` (la primitiva
    // canónica que usa Geode loader y textureldr) con cadena de fallbacks
    // hacia CCScale9Sprite y, último recurso, CCLayerColor.
    cocos2d::CCNodeRGBA* bg = paimon::SpriteHelper::safeCreateNineSliceFromFile("GJ_square01.png");
    if (!bg) bg = paimon::SpriteHelper::safeCreateNineSliceFromFile("square02_001.png");
    if (!bg) bg = paimon::SpriteHelper::safeCreateScale9("GJ_square01.png");
    if (!bg) bg = paimon::SpriteHelper::safeCreateScale9("square02_001.png");
    if (!bg) {
        // Fallback final: CCLayerColor plana.
        auto layer = CCLayerColor::create({20, 20, 35, 220});
        layer->setContentSize({340.f, 42.f});
        layer->ignoreAnchorPointForPosition(false);
        layer->setAnchorPoint({0.5f, 0.5f});
        this->addChild(layer);
    } else {
        bg->setContentSize({340.f, 42.f});
        bg->setColor({20, 20, 35});
        bg->setOpacity(220);
        bg->setAnchorPoint({0.5f, 0.5f});
        this->addChild(bg);
    }

    // Text label
    auto label = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
    label->setScale(0.55f);
    label->setColor({255, 255, 255});
    label->setAnchorPoint({0.5f, 0.5f});
    label->setPosition({0, 2});
    this->addChild(label);

    this->setContentSize({340.f, 42.f});
    this->setPosition({screenSize.width / 2.f, screenSize.height});
    this->setID("now-playing"_spr);
    this->setZOrder(200);

    // Animate in and out
    auto posx = screenSize.width / 2.f;
    auto posy = screenSize.height;

    auto sequence = cocos2d::CCSequence::create(
        cocos2d::CCEaseInOut::create(cocos2d::CCMoveTo::create(0.8f, {posx, posy - 28.f}), 2.0f),
        cocos2d::CCDelayTime::create(Mod::get()->getSettingValue<double>("menuLoopNotificationTime")),
        cocos2d::CCEaseInOut::create(cocos2d::CCMoveTo::create(0.8f, {posx, posy}), 2.0f),
        cocos2d::CCCallFunc::create(this, callfunc_selector(CCNode::removeFromParent)),
        nullptr
    );
    this->runAction(sequence);

    return true;
}

static std::string formatNGMLSongName(SongInfoObject* songInfo) {
    if (!songInfo) return "Unknown";
    const std::string fmt = Mod::get()->getSavedValue<std::string>("menuLoopSongFormatNGML", "Song Name, Artist, Song ID");
    if (fmt == "Song Name") return songInfo->m_songName;
    if (fmt == "Song Name + Artist") return fmt::format("{} by {}", songInfo->m_songName, songInfo->m_artistName);
    if (fmt == "Song Name + Song ID") return fmt::format("{} ({})", songInfo->m_songName, songInfo->m_songID);
    return fmt::format("{}, {}, {}", songInfo->m_songName, songInfo->m_artistName, songInfo->m_songID);
}

static std::string buildDisplayName() {
    auto& sm = MenuLoopManager::get();
    if (sm.isOriginalMenuLoop()) return "Original Menu Loop by RobTop";
    if (sm.isOverride()) return sm.getCurrentSongDisplayName() + " (CUSTOM OVERRIDE)";
    if (sm.isPreviousSong()) return sm.getCurrentSongDisplayName() + " (PREVIOUS SONG)";

    const std::string& current = sm.getCurrentSong();
    auto path = std::filesystem::path(current);
    auto stem = geode::utils::string::pathToString(path.stem());

    // Try to parse as song ID for NG/ML songs
    auto numRes = geode::utils::numFromString<int>(stem);
    if (numRes.isOk()) {
        int songID = numRes.unwrap();
        if (songID > 0) {
            auto* mdm = MusicDownloadManager::sharedState();
            if (auto* info = mdm->getSongInfoObject(songID)) {
                return formatNGMLSongName(info);
            }
        }
    }

    // Custom song: just filename
    return geode::utils::string::pathToString(path.filename());
}

void NowPlayingCard::showForCurrentSong(cocos2d::CCNode* parent) {
    if (!parent) return;

    // Remove old card
    if (auto old = parent->getChildByIDRecursive("now-playing"_spr)) {
        old->removeMeAndCleanup();
    }

    auto& sm = MenuLoopManager::get();
    if (!Mod::get()->getSettingValue<bool>("menuLoopEnableNotification")) return;

    std::string prefix;
    const std::string& p = Mod::get()->getSettingValue<std::string>("menuLoopCustomPrefix");
    if (p != "[Empty]") prefix = fmt::format("{}: ", p);

    std::string text = prefix + buildDisplayName();

    if (auto card = NowPlayingCard::create(text)) {
        parent->addChild(card);
    }
}
