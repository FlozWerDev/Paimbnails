#include "ForumApi.hpp"
#include "../../../utils/HttpClient.hpp"
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/loader/Mod.hpp>
#include <chrono>
#include <random>
#include <sstream>
#include <ctime>
#include <iomanip>

using namespace geode::prelude;

namespace paimon::forum {

// ─────────────────────────────────────────────────────────────────────────────
// JSON serialization helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::string jsonStr(matjson::Value const& v, std::string const& def = "") {
    if (v.isString()) return v.asString().unwrapOr(def);
    return def;
}

static int64_t jsonInt(matjson::Value const& v, int64_t def = 0) {
    if (v.isNumber()) return static_cast<int64_t>(v.asDouble().unwrapOr(static_cast<double>(def)));
    return def;
}

static bool jsonBool(matjson::Value const& v, bool def = false) {
    if (v.isBool()) return v.asBool().unwrapOr(def);
    return def;
}

// Extracts HTTP status code from HttpClient error strings like "HTTP 429: ..."
static int extractHttpStatus(std::string const& resp) {
    if (resp.rfind("HTTP ", 0) == 0) {
        size_t space = resp.find(' ', 5);
        if (space != std::string::npos) {
            auto codeStr = resp.substr(5, space - 5);
            try { return std::stoi(codeStr); } catch (...) {}
        }
    }
    return 0;
}

static std::string extractErrorMessage(std::string const& resp) {
    size_t pos = resp.find(':');
    if (pos != std::string::npos && resp.rfind("HTTP ", 0) == 0) {
        std::string msg = resp.substr(pos + 1);
        while (!msg.empty() && (msg.front() == ' ' || msg.front() == '\t')) msg.erase(0, 1);
        return msg;
    }
    return resp;
}

matjson::Value Author::toJson() const {
    return matjson::makeObject({
        {"accountID", accountID},
        {"username", username},
        {"iconID", iconID},
        {"iconType", iconType},
        {"color1", color1},
        {"color2", color2},
        {"glowEnabled", glowEnabled},
    });
}

Author Author::fromJson(matjson::Value const& v) {
    Author a;
    a.accountID    = static_cast<int>(jsonInt(v["accountID"]));
    a.username     = jsonStr(v["username"]);
    a.iconID       = static_cast<int>(jsonInt(v["iconID"], 1));
    a.iconType     = static_cast<int>(jsonInt(v["iconType"]));
    a.color1       = static_cast<int>(jsonInt(v["color1"]));
    a.color2       = static_cast<int>(jsonInt(v["color2"], 3));
    a.glowEnabled  = jsonBool(v["glowEnabled"]);
    return a;
}

Author Author::currentUser() {
    Author a;
    if (auto* acc = GJAccountManager::get()) {
        a.accountID = acc->m_accountID;
    }
    if (auto* gm = GameManager::get()) {
        a.username    = gm->m_playerName;
        int frame     = static_cast<int>(gm->m_playerFrame);
        a.iconID      = frame > 0 ? frame : 1;
        a.color1      = static_cast<int>(gm->m_playerColor);
        a.color2      = static_cast<int>(gm->m_playerColor2);
        a.glowEnabled = static_cast<bool>(gm->m_playerGlow);
    }
    if (a.username.empty()) a.username = "Anonymous";
    return a;
}

matjson::Value Reply::toJson() const {
    return matjson::makeObject({
        {"id", id},
        {"postId", postId},
        {"parentReplyId", parentReplyId},
        {"author", author.toJson()},
        {"content", content},
        {"createdAt", static_cast<double>(createdAt)},
        {"likes", likes},
        {"likedByMe", likedByMe},
        {"reportCount", reportCount},
    });
}

Reply Reply::fromJson(matjson::Value const& v) {
    Reply r;
    r.id            = jsonStr(v["id"]);
    r.postId        = jsonStr(v["postId"]);
    r.parentReplyId = jsonStr(v["parentReplyId"]);
    r.author        = Author::fromJson(v["author"]);
    r.content       = jsonStr(v["content"]);
    r.createdAt     = jsonInt(v["createdAt"]);
    r.likes         = static_cast<int>(jsonInt(v["likes"]));
    r.likedByMe     = jsonBool(v["likedByMe"]);
    r.reportCount   = static_cast<int>(jsonInt(v["reportCount"]));
    return r;
}

