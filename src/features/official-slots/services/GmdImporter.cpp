#include "GmdImporter.hpp"

#include <Geode/Geode.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/general.hpp>
#include <Geode/utils/string.hpp>

using namespace geode::prelude;

namespace paimon::officialslots {

namespace {

// values sit right after their <k>key</k>; scanning for that pair avoids
// parsing the whole document, most of which is the level string.
std::optional<std::string> valueAfterKey(
    std::string const& xml, std::string_view key, std::string_view openTag,
    std::string_view closeTag
) {
    auto keyPattern = fmt::format("<k>{}</k>", key);
    auto keyPos = xml.find(keyPattern);
    if (keyPos == std::string::npos) return std::nullopt;

    auto valueStart = xml.find(openTag, keyPos + keyPattern.size());
    if (valueStart == std::string::npos) return std::nullopt;

    // the value has to be the next tag, otherwise we are reading a later key's.
    auto nextKey = xml.find("<k>", keyPos + keyPattern.size());
    if (nextKey != std::string::npos && nextKey < valueStart) return std::nullopt;

    valueStart += openTag.size();
    auto valueEnd = xml.find(closeTag, valueStart);
    if (valueEnd == std::string::npos) return std::nullopt;

    return xml.substr(valueStart, valueEnd - valueStart);
}

std::optional<std::string> stringValue(std::string const& xml, std::string_view key) {
    return valueAfterKey(xml, key, "<s>", "</s>");
}

int intValue(std::string const& xml, std::string_view key) {
    auto raw = valueAfterKey(xml, key, "<i>", "</i>");
    if (!raw) return 0;
    auto parsed = numFromString<int>(*raw);
    return parsed ? parsed.unwrap() : 0;
}

std::string decodeEntities(std::string text) {
    static std::pair<std::string_view, std::string_view> const kEntities[] = {
        {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""},
        {"&apos;", "'"}, {"&amp;", "&"},
    };
    for (auto const& [entity, replacement] : kEntities) {
        size_t pos = 0;
        while ((pos = text.find(entity, pos)) != std::string::npos) {
            text.replace(pos, entity.size(), replacement);
            pos += replacement.size();
        }
    }
    return text;
}

} // namespace

std::optional<GmdInfo> readGmdInfo(std::filesystem::path const& path) {
    auto contents = utils::file::readString(path);
    if (!contents) {
        log::warn("[OfficialSlots] Could not read the .gmd: {}", contents.unwrapErr());
        return std::nullopt;
    }

    auto const& xml = contents.unwrap();
    if (xml.find("<k>") == std::string::npos) {
        log::warn("[OfficialSlots] That file does not look like a .gmd");
        return std::nullopt;
    }

    GmdInfo info;
    if (auto name = stringValue(xml, "k2")) info.name = decodeEntities(*name);

    // k5 is the author on downloaded levels; locally made ones leave it empty.
    if (auto author = stringValue(xml, "k5")) info.author = decodeEntities(*author);

    info.songId = intValue(xml, "k45");

    if (info.name.empty()) {
        info.name = utils::string::pathToString(path.stem());
    }
    return info;
}

std::string readGmdLevelString(std::filesystem::path const& path) {
    auto contents = utils::file::readString(path);
    if (!contents) {
        log::warn("[OfficialSlots] Could not read the .gmd for its level string: {}",
                  contents.unwrapErr());
        return {};
    }

    // same <s>text</s> pair as every other key; k4 is just much longer.
    auto raw = stringValue(contents.unwrap(), "k4");
    if (!raw || raw->empty()) return {};

    // base64 carries no entities, but decoding is harmless and keeps .gmd files
    // with a raw (uncompressed) level string working too.
    return decodeEntities(*raw);
}

} // namespace paimon::officialslots
