// Comment Mentions — notifies when someone mentions you in level comments
// (daily, weekly, event, or custom IDs).
//
// The polling timer runs on a background thread (sleep + fire only);
// GD requests are dispatched on the main thread via WebHelper, and all
// mention state lives exclusively on the main thread.

#include <Geode/Geode.hpp>

#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <regex>
#include <string>
#include <thread>
#include <vector>

#include "../../utils/GDRobTopCache.hpp"
#include "../../utils/AccountVerifier.hpp"
#include "../../utils/ThreadTracker.hpp"
#include "../../utils/PaimonNotification.hpp"
#include "../../core/RuntimeLifecycle.hpp"
#include "MentionLink.hpp"

using namespace geode::prelude;

namespace {

namespace gstr = geode::utils::string;

constexpr char const* GD_SECRET = "Wmfd2893gb7";

inline bool        sBool(char const* k) { return Mod::get()->getSettingValue<bool>(k); }
inline int64_t     sInt(char const* k)  { return Mod::get()->getSettingValue<int64_t>(k); }
inline std::string sStr(char const* k)  { return Mod::get()->getSettingValue<std::string>(k); }

// Decodes URL-safe base64 (GD comments use '-' and '_').
// Also tolerates the standard '+' '/' alphabet and padding/whitespace.
std::string base64UrlDecode(std::string const& in) {
    static int8_t const* T = [] {
        static int8_t arr[256];
        for (int i = 0; i < 256; ++i) arr[i] = -1;
        char const* a = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
        for (int i = 0; i < 64; ++i) arr[(unsigned char)a[i]] = (int8_t)i;
        arr[(unsigned char)'+'] = 62;
        arr[(unsigned char)'/'] = 63;
        return arr;
    }();

    std::string out;
    out.reserve(in.size() * 3 / 4);
    int bits = 0, value = 0;
    for (unsigned char c : in) {
        int8_t v = T[c];
        if (v < 0) continue;
        value = (value << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((char)((value >> bits) & 0xFF));
        }
    }
    return out;
}

// Parses a "k<sep>v<sep>k<sep>v..." string into a map.
std::map<std::string, std::string> parseKV(std::string const& s, std::string const& sep) {
    auto parts = gstr::split(s, sep);
    std::map<std::string, std::string> m;
    for (size_t i = 0; i + 1 < parts.size(); i += 2) m[parts[i]] = parts[i + 1];
    return m;
}

// Reads a comma-separated string setting into a trimmed list.
std::vector<std::string> listSetting(char const* key) {
    std::vector<std::string> out;
    for (auto const& it : gstr::split(sStr(key), ",")) {
        auto t = gstr::trim(it);
        if (!t.empty()) out.push_back(t);
    }
    return out;
}

// POST to RobTop with disk cache. Callback runs on the main thread.
void gdRequest(std::string const& endpoint, std::string const& body,
               std::function<void(bool, std::string)> cb) {
    paimon::gd::postCached(endpoint, body, std::move(cb), paimon::gd::policyForEndpoint(endpoint));
}

class MentionWatcher {
public:
    static MentionWatcher& get() {
        static MentionWatcher inst;
        return inst;
    }