matjson::Value Post::toJson() const {
    matjson::Value tagsArr = matjson::Value::array();
    for (auto const& t : tags) tagsArr.push(t);

    matjson::Value repliesArr = matjson::Value::array();
    for (auto const& r : replies) repliesArr.push(r.toJson());

    return matjson::makeObject({
        {"id", id},
        {"author", author.toJson()},
        {"title", title},
        {"description", description},
        {"tags", tagsArr},
        {"createdAt", static_cast<double>(createdAt)},
        {"updatedAt", static_cast<double>(updatedAt)},
        {"likes", likes},
        {"likedByMe", likedByMe},
        {"replyCount", replyCount},
        {"reportCount", reportCount},
        {"pinned", pinned},
        {"locked", locked},
        {"replies", repliesArr},
    });
}

Post Post::fromJson(matjson::Value const& v) {
    Post p;
    p.id          = jsonStr(v["id"]);
    p.author      = Author::fromJson(v["author"]);
    p.title       = jsonStr(v["title"]);
    p.description = jsonStr(v["description"]);
    p.createdAt   = jsonInt(v["createdAt"]);
    p.updatedAt   = jsonInt(v["updatedAt"]);
    p.likes       = static_cast<int>(jsonInt(v["likes"]));
    p.likedByMe   = jsonBool(v["likedByMe"]);
    p.replyCount  = static_cast<int>(jsonInt(v["replyCount"]));
    p.reportCount = static_cast<int>(jsonInt(v["reportCount"]));
    p.pinned      = jsonBool(v["pinned"]);
    p.locked      = jsonBool(v["locked"]);
    if (v["tags"].isArray()) {
        for (auto const& t : v["tags"]) p.tags.push_back(jsonStr(t));
    }
    if (v["replies"].isArray()) {
        for (auto const& r : v["replies"]) p.replies.push_back(Reply::fromJson(r));
    }
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// ForumApi
// ─────────────────────────────────────────────────────────────────────────────

ForumApi& ForumApi::get() {
    static ForumApi instance;
    return instance;
}

ForumApi::ForumApi() {
    loadCache();
}

bool ForumApi::hasServer() const {
    return !HttpClient::get().getServerURL().empty() || !HttpClient::get().getForumServerURL().empty();
}

static std::string forumBaseUrl() {
    auto forum = HttpClient::get().getForumServerURL();
    if (!forum.empty()) return forum;
    return HttpClient::get().getServerURL();
}

int64_t ForumApi::nowEpoch() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string ForumApi::makeLocalId() const {
    static std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    return fmt::format("local-{:x}-{:x}", static_cast<uint64_t>(nowEpoch()), dist(rng));
}

std::string ForumApi::sortToString(SortMode s) {
    switch (s) {
        case SortMode::TopRated:  return "top";
        case SortMode::MostLiked: return "liked";
        case SortMode::Recent:
        default:                  return "recent";
    }
}

std::string ForumApi::urlEncode(std::string const& s) {
    return HttpClient::encodeQueryParam(s);
}

void ForumApi::loadCache() {
    if (m_loaded) return;
    m_loaded = true;

    auto saved = Mod::get()->getSavedValue<matjson::Value>("forum-cache", matjson::Value::array());
    if (!saved.isArray()) return;
    for (auto const& v : saved) {
        try {
            m_cache.push_back(Post::fromJson(v));
        } catch (...) {
            // skip corrupt entries
        }
    }
}

void ForumApi::saveCache() {
    matjson::Value arr = matjson::Value::array();
    for (auto const& p : m_cache) {
        try {
            arr.push(p.toJson());
        } catch (...) {
            // skip corrupt entries
        }
    }
    Mod::get()->setSavedValue("forum-cache", arr);
}

Post* ForumApi::findCached(std::string const& id) {
    for (auto& p : m_cache) if (p.id == id) return &p;
    return nullptr;
}

Post& ForumApi::upsertCache(Post const& p) {
    if (auto* existing = findCached(p.id)) {
        *existing = p;
        return *existing;
    }
    m_cache.insert(m_cache.begin(), p);
    return m_cache.front();
}

// ── helpers callback ────────────────────────────────────────────────────────

template<typename T>
static Result<T> makeOk(T data) {
    Result<T> r;
    r.ok = true;
    r.data = std::move(data);
    return r;
}

template<typename T>
static Result<T> makeErr(std::string const& msg) {
    Result<T> r;
    r.ok = false;
    r.error = msg;
    return r;
}

// ── listPosts ────────────────────────────────────────────────────────────────

void ForumApi::listPosts(ListFilter const& filter, ListCallback cb) {
    // Construye query string
    int accountId = Author::currentUser().accountID;
    std::string qs = fmt::format("?sort={}&page={}&limit={}&_aid={}",
        sortToString(filter.sort), filter.page, filter.limit, accountId);
    if (!filter.tags.empty()) {
        std::string joined;
        for (size_t i = 0; i < filter.tags.size(); i++) {
            if (i > 0) joined += ",";
            joined += filter.tags[i];
        }
        qs += "&tags=" + urlEncode(joined);
    }
    if (!filter.query.empty()) {
        qs += "&q=" + urlEncode(filter.query);
    }

    auto fallback = [this, filter, cb]() {
        // Filtra/ordena cache local
        std::vector<Post> out;
        out.reserve(m_cache.size());
        for (auto const& p : m_cache) {
            if (!filter.tags.empty()) {
                bool match = false;
                for (auto const& t : filter.tags) {
                    for (auto const& pt : p.tags) {
                        if (pt == t) { match = true; break; }
                    }
                    if (match) break;
                }
                if (!match) continue;
            }
            if (!filter.query.empty()) {
                std::string q = filter.query;
                for (auto& c : q) c = static_cast<char>(std::tolower(c));
                std::string title = p.title;
                for (auto& c : title) c = static_cast<char>(std::tolower(c));
                if (title.find(q) == std::string::npos) continue;
            }
            out.push_back(p);
        }
        switch (filter.sort) {
            case SortMode::TopRated:
                std::sort(out.begin(), out.end(), [](Post const& a, Post const& b) {
                    return (a.likes - a.reportCount) > (b.likes - b.reportCount);
                });
                break;
            case SortMode::MostLiked:
                std::sort(out.begin(), out.end(), [](Post const& a, Post const& b) {
                    return a.likes > b.likes;
                });
                break;
            case SortMode::Recent:
            default:
                std::sort(out.begin(), out.end(), [](Post const& a, Post const& b) {
                    return a.createdAt > b.createdAt;
                });
                break;
        }
        cb(makeOk(std::move(out)));
    };

    if (!hasServer()) { fallback(); return; }

    HttpClient::get().get(forumBaseUrl() + "/api/forum/posts" + qs,
        [cb, fallback](bool ok, std::string const& resp) {
            if (!ok) { fallback(); return; }
            auto parsed = matjson::parse(resp);
            if (!parsed.isOk()) { fallback(); return; }
            auto root = parsed.unwrap();
            std::vector<Post> posts;
            if (root.isArray()) {
                for (auto const& v : root) posts.push_back(Post::fromJson(v));
            } else if (root["posts"].isArray()) {
                for (auto const& v : root["posts"]) posts.push_back(Post::fromJson(v));
            }
            cb(makeOk(std::move(posts)));
        });
}

// ── getPost ─────────────────────────────────────────────────────────────────

void ForumApi::getPost(std::string const& postId, PostCallback cb) {
    auto fallback = [this, postId, cb]() {
        if (auto* p = findCached(postId)) {
            cb(makeOk(*p));
        } else {
            cb(makeErr<Post>("Post not found"));
        }
    };

    if (!hasServer()) { fallback(); return; }

    int accountId = Author::currentUser().accountID;
    HttpClient::get().get(forumBaseUrl() + "/api/forum/posts/" + urlEncode(postId) + "?_aid=" + std::to_string(accountId),
        [cb, fallback](bool ok, std::string const& resp) {
            if (!ok) { fallback(); return; }
            auto parsed = matjson::parse(resp);
            if (!parsed.isOk()) { fallback(); return; }
            cb(makeOk(Post::fromJson(parsed.unwrap())));
        });
}

// ── createPost ──────────────────────────────────────────────────────────────

void ForumApi::createPost(CreatePostRequest const& req, PostCallback cb) {
    Post p;
    p.id          = makeLocalId();
    p.author      = Author::currentUser();
    p.title       = req.title;
    p.description = req.description;
    p.tags        = req.tags;
    p.createdAt   = nowEpoch();
    p.updatedAt   = p.createdAt;

    // Always save locally first (optimistic)
    upsertCache(p);
    saveCache();

    if (!hasServer()) {
        cb(makeOk(p));
        return;
    }

    matjson::Value tagsArr = matjson::Value::array();
    for (auto const& t : req.tags) tagsArr.push(t);

    matjson::Value body = matjson::makeObject({
        {"title", req.title},
        {"description", req.description},
        {"tags", tagsArr},
        {"author", p.author.toJson()},
        {"localId", p.id},
    });

    HttpClient::get().postWithAuth(forumBaseUrl() + "/api/forum/posts", body.dump(),
        [this, cb, localId = p.id](bool ok, std::string const& resp) {
            if (!ok) {
                int code = extractHttpStatus(resp);
                // keep local copy on server error so data is not lost
                if (auto* cached = findCached(localId)) {
                    if (code == 429) {
                        markPostCooldown(30); // server rate limit ~30s
                        cb(makeErr<Post>("Rate limited. Please wait before posting again."));
                    } else {
                        cb(makeOk(*cached)); // silent success with local copy
                    }
                } else {
                    if (code == 429) {
                        markPostCooldown(30);
                        cb(makeErr<Post>("Rate limited. Please wait before posting again."));
                    } else {
                        cb(makeErr<Post>("Server error: " + resp));
                    }
                }
                return;
            }
            auto parsed = matjson::parse(resp);
            if (!parsed.isOk()) {
                if (auto* cached = findCached(localId)) {
                    cb(makeOk(*cached));
                } else {
                    cb(makeErr<Post>("Bad JSON"));
                }
                return;
            }
            auto srv = Post::fromJson(parsed.unwrap());
            // replace optimistic local copy with server copy
            upsertCache(srv);
            saveCache();
            markPostCooldown(3); // prevent accidental double-submit
            cb(makeOk(srv));
        });
}

// ── deletePost ──────────────────────────────────────────────────────────────

void ForumApi::deletePost(std::string const& postId, BoolCallback cb) {
    // Borra local sin esperar
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
        if (it->id == postId) { m_cache.erase(it); break; }
    }
    saveCache();

    if (!hasServer()) { cb(makeOk(true)); return; }

    matjson::Value body = matjson::makeObject({
        {"postId", postId},
        {"_method", "DELETE"},
        {"author", Author::currentUser().toJson()},
    });
    HttpClient::get().postWithAuth(forumBaseUrl() + "/api/forum/posts/" + urlEncode(postId) + "/delete",
        body.dump(),
        [cb](bool ok, std::string const&) { cb(makeOk(ok)); });
}

