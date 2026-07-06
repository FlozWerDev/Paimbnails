#include "LevelMetadata.hpp"

#include <Geode/binding/GJGameLevel.hpp>
#include <matjson.hpp>

using namespace geode::prelude;

namespace {
// gd::string -> std::string (Geode provides an implicit conversion, but this
// keeps the intent explicit and avoids dangling temporaries).
inline std::string s(gd::string const& v) { return std::string(v); }
} // namespace

namespace paimon {

std::string collectLevelMetadata(GJGameLevel* level) {
    if (!level) return "";

    auto j = matjson::Value::object();

    // --- Identity ---
    j["levelID"]        = level->m_levelID.value();
    j["levelName"]      = s(level->m_levelName);
    j["creatorName"]    = s(level->m_creatorName);
    j["userID"]         = level->m_userID.value();
    j["accountID"]      = level->m_accountID.value();
    j["originalLevel"]  = level->m_originalLevel.value();
    j["levelType"]      = static_cast<int>(level->m_levelType);

    // --- Description (raw + unpacked) ---
    j["levelDesc"]          = s(level->m_levelDesc);
    j["levelDescUnpacked"]  = s(level->getUnpackedLevelDescription());

    // --- Difficulty / rating ---
    j["difficulty"]        = static_cast<int>(level->m_difficulty);
    j["averageDifficulty"] = level->getAverageDifficulty();
    j["demon"]             = level->m_demon.value();
    j["demonDifficulty"]   = level->m_demonDifficulty;
    j["autoLevel"]         = level->m_autoLevel;
    j["stars"]             = level->m_stars.value();
    j["starsRequested"]    = level->m_starsRequested;
    j["featured"]          = level->m_featured;
    j["isEpic"]            = level->m_isEpic;
    j["ratings"]           = level->m_ratings;
    j["ratingsSum"]        = level->m_ratingsSum;
    j["starRatings"]       = level->m_starRatings;
    j["starRatingsSum"]    = level->m_starRatingsSum;
    j["maxStarRatings"]    = level->m_maxStarRatings;
    j["minStarRatings"]    = level->m_minStarRatings;
    j["demonVotes"]        = level->m_demonVotes;
    j["rateStars"]         = level->m_rateStars;
    j["rateFeature"]       = level->m_rateFeature;
    j["rateUser"]          = s(level->m_rateUser);

    // --- Engagement / community ---
    j["downloads"]   = level->m_downloads;
    j["likes"]       = level->m_likes;
    j["dislikes"]    = level->m_dislikes;

    // --- Length / size / version ---
    j["levelLength"]    = level->m_levelLength;
    j["isPlatformer"]   = level->isPlatformer();
    j["objectCount"]    = level->m_objectCount.value();
    j["levelVersion"]   = level->m_levelVersion;
    j["gameVersion"]    = level->m_gameVersion;
    j["levelRev"]       = level->m_levelRev;
    j["levelIndex"]     = level->m_levelIndex;
    j["levelStringLength"] = static_cast<int64_t>(level->m_levelString.size());

    // --- Song / audio ---
    j["songID"]         = level->m_songID;
    j["audioTrack"]     = level->m_audioTrack;
    j["songName"]       = s(level->getSongName());
    j["audioFileName"]  = s(level->getAudioFileName());
    j["songIDs"]        = s(level->m_songIDs);
    j["sfxIDs"]         = s(level->m_sfxIDs);
    j["songSize"]       = level->m_songSize;

    // --- Coins ---
    j["coins"]              = level->m_coins;
    j["coinsVerified"]      = level->m_coinsVerified.value();
    j["firstCoinVerified"]  = level->m_firstCoinVerified.value();
    j["secondCoinVerified"] = level->m_secondCoinVerified.value();
    j["thirdCoinVerified"]  = level->m_thirdCoinVerified.value();
    j["requiredCoins"]      = level->m_requiredCoins;

    // --- Dates / timestamps ---
    j["uploadDate"]  = s(level->m_uploadDate);
    j["updateDate"]  = s(level->m_updateDate);
    j["timestamp"]   = level->m_timestamp;

    // --- Player progress / records ---
    j["normalPercent"]      = level->m_normalPercent.value();
    j["newNormalPercent2"]  = level->m_newNormalPercent2.value();
    j["practicePercent"]    = level->m_practicePercent;
    j["orbCompletion"]      = level->m_orbCompletion.value();
    j["attempts"]           = level->m_attempts.value();
    j["jumps"]              = level->m_jumps.value();
    j["clicks"]             = level->m_clicks.value();
    j["attemptTime"]        = level->m_attemptTime.value();
    j["bestTime"]           = level->m_bestTime;
    j["bestPoints"]         = level->m_bestPoints;
    j["recordString"]       = s(level->m_recordString);

    // --- Daily / weekly / gauntlet ---
    j["dailyID"]        = level->m_dailyID.value();
    j["gauntletLevel"]  = level->m_gauntletLevel;
    j["gauntletLevel2"] = level->m_gauntletLevel2;
    j["listPosition"]   = level->m_listPosition;

    // --- Flags / state ---
    j["unlisted"]               = level->m_unlisted;
    j["friendsOnly"]            = level->m_friendsOnly;
    j["isEditable"]             = level->m_isEditable;
    j["isUploaded"]             = level->m_isUploaded;
    j["isVerified"]             = level->m_isVerifiedRaw;
    j["hasBeenModified"]        = level->m_hasBeenModified;
    j["twoPlayerMode"]          = level->m_twoPlayerMode;
    j["lowDetailMode"]          = level->m_lowDetailMode;
    j["lowDetailModeToggled"]   = level->m_lowDetailModeToggled;
    j["levelFavorited"]         = level->m_levelFavorited;
    j["levelFolder"]            = level->m_levelFolder;
    j["isCompletionLegitimate"] = level->m_isCompletionLegitimate;
    j["levelNotDownloaded"]     = level->m_levelNotDownloaded;
    j["isUnlocked"]             = level->m_isUnlocked;
    j["highObjectsEnabled"]     = level->m_highObjectsEnabled;
    j["unlimitedObjectsEnabled"] = level->m_unlimitedObjectsEnabled;
    j["workingTime"]            = level->m_workingTime;
    j["workingTime2"]           = level->m_workingTime2;

    return j.dump(matjson::NO_INDENTATION);
}

} // namespace paimon