    // Called in $on_game(Loaded). Starts the polling thread exactly once.
    void startup() {
        if (m_started) return;
        m_started = true;

        // First run: seed aliases with the current username.
        if (!Mod::get()->setSavedValue("mentions-alias-initialized", true)) {
            auto user = AccountVerifier::get().getUsername();
            if (!user.empty() && sStr("mentions-aliases").empty()) {
                Mod::get()->setSettingValue<std::string>("mentions-aliases", gstr::toLower(user));
            }
        }

        load();
        reloadLevels();

        paimon::ThreadTracker::get().spawn([] {
            geode::utils::thread::setName("PaimonMentions");
            while (!shuttingDown()) {
                int rate = (int)std::clamp<int64_t>(sInt("mentions-refresh-rate"), 10, 300);
                for (int i = 0; i < rate; ++i) {
                    if (shuttingDown()) return;
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                geode::Loader::get()->queueInMainThread([] {
                    if (paimon::isRuntimeShuttingDown()) return;
                    MentionWatcher::get().pollOnce();
                });
            }
        });
    }

    // Recomputes the special level IDs to watch. Main thread.
    void reloadLevels() {
        m_dailyID = m_weeklyID = m_eventID = 0;
        if (!sBool("mentions-enabled")) return;
        if (sBool("mentions-daily"))  fetchSpecial(21);
        if (sBool("mentions-weekly")) fetchSpecial(22);
        if (sBool("mentions-event"))  fetchSpecial(23);
    }

    void save() {
        Mod::get()->setSavedValue("mentions-seen",
            std::vector<std::string>(m_seen.begin(), m_seen.end()));
    }

private:
    struct Mention { std::string user, text; };

    static bool shuttingDown() {
        return paimon::isRuntimeShuttingDown() || paimon::ThreadTracker::get().isShuttingDown();
    }

    void load() {
        for (auto const& s : Mod::get()->getSavedValue<std::vector<std::string>>("mentions-seen", {})) {
            m_seen.push_back(s);
        }
    }

    void fetchSpecial(int type) {
        if (shuttingDown()) return;
        gdRequest("getGJLevels21.php", fmt::format("type={}&secret={}", type, GD_SECRET),
            [this, type](bool ok, std::string body) {
                if (shuttingDown() || !ok) return;
                auto head = gstr::split(gstr::split(body, "#")[0], "|");
                if (head.empty()) return;
                auto kv = parseKV(head[0], ":");
                auto it = kv.find("1");
                if (it == kv.end()) return;
                auto id = geode::utils::numFromString<int>(it->second);
                if (!id) return;
                if (type == 21)      m_dailyID  = id.unwrap();
                else if (type == 22) m_weeklyID = id.unwrap();
                else                 m_eventID  = id.unwrap();
            });
    }

    // Main thread. Fires a comment request per watched level.
    void pollOnce() {
        if (shuttingDown() || !sBool("mentions-enabled")) return;
        updateAliases();
        if (!m_hasAliases) return;

        std::vector<int> ids;
        if (m_dailyID > 0)  ids.push_back(m_dailyID);
        if (m_weeklyID > 0) ids.push_back(m_weeklyID);
        if (m_eventID > 0)  ids.push_back(m_eventID);
        for (auto const& s : listSetting("mentions-custom-ids")) {
            auto n = geode::utils::numFromString<int>(s);
            if (n) ids.push_back(n.unwrap());
        }
        for (int id : ids) pollLevel(id);
    }

    void pollLevel(int levelID) {
        if (shuttingDown()) return;
        gdRequest("getGJComments21.php",
            fmt::format("levelID={}&page=0&secret={}", levelID, GD_SECRET),
            [this](bool ok, std::string body) {
                if (shuttingDown() || !ok) return;

                std::vector<std::string> seenNow;
                std::vector<Mention> found;
                for (auto const& c : gstr::split(gstr::split(body, "#")[0], "|")) {
                    auto outer = gstr::split(c, ":");
                    if (outer.size() < 2) continue;
                    auto cm = parseKV(outer[0], "~");
                    auto au = parseKV(outer[1], "~");

                    auto textIt = cm.find("2");
                    auto msgIt  = cm.find("6");
                    if (textIt == cm.end() || msgIt == cm.end()) continue;

                    std::string text = base64UrlDecode(textIt->second);
                    if (!containsMention(text)) continue;
                    if (isSeen(msgIt->second)) continue;
                    if (sBool("mentions-ignore-self") &&
                        isSelf(au.count("16") ? au["16"] : "")) continue;

                    std::string user = au.count("1") ? au["1"] : "Someone";
                    if (isBlacklisted(user)) continue;
                    if (isHiddenWord(text)) continue;

                    seenNow.push_back(msgIt->second);
                    found.push_back({user, text});
                }

                if (found.empty()) return;
                // If playing and not allowed, skip marking seen so they
                // are re-detected when the user returns to the menu.
                if (!sBool("mentions-show-while-playing") && PlayLayer::get()) return;

                for (auto const& id : seenNow) markSeen(id);
                notify(found);
            });
    }

    void notify(std::vector<Mention> const& found) {
        int maxN = (int)std::clamp<int64_t>(sInt("mentions-max-notifications"), 1, 20);
        if ((int)found.size() > maxN) {
            showNotif(fmt::format("{} new mentions!", found.size()), "Check them out!");
        } else {
            for (auto const& f : found) {
                showNotif(fmt::format("{} mentioned you!", f.user), f.text);
            }
        }
    }

    void showNotif(std::string const& title, std::string const& msg) {
        if (auto* n = AchievementNotifier::sharedState()) {
            n->notifyAchievement(title.c_str(), msg.c_str(),
                                 "accountBtn_pendingRequest_001.png", true);
        }
    }

    void updateAliases() {
        std::vector<std::string> aliases;
        if (sBool("mentions-enable-everyone")) aliases.push_back("@everyone");
        for (auto const& a : listSetting("mentions-aliases")) {
            if (gstr::contains(a, "everyone")) continue;
            aliases.push_back(a);
        }
        m_hasAliases = !aliases.empty();
        if (!m_hasAliases) return;
        try {
            m_re = std::regex(
                fmt::format("\\b{}(?:{})\\b",
                            sBool("mentions-require-at") ? "@" : "",
                            gstr::join(aliases, "|")),
                sBool("mentions-case-sensitive")
                    ? std::regex::optimize
                    : (std::regex::icase | std::regex::optimize));
        } catch (...) {
            m_hasAliases = false;
        }
    }

    bool containsMention(std::string const& s) {
        return m_hasAliases && std::regex_search(s, m_re);
    }

    bool isSelf(std::string const& accID) {
        if (accID.empty()) return false;
        auto n = geode::utils::numFromString<int>(accID);
        return n && n.unwrap() == AccountVerifier::get().getAccountID();
    }

    bool isBlacklisted(std::string const& user) {
        auto lu = gstr::toLower(user);
        for (auto const& b : listSetting("mentions-user-blacklist")) {
            if (gstr::toLower(b) == lu) return true;
        }
        return false;
    }

    bool isHiddenWord(std::string const& text) {
        auto words = listSetting("mentions-word-blacklist");
        if (words.empty()) return false;
        auto lt = gstr::toLower(text);
        for (auto const& w : words) {
            auto lw = gstr::toLower(w);
            if (!lw.empty() && lt.find(lw) != std::string::npos) return true;
        }
        return false;
    }

    bool isSeen(std::string const& id) {
        return std::find(m_seen.begin(), m_seen.end(), id) != m_seen.end();
    }

    void markSeen(std::string const& id) {
        m_seen.push_back(id);
        while (m_seen.size() > 50) m_seen.pop_front();
    }

    bool m_started = false;
    bool m_hasAliases = false;
    int m_dailyID = 0, m_weeklyID = 0, m_eventID = 0;
    std::deque<std::string> m_seen;
    std::regex m_re;
};

} // namespace

// Resolves username -> accountID via getGJUsers20 and opens the ProfilePage.
// Uses gdRequest/parseKV helpers from this TU's anonymous namespace.
void paimon::mentions::openProfile(std::string const& username) {
    if (username.empty()) return;

    PaimonNotify::show(fmt::format("Loading @{}...", username),
                       geode::NotificationIcon::Loading, 0.7f);

    gdRequest("getGJUsers20.php",
        fmt::format("str={}&page=0&secret={}", username, GD_SECRET),
        [username](bool ok, std::string body) {
            auto fail = [&] {
                PaimonNotify::show(fmt::format("Couldn't find @{}", username),
                                   geode::NotificationIcon::Error);
            };
            if (paimon::isRuntimeShuttingDown()) return;
            if (!ok) { fail(); return; }

            auto users = geode::utils::string::split(
                geode::utils::string::split(body, "#")[0], "|");
            if (users.empty()) { fail(); return; }

            auto kv = parseKV(users[0], ":");
            auto it = kv.find("16"); // accountID
            if (it == kv.end()) { fail(); return; }

            auto acc = geode::utils::numFromString<int>(it->second);
            if (!acc || acc.unwrap() <= 0) { fail(); return; }

            int accID = acc.unwrap();
            if (paimon::isRuntimeShuttingDown()) return;
            bool own = AccountVerifier::get().getAccountID() == accID;
            if (auto* page = ProfilePage::create(accID, own)) {
                page->show();
            }
        });
}

$execute {
    auto reload = [](bool) {
        geode::Loader::get()->queueInMainThread([] {
            MentionWatcher::get().reloadLevels();
        });
    };
    listenForSettingChanges<bool>("mentions-enabled", reload);
    listenForSettingChanges<bool>("mentions-daily", reload);
    listenForSettingChanges<bool>("mentions-weekly", reload);
    listenForSettingChanges<bool>("mentions-event", reload);
}

$on_mod(DataSaved) {
    MentionWatcher::get().save();
}

$on_game(Loaded) {
    MentionWatcher::get().startup();
}