// ── togglePostLike ──────────────────────────────────────────────────────────

void ForumApi::togglePostLike(std::string const& postId, BoolCallback cb) {
    if (auto* p = findCached(postId)) {
        p->likedByMe = !p->likedByMe;
        p->likes += p->likedByMe ? 1 : -1;
        if (p->likes < 0) p->likes = 0;
        saveCache();
    }

    if (!hasServer()) { cb(makeOk(true)); return; }

    matjson::Value body = matjson::makeObject({{"postId", postId}, {"author", Author::currentUser().toJson()}});
    HttpClient::get().postWithAuth(forumBaseUrl() + "/api/forum/posts/" + urlEncode(postId) + "/like",
        body.dump(),
        [cb](bool ok, std::string const&) { cb(makeOk(ok)); });
}

// ── reportPost ──────────────────────────────────────────────────────────────

void ForumApi::reportPost(std::string const& postId, std::string const& reason, BoolCallback cb) {
    if (!hasServer()) {
        // Aun sin server, registramos el report localmente
        if (auto* p = findCached(postId)) p->reportCount++;
        saveCache();
        cb(makeOk(true));
        return;
    }

    matjson::Value body = matjson::makeObject({
        {"postId", postId},
        {"reason", reason},
        {"author", Author::currentUser().toJson()},
    });
    HttpClient::get().postWithAuth(forumBaseUrl() + "/api/forum/posts/" + urlEncode(postId) + "/report",
        body.dump(),
        [cb](bool ok, std::string const&) { cb(makeOk(ok)); });
}

