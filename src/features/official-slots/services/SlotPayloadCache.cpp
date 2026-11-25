#include "SlotPayloadCache.hpp"
#include "../../../utils/AtomicFileWrite.hpp"
#include "../../../utils/JsonHelper.hpp"

#include <fstream>
#include <matjson.hpp>

namespace paimon::officialslots {
namespace {
constexpr std::uintmax_t kMaxPayloadFileBytes = 64ull * 1024 * 1024;

std::filesystem::path payloadPath(std::filesystem::path const& directory, int levelId) {
    return directory / (std::to_string(levelId) + ".json");
}
} // namespace

std::optional<CachedSlotPayload> readSlotPayload(std::filesystem::path const& directory, int levelId) {
    if (levelId <= 0) return std::nullopt;
    auto const path = payloadPath(directory, levelId);
    std::error_code error;
    auto const size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > kMaxPayloadFileBytes) return std::nullopt;
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;
    std::string text(static_cast<std::size_t>(size), '\0');
    if (!input.read(text.data(), static_cast<std::streamsize>(size))) return std::nullopt;
    auto parsed = matjson::parse(text);
    if (!parsed || !parsed.unwrap().isObject()) return std::nullopt;
    auto const& json = parsed.unwrap();
    if (paimon::json::integerOr<int>(json["version"]) != 1 ||
        paimon::json::integerOr<int>(json["levelId"]) != levelId) return std::nullopt;
    CachedSlotPayload result;
    result.levelId = levelId;
    result.levelString = json["levelString"].asString().unwrapOr("");
    if (result.levelString.empty()) return std::nullopt;
    result.audioTrack = paimon::json::integerOr<int>(json["audioTrack"]);
    result.songId = paimon::json::integerOr<int>(json["songId"]);
    result.songIds = json["songIds"].asString().unwrapOr("");
    result.sfxIds = json["sfxIds"].asString().unwrapOr("");
    result.levelLength = paimon::json::integerOr<int>(json["levelLength"]);
    result.name = json["name"].asString().unwrapOr("");
    result.author = json["author"].asString().unwrapOr("");
    result.difficulty = paimon::json::integerOr<int>(json["difficulty"]);
    result.ratings = paimon::json::integerOr<int>(json["ratings"]);
    result.ratingsSum = paimon::json::integerOr<int>(json["ratingsSum"]);
    result.demon = paimon::json::integerOr<int>(json["demon"]);
    result.demonDifficulty = paimon::json::integerOr<int>(json["demonDifficulty"]);
    result.autoLevel = json["autoLevel"].asBool().unwrapOr(false);
    result.stars = paimon::json::integerOr<int>(json["stars"]);
    result.coins = paimon::json::integerOr<int>(json["coins"]);
    result.featured = paimon::json::integerOr<int>(json["featured"]);
    result.epic = paimon::json::integerOr<int>(json["epic"]);
    return result;
}

bool writeSlotPayload(std::filesystem::path const& directory, CachedSlotPayload const& payload) {
    if (payload.levelId <= 0 || payload.levelString.empty()) return false;
    auto json = matjson::makeObject({
        {"version", 1}, {"levelId", payload.levelId}, {"levelString", payload.levelString},
        {"audioTrack", payload.audioTrack}, {"songId", payload.songId},
        {"songIds", payload.songIds}, {"sfxIds", payload.sfxIds},
        {"levelLength", payload.levelLength}, {"name", payload.name}, {"author", payload.author},
        {"difficulty", payload.difficulty}, {"ratings", payload.ratings}, {"ratingsSum", payload.ratingsSum},
        {"demon", payload.demon}, {"demonDifficulty", payload.demonDifficulty}, {"autoLevel", payload.autoLevel},
        {"stars", payload.stars}, {"coins", payload.coins}, {"featured", payload.featured}, {"epic", payload.epic},
    });
    auto text = json.dump();
    if (text.size() > kMaxPayloadFileBytes) return false;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) return false;
    return paimon::file::writeAtomically(payloadPath(directory, payload.levelId), text);
}

} // namespace paimon::officialslots
