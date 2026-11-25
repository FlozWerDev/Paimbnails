#pragma once

#include "ForumApi.hpp"
#include "../../../utils/Localization.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/GJSearchObject.hpp>
#include <Geode/binding/LevelBrowserLayer.hpp>
#include <Geode/binding/ProfilePage.hpp>
#include <Geode/binding/SimplePlayer.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace paimon::forum {

inline std::string toLowerAscii(std::string s) {
    for (auto& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

inline std::string trimAscii(std::string_view s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return std::string(s.substr(start, end - start));
}

inline std::string normalizeTagKey(std::string const& tag) {
    return toLowerAscii(trimAscii(tag));
}

inline std::string displayTag(std::string const& tag) {
    std::string out = trimAscii(tag);
    if (out.empty()) return out;
    bool hasUpper = false;
    for (char c : out) {
        if (std::isupper(static_cast<unsigned char>(c))) {
            hasUpper = true;
            break;
        }
    }
    if (!hasUpper && std::islower(static_cast<unsigned char>(out[0]))) {
        out[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[0])));
    }
    return out;
}

inline std::vector<std::string> getBookmarks() {
    return geode::Mod::get()->getSavedValue<std::vector<std::string>>("forum-bookmarks", {});
}

inline bool isBookmarked(std::string const& postId) {
    if (postId.empty()) return false;
    auto list = getBookmarks();
    return std::find(list.begin(), list.end(), postId) != list.end();
}

inline bool toggleBookmark(std::string const& postId) {
    if (postId.empty()) return false;
    auto list = getBookmarks();
    auto it = std::find(list.begin(), list.end(), postId);
    bool nowSaved = false;
    if (it != list.end()) {
        list.erase(it);
        nowSaved = false;
    } else {
        list.insert(list.begin(), postId);
        if (list.size() > 200) list.resize(200);
        nowSaved = true;
    }
    geode::Mod::get()->setSavedValue("forum-bookmarks", list);
    return nowSaved;
}

inline std::string formatTimeShort(int64_t epoch, bool spanish) {
    if (epoch <= 0) return spanish ? "ahora" : "just now";
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    int64_t diff = std::max<int64_t>(0, now - epoch);
    if (diff < 60) return spanish ? "ahora" : "just now";
    if (diff < 3600) return spanish ? fmt::format("hace {}m", diff / 60) : fmt::format("{}m ago", diff / 60);
    if (diff < 86400) return spanish ? fmt::format("hace {}h", diff / 3600) : fmt::format("{}h ago", diff / 3600);
    if (diff < 604800) return spanish ? fmt::format("hace {}d", diff / 86400) : fmt::format("{}d ago", diff / 86400);
    if (diff < 2592000) return spanish ? fmt::format("hace {}sem", diff / 604800) : fmt::format("{}w ago", diff / 604800);
    if (diff < 31536000) return spanish ? fmt::format("hace {}mes", diff / 2592000) : fmt::format("{}mo ago", diff / 2592000);
    return spanish ? fmt::format("hace {}a", diff / 31536000) : fmt::format("{}y ago", diff / 31536000);
}

inline std::vector<int> extractLevelIds(std::string const& text) {
    std::vector<int> ids;
    if (text.empty()) return ids;

    auto isWordChar = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    };

    auto hasExplicitPrefix = [&](size_t digitStart) {
        size_t pos = digitStart;
        while (pos > 0 && (text[pos - 1] == ' ' || text[pos - 1] == ':' || text[pos - 1] == '=')) {
            --pos;
        }
        if (pos > 0 && text[pos - 1] == '#') {
            return true;
        }
        size_t wordEnd = pos;
        while (pos > 0 && std::isalpha(static_cast<unsigned char>(text[pos - 1]))) {
            --pos;
        }
        if (pos < wordEnd) {
            if (pos > 0 && isWordChar(text[pos - 1])) return false;
            std::string word = toLowerAscii(text.substr(pos, wordEnd - pos));
            return word == "id" || word == "level" || word == "nivel" || word == "lvl";
        }
        return false;
    };

    size_t i = 0;
    size_t const n = text.size();
    while (i < n && ids.size() < 5) {
        // Skip URLs so numeric path/query tokens inside URLs are ignored.
        if ((i + 7 <= n && toLowerAscii(text.substr(i, 7)) == "http://") ||
            (i + 8 <= n && toLowerAscii(text.substr(i, 8)) == "https://") ||
            (i + 4 <= n && toLowerAscii(text.substr(i, 4)) == "www.")) {
            while (i < n && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
            continue;
        }

        if (!std::isdigit(static_cast<unsigned char>(text[i]))) {
            ++i;
            continue;
        }

        size_t start = i;
        while (i < n && std::isdigit(static_cast<unsigned char>(text[i]))) {
            ++i;
        }
        size_t end = i;
        size_t len = end - start;

        // Ignore numbers embedded inside identifiers, dates, versions, or times.
        if (start > 0 && isWordChar(text[start - 1])) continue;
        if (end < n && isWordChar(text[end])) continue;
        if (start > 0 && (text[start - 1] == '-' || text[start - 1] == '.' || text[start - 1] == '/')) {
            if (start >= 2 && std::isdigit(static_cast<unsigned char>(text[start - 2]))) continue;
        }
        if (end < n && (text[end] == '-' || text[end] == '.' || text[end] == '/')) {
            if (end + 1 < n && std::isdigit(static_cast<unsigned char>(text[end + 1]))) continue;
        }
        if (start > 0 && text[start - 1] == ':' && start >= 2 && std::isdigit(static_cast<unsigned char>(text[start - 2]))) {
            continue;
        }
        if (end < n && text[end] == ':' && end + 1 < n && std::isdigit(static_cast<unsigned char>(text[end + 1]))) {
            continue;
        }

        if (len < 4 || len > 9) continue;

        auto parsed = geode::utils::numFromString<int>(text.substr(start, len));
        if (!parsed.isOk()) continue;
        int val = parsed.unwrap();
        if (val <= 0 || val > 999999999) continue;

        if (len >= 6 || hasExplicitPrefix(start)) {
            if (std::find(ids.begin(), ids.end(), val) == ids.end()) {
                ids.push_back(val);
            }
        }
    }

    return ids;
}