// ── createReply ─────────────────────────────────────────────────────────────

void ForumApi::createReply(CreateReplyRequest const& req, ReplyCallback cb) {
    Reply r;
    r.id            = "local-" + std::to_string(nowEpoch()) + "-" + std::to_string(rand());
    r.postId        = req.postId;
    r.parentReplyId = req.parentReplyId;
    r.author        = Author::currentUser();
    r.content       = req.content;
    r.createdAt     = nowEpoch();

    // Insercion optimista en cache
    if (auto* p = findCached(req.postId)) {
        p->replies.push_back(r);
        p->replyCount++;
        saveCache();
    }

    if (!hasServer()) { cb(makeOk(r)); return; }

    matjson::Value body = matjson::makeObject({
        {"postId", req.postId},
        {"parentReplyId", req.parentReplyId},
        {"content", req.content},
        {"author", r.author.toJson()},
        {"localId", r.id},
    });
    HttpClient::get().postWithAuth(
        forumBaseUrl() + "/api/forum/posts/" + urlEncode(req.postId) + "/replies",
        body.dump(),
        [this, cb](bool ok, std::string const& resp) {
            if (!ok) {
                int code = extractHttpStatus(resp);
                if (code == 429) {
                    markReplyCooldown(15); // server rate limit ~15s for replies
                    cb(makeErr<Reply>("Rate limited. Please wait before replying again."));
                } else {
                    cb(makeErr<Reply>("Server error"));
                }
                return;
            }
            auto parsed = matjson::parse(resp);
            if (!parsed.isOk()) { cb(makeErr<Reply>("Bad JSON")); return; }
            markReplyCooldown(3); // prevent accidental double-submit
            cb(makeOk(Reply::fromJson(parsed.unwrap())));
        });
}

