#pragma once

#include <Geode/utils/string.hpp>
#include <string>

// Port of Mod-Previews by Alphalaneous to Geode v5.

namespace paimon::mod_previews {

struct RepoData {
    bool valid = false;
    std::string rawURL;
};

inline RepoData getRepoData(std::string url) {
    using namespace geode::utils::string;
    toLowerIP(url);

    auto parts = split(url, "://");
    if (parts.size() < 2) return {};

    std::string post = parts[1];
    if (post.rfind("www.", 0) == 0) post.erase(0, 4);

    auto slash = split(post, "/");
    if (slash.size() < 2) return {};

    std::string platform = slash[0];
    std::string repo = post.substr(platform.size() + 1);
    while (!repo.empty() && repo.back() == '/') repo.pop_back();
    if (repo.size() > 4 && repo.substr(repo.size() - 4) == ".git") repo.erase(repo.size() - 4);

    RepoData data;
    if (platform == "github.com") {
        data.rawURL = "https://raw.githubusercontent.com/" + repo;
        data.valid = true;
    } else if (platform == "codeberg.org") {
        data.rawURL = "https://codeberg.org/" + repo + "/raw/branch";
        data.valid = true;
    }
    return data;
}

} // namespace paimon::mod_previews