inline void openLevelById(int levelId) {
    if (levelId <= 0) return;
    auto* obj = GJSearchObject::create(SearchType::Search, std::to_string(levelId));
    if (!obj) return;
    auto* scene = LevelBrowserLayer::scene(obj);
    if (!scene) return;
    cocos2d::CCDirector::get()->pushScene(
        cocos2d::CCTransitionFade::create(0.4f, scene)
    );
}

inline void openAuthorProfile(int accountID) {
    if (accountID <= 0) return;
    auto* acc = GJAccountManager::get();
    bool own = acc && acc->m_accountID == accountID;
    if (auto* page = ProfilePage::create(accountID, own)) {
        page->show();
    }
}

inline SimplePlayer* makeAuthorIcon(Author const& a, float targetSize) {
    auto* gm = GameManager::get();
    int iconID = std::max(1, a.iconID);
    auto* player = SimplePlayer::create(iconID);
    if (!player) return nullptr;
    if (a.iconType > 0) {
        player->updatePlayerFrame(iconID, static_cast<IconType>(a.iconType));
    }
    if (gm) {
        auto col1 = gm->colorForIdx(a.color1);
        auto col2 = gm->colorForIdx(a.color2);
        player->setColor(col1);
        player->setSecondColor(col2);
        if (a.glowEnabled) player->setGlowOutline(col2);
        else player->disableGlowOutline();
    }
    float maxDim = std::max(player->getContentSize().width, player->getContentSize().height);
    float gdRefSize = 30.f;
    float scale = (maxDim > 10.f && maxDim < 80.f) ? (targetSize / maxDim) : (targetSize / gdRefSize);
    player->setScale(std::max(scale, 0.45f));
    return player;
}

struct ThreadedReply {
    Reply reply;
    int depth = 0;
    std::string parentAuthor;
};

// Groups replies under their root parent while keeping depth capped at 1 so narrow cards remain readable.
inline std::vector<ThreadedReply> buildThreadedReplies(std::vector<Reply> const& replies, int sortMode) {
    std::vector<ThreadedReply> out;
    if (replies.empty()) return out;

    std::unordered_map<std::string, Reply const*> byId;
    byId.reserve(replies.size());
    for (auto const& r : replies) {
        if (!r.id.empty()) byId[r.id] = &r;
    }

    auto findRootId = [&](Reply const& r) -> std::string {
        std::string cur = r.parentReplyId;
        if (cur.empty() || byId.find(cur) == byId.end()) return "";
        std::unordered_set<std::string> visited;
        visited.insert(r.id);
        int hops = 0;
        while (!cur.empty() && hops < 32) {
            if (visited.count(cur)) break;
            visited.insert(cur);
            auto it = byId.find(cur);
            if (it == byId.end()) break;
            std::string const& next = it->second->parentReplyId;
            if (next.empty() || byId.find(next) == byId.end()) {
                return cur;
            }
            cur = next;
            ++hops;
        }
        return cur;
    };

    std::vector<Reply const*> roots;
    std::unordered_map<std::string, std::vector<Reply const*>> childrenByRoot;

    for (auto const& r : replies) {
        std::string rootId = findRootId(r);
        if (rootId.empty() || rootId == r.id) {
            roots.push_back(&r);
        } else {
            childrenByRoot[rootId].push_back(&r);
        }
    }

    std::sort(roots.begin(), roots.end(), [sortMode](Reply const* a, Reply const* b) {
        if (sortMode == 1) { // Newest
            if (a->createdAt != b->createdAt) return a->createdAt > b->createdAt;
        } else if (sortMode == 2) { // Top liked
            if (a->likes != b->likes) return a->likes > b->likes;
        }
        return a->createdAt < b->createdAt; // Oldest (default)
    });

    out.reserve(replies.size());
    for (Reply const* root : roots) {
        out.push_back(ThreadedReply{*root, 0, ""});
        auto it = childrenByRoot.find(root->id);
        if (it == childrenByRoot.end()) continue;
        auto& kids = it->second;
        std::sort(kids.begin(), kids.end(), [](Reply const* a, Reply const* b) {
            return a->createdAt < b->createdAt;
        });
        for (Reply const* kid : kids) {
            std::string pAuthor;
            if (auto pit = byId.find(kid->parentReplyId); pit != byId.end()) {
                pAuthor = pit->second->author.username.empty()
                    ? "Anonymous"
                    : pit->second->author.username;
            }
            out.push_back(ThreadedReply{*kid, 1, std::move(pAuthor)});
        }
    }

    return out;
}

} // namespace paimon::forum