void ForumApi::deleteReply(std::string const& replyId, BoolCallback cb) {
    for (auto& p : m_cache) {
        for (auto it = p.replies.begin(); it != p.replies.end(); ++it) {
            if (it->id == replyId) {
                p.replies.erase(it);
                if (p.replyCount > 0) p.replyCount--;
                break;
            }
        }
    }
    saveCache();

    if (!hasServer()) { cb(makeOk(true)); return; }

    matjson::Value body = matjson::makeObject({{"replyId", replyId}, {"author", Author::currentUser().toJson()}});
    HttpClient::get().postWithAuth(forumBaseUrl() + "/api/forum/replies/" + urlEncode(replyId) + "/delete",
        body.dump(),
        [cb](bool ok, std::string const&) { cb(makeOk(ok)); });
}

void ForumApi::toggleReplyLike(std::string const& replyId, BoolCallback cb) {
    for (auto& p : m_cache) {
        for (auto& r : p.replies) {
            if (r.id == replyId) {
                r.likedByMe = !r.likedByMe;
                r.likes += r.likedByMe ? 1 : -1;
                if (r.likes < 0) r.likes = 0;
            }
        }
    }
    saveCache();

    if (!hasServer()) { cb(makeOk(true)); return; }

    matjson::Value body = matjson::makeObject({{"replyId", replyId}, {"author", Author::currentUser().toJson()}});
    HttpClient::get().postWithAuth(forumBaseUrl() + "/api/forum/replies/" + urlEncode(replyId) + "/like",
        body.dump(),
        [cb](bool ok, std::string const&) { cb(makeOk(ok)); });
}

void ForumApi::reportReply(std::string const& replyId, std::string const& reason, BoolCallback cb) {
    if (!hasServer()) { cb(makeOk(true)); return; }
    matjson::Value body = matjson::makeObject({
        {"replyId", replyId},
        {"reason", reason},
        {"author", Author::currentUser().toJson()},
    });
    HttpClient::get().postWithAuth(forumBaseUrl() + "/api/forum/replies/" + urlEncode(replyId) + "/report",
        body.dump(),
        [cb](bool ok, std::string const&) { cb(makeOk(ok)); });
}

