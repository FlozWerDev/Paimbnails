#include "../../../utils/FilesystemPath.hpp"
#include "NowPlayingCard.hpp"
#include "../services/MenuLoopManager.hpp"
#include "../../menu-music/ui/components/NowPlayingToast.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include <Geode/binding/MusicDownloadManager.hpp>
#include <Geode/utils/string.hpp>
#include <fmt/format.h>

using namespace geode::prelude;
using namespace paimon::menuloop;

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
    auto stem = paimon::paths::toUtf8(path.stem());

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

    return paimon::paths::toUtf8(path.filename());
}

void NowPlayingCard::showForCurrentSong(cocos2d::CCNode* parent) {
    if (!parent) return;
    if (!paimon::modules::isEnabled("paimbnails.menuloop.menu")) return;
    if (!Mod::get()->getSettingValue<bool>("menuLoopEnableNotification")) return;

    paimon::menumusic::NowPlayingToastContent content;
    const std::string prefix = Mod::get()->getSettingValue<std::string>("menuLoopCustomPrefix");
    if (prefix != "[Empty]") content.prefix = prefix;
    content.title = buildDisplayName();
    content.subtitle = "Menu Loop";
    paimon::menumusic::NowPlayingToast::showContent(parent, content);
}
