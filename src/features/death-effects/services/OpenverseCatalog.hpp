#pragma once

#include "DeathEffectManager.hpp"

#include <Geode/Geode.hpp>

#include <cstdint>
#include <string>
#include <vector>

// legal SFX catalogue: searches Openverse (api.openverse.org), which only
// indexes openly licensed audio. No API key is needed for anonymous
// searches, nothing is bundled with the mod, and a sound is downloaded
// only when the user picks it. Every result carries its author and
// creative Commons license, which the manager stores next to the file.
namespace paimon::death_effects::openverse {

struct ResultItem {
    std::string title;
    std::string creator;
    std::string creatorUrl;
    std::string license;
    std::string licenseVersion;
    std::string licenseUrl;
    std::string downloadUrl;
    std::string sourceUrl;
    std::string fileType;
    std::string attribution;
    unsigned int durationMs = 0;

    std::string licenseLabel() const;
    SoundCredit toCredit() const;
};

using SearchCallback = geode::CopyableFunction<
    void(bool ok, std::vector<ResultItem> items, std::string error)>;
using DownloadCallback = geode::CopyableFunction<
    void(bool ok, std::vector<std::uint8_t> bytes, std::string error)>;

void search(std::string const& query, SearchCallback callback);
void download(ResultItem const& item, DownloadCallback callback);

} // namespace paimon::death_effects::openverse