void ForumApi::listTags(TagsCallback cb) {
    auto fallback = [cb]() {
        std::vector<std::string> defaults = {
            "Guide", "Tip", "Question", "Bug", "Suggestion",
            "Showcase", "Discussion", "Help", "News", "Update",
            "Level", "Video", "Art", "Music", "Story",
            "Theory", "Challenge", "Competition", "Feedback", "Other"
        };
        cb(makeOk(std::move(defaults)));
    };
    if (!hasServer()) { fallback(); return; }
    HttpClient::get().get(forumBaseUrl() + "/api/forum/tags",
        [cb, fallback](bool ok, std::string const& resp) {
            if (!ok) { fallback(); return; }
            auto parsed = matjson::parse(resp);
            if (!parsed.isOk()) { fallback(); return; }
            auto root = parsed.unwrap();
            std::vector<std::string> out;
            if (root.isArray()) {
                for (auto const& v : root) out.push_back(jsonStr(v));
            } else if (root["tags"].isArray()) {
                for (auto const& v : root["tags"]) out.push_back(jsonStr(v));
            }
            if (out.empty()) { fallback(); return; }
            cb(makeOk(std::move(out)));
        });
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers UI
// ─────────────────────────────────────────────────────────────────────────────

std::string formatRelativeTime(int64_t epoch) {
    if (epoch <= 0) return "just now";
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    int64_t diff = now - epoch;
    if (diff < 0) diff = 0;
    if (diff < 60)        return "just now";
    if (diff < 3600)      return fmt::format("{}m ago",  diff / 60);
    if (diff < 86400)     return fmt::format("{}h ago",  diff / 3600);
    if (diff < 604800)    return fmt::format("{}d ago",  diff / 86400);
    if (diff < 2592000)   return fmt::format("{}w ago",  diff / 604800);
    if (diff < 31536000)  return fmt::format("{}mo ago", diff / 2592000);
    return fmt::format("{}y ago", diff / 31536000);
}

std::string formatAbsoluteTime(int64_t epoch) {
    if (epoch <= 0) return "";
    std::time_t t = static_cast<std::time_t>(epoch);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M");
    return oss.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// UserStatus
// ─────────────────────────────────────────────────────────────────────────────

matjson::Value UserStatus::toJson() const {
    return matjson::makeObject({
        {"accountID", accountID},
        {"online", online},
        {"lastSeen", static_cast<double>(lastSeen)},
    });
}

UserStatus UserStatus::fromJson(matjson::Value const& v) {
    UserStatus s;
    s.accountID = static_cast<int>(jsonInt(v["accountID"]));
    s.online    = jsonBool(v["online"]);
    s.lastSeen  = jsonInt(v["lastSeen"]);
    return s;
}

matjson::Value ProfileView::toJson() const {
    return matjson::makeObject({
        {"viewerAccountID", viewerAccountID},
        {"viewerUsername", viewerUsername},
        {"viewedAt", static_cast<double>(viewedAt)},
    });
}

ProfileView ProfileView::fromJson(matjson::Value const& v) {
    ProfileView pv;
    pv.viewerAccountID = static_cast<int>(jsonInt(v["viewerAccountID"]));
    pv.viewerUsername  = jsonStr(v["viewerUsername"]);
    pv.viewedAt        = jsonInt(v["viewedAt"]);
    return pv;
}

// ─────────────────────────────────────────────────────────────────────────────
// User status endpoints
// ─────────────────────────────────────────────────────────────────────────────

void ForumApi::getUserStatus(int accountID, UserStatusCallback cb) {
    if (!hasServer()) {
        // Sin servidor: devolver offline con lastSeen = 0
        UserStatus offline;
        offline.accountID = accountID;
        offline.online = false;
        offline.lastSeen = 0;
        cb(makeOk(std::move(offline)));
        return;
    }

    HttpClient::get().get(
        forumBaseUrl() + fmt::format("/api/forum/users/{}/status", accountID),
        [cb, accountID](bool ok, std::string const& resp) {
            if (!ok) {
                UserStatus offline;
                offline.accountID = accountID;
                offline.online = false;
                offline.lastSeen = 0;
                cb(makeOk(std::move(offline)));
                return;
            }
            auto parsed = matjson::parse(resp);
            if (!parsed.isOk()) {
                UserStatus offline;
                offline.accountID = accountID;
                offline.online = false;
                offline.lastSeen = 0;
                cb(makeOk(std::move(offline)));
                return;
            }
            cb(makeOk(UserStatus::fromJson(parsed.unwrap())));
        });
}

void ForumApi::sendHeartbeat(BoolCallback cb) {
    if (!hasServer()) {
        cb(makeOk(true));
        return;
    }

    matjson::Value body = matjson::makeObject({
        {"author", Author::currentUser().toJson()},
        {"timestamp", static_cast<double>(nowEpoch())},
    });

    HttpClient::get().postWithAuth(
        forumBaseUrl() + "/api/forum/users/heartbeat",
        body.dump(),
        [cb](bool ok, std::string const&) {
            cb(makeOk(ok));
        });
}

// ─────────────────────────────────────────────────────────────────────────────
// Profile view endpoints
// ─────────────────────────────────────────────────────────────────────────────

void ForumApi::recordProfileView(int accountID, BoolCallback cb) {
    if (!hasServer()) {
        cb(makeOk(true));
        return;
    }

    matjson::Value body = matjson::makeObject({
        {"author", Author::currentUser().toJson()},
        {"timestamp", static_cast<double>(nowEpoch())},
    });

    HttpClient::get().postWithAuth(
        forumBaseUrl() + fmt::format("/api/forum/users/{}/view", accountID),
        body.dump(),
        [cb](bool ok, std::string const& resp) {
            if (!ok) {
                int code = extractHttpStatus(resp);
                if (code == 404) {
                    // Server does not support profile views yet — silently ignore
                    cb(makeOk(true));
                } else if (code == 429) {
                    cb(makeErr<bool>("Rate limited. Please wait before viewing profiles again."));
                } else {
                    cb(makeOk(false));
                }
                return;
            }
            cb(makeOk(true));
        });
}

void ForumApi::getProfileViews(int accountID, ProfileViewsCallback cb) {
    if (!hasServer()) {
        cb(makeOk(std::vector<ProfileView>{}));
        return;
    }

    HttpClient::get().get(
        forumBaseUrl() + fmt::format("/api/forum/users/{}/views", accountID),
        [cb](bool ok, std::string const& resp) {
            if (!ok) {
                int code = extractHttpStatus(resp);
                if (code == 404) {
                    cb(makeErr<std::vector<ProfileView>>("Profile views are not available on this server version."));
                } else if (code == 429) {
                    cb(makeErr<std::vector<ProfileView>>("Rate limited. Please wait a moment."));
                } else {
                    cb(makeOk(std::vector<ProfileView>{}));
                }
                return;
            }
            auto parsed = matjson::parse(resp);
            if (!parsed.isOk()) {
                cb(makeOk(std::vector<ProfileView>{}));
                return;
            }
            auto root = parsed.unwrap();
            std::vector<ProfileView> views;
            if (root.isArray()) {
                for (auto const& v : root) views.push_back(ProfileView::fromJson(v));
            } else if (root["views"].isArray()) {
                for (auto const& v : root["views"]) views.push_back(ProfileView::fromJson(v));
            }
            cb(makeOk(std::move(views)));
        });
}

// ─────────────────────────────────────────────────────────────────────────────
// Cooldown tracking (client-side rate limit awareness)
// ─────────────────────────────────────────────────────────────────────────────

int64_t ForumApi::getPostCooldownRemaining() const {
    int64_t now = nowEpoch();
    if (m_postCooldownUntil <= now) return 0;
    return m_postCooldownUntil - now;
}

int64_t ForumApi::getReplyCooldownRemaining() const {
    int64_t now = nowEpoch();
    if (m_replyCooldownUntil <= now) return 0;
    return m_replyCooldownUntil - now;
}

void ForumApi::markPostCooldown(int64_t seconds) {
    m_postCooldownUntil = nowEpoch() + seconds;
}

void ForumApi::markReplyCooldown(int64_t seconds) {
    m_replyCooldownUntil = nowEpoch() + seconds;
}

} // namespace paimon::forum
