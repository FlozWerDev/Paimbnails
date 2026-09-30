#include "../../utils/Base64.hpp"
// polls robtop's public message endpoints through achievementnotifier;
// independent reimplementation, no code copied from the inspiring mod.

#include <Geode/Geode.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "../../utils/GDRobTopCache.hpp"
#include "../../utils/ThreadTracker.hpp"
#include "../../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;

namespace {

namespace gstr = geode::utils::string;

constexpr char const* GD_SECRET = "Wmfd2893gb7";

inline bool    sBool(char const* k) { return Mod::get()->getSettingValue<bool>(k); }
inline int64_t sInt(char const* k)  { return Mod::get()->getSettingValue<int64_t>(k); }

inline int toInt(std::string const& s) {
    return geode::utils::numFromString<int>(s).unwrapOr(-1);
}

// parse "k:v:k:v..." keeping only the keys we care about.
struct MessageData {
    int messageID = -1;
    std::string title, username;
    static MessageData parse(std::string const& data) {
        MessageData m;
        auto split = gstr::split(data, ":");
        int key = -1;
        for (auto const& str : split) {
            if (key == -1) { key = toInt(str); continue; }
            switch (key) {
                case 1: m.messageID = toInt(str); break;
                case 4: m.title = paimon::base64UrlDecode(str); break;
                case 6: m.username = str; break;
            }
            key = -1;
        }
        return m;
    }
};

struct FriendData {
    int requestID = -1;
    std::string username, message;
    static FriendData parse(std::string const& data) {
        FriendData f;
        auto split = gstr::split(data, ":");
        int key = -1;
        for (auto const& str : split) {
            if (key == -1) { key = toInt(str); continue; }
            switch (key) {
                case 1:  f.username = str; break;
                case 32: f.requestID = toInt(str); break;
                case 35: f.message = paimon::base64UrlDecode(str); break;
            }
            key = -1;
        }
        return f;
    }
};

class MessageWatcher {
public:
    static MessageWatcher& get() {
        static MessageWatcher inst;
        return inst;
    }

    void startup() {
        if (m_started) return;
        m_started = true;

        listenForSettingChanges<bool>("msgnotif-enabled", [](bool enabled) {
            if (enabled) MessageWatcher::get().pollOnce();
        });

        // seed at once: waiting for the first tick would make the first new
        // message the baseline and suppress its notification.
        pollOnce();

        paimon::ThreadTracker::get().spawn([] {
            geode::utils::thread::setName("PaimonMsgNotif");
            while (!shuttingDown()) {
                int rate = (int)std::clamp<int64_t>(sInt("msgnotif-check-interval"), 60, 3600);
                for (int i = 0; i < rate; ++i) {
                    if (shuttingDown()) return;
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
                geode::Loader::get()->queueInMainThread([] {
                    if (paimon::isRuntimeShuttingDown()) return;
                    MessageWatcher::get().pollOnce();
                });
            }
        });
    }

    void onMessageResponse(std::string const& data) {
        auto items = topLevelItems(data);
        std::reverse(items.begin(), items.end()); // ascending order to count new ones
        int latest = Mod::get()->getSavedValue<int>("msgnotif-latest-id", 0);
        int count = 0;
        MessageData last;
        for (auto const& s : items) {
            auto d = MessageData::parse(s);
            if (d.messageID > latest) { latest = d.messageID; last = d; count++; }
        }
        Mod::get()->setSavedValue<int>("msgnotif-latest-id", latest);
        // first run: just record baseline, don't notify.
        if (!Mod::get()->setSavedValue<bool>("msgnotif-seeded-msg", true)) return;
        if (count > 1) {
            showNotif(fmt::format("{} New Messages!", count), "Check them out!", false);
        } else if (count == 1) {
            showNotif(fmt::format("New Message from: {}", last.username), last.title, false);
        }
    }

    void onFriendResponse(std::string const& data) {
        auto items = topLevelItems(data);
        std::reverse(items.begin(), items.end());
        int latest = Mod::get()->getSavedValue<int>("msgnotif-latest-request-id", 0);
        int count = 0;
        FriendData last;
        for (auto const& s : items) {
            auto d = FriendData::parse(s);
            if (d.requestID > latest) { latest = d.requestID; last = d; count++; }
        }
        Mod::get()->setSavedValue<int>("msgnotif-latest-request-id", latest);
        if (!Mod::get()->setSavedValue<bool>("msgnotif-seeded-req", true)) return;
        if (count > 1) {
            showNotif(fmt::format("{} New Friend Requests!", count), "Check them out!", true);
        } else if (count == 1) {
            showNotif(fmt::format("{} sent you a Friend Request!", last.username), last.message, true);
        }
    }

private:
    static bool shuttingDown() {
        return paimon::isRuntimeShuttingDown() || paimon::ThreadTracker::get().isShuttingDown();
    }

    static std::vector<std::string> topLevelItems(std::string const& data) {
        auto hash = gstr::split(data, "#");
        if (hash.empty()) return {};
        return gstr::split(hash[0], "|");
    }

    // main thread.
    void pollOnce() {
        if (!sBool("msgnotif-enabled")) return;
        if (PlayLayer::get() && sBool("msgnotif-disable-while-playing")) return;
        if (LevelEditorLayer::get() && sBool("msgnotif-disable-while-editing")) return;

        auto* acc = GJAccountManager::sharedState();
        if (!acc || acc->m_accountID <= 0) return;
        std::string gjp2(acc->m_GJP2);
        if (gjp2.empty()) return;

        std::string body = fmt::format("accountID={}&gjp2={}&page=0&total=0&getSent=0&secret={}",
                                       acc->m_accountID, gjp2, GD_SECRET);

        if (!sBool("msgnotif-stop-messages")) {
            request("getGJMessages20.php", body, [](std::string b) {
                MessageWatcher::get().onMessageResponse(b);
            });
        }
        if (!sBool("msgnotif-stop-friends")) {
            request("getGJFriendRequests20.php", body, [](std::string b) {
                MessageWatcher::get().onFriendResponse(b);
            });
        }
    }

    // live polling: not cached.
    void request(char const* endpoint, std::string const& body,
                 std::function<void(std::string)> cb) {
        paimon::gd::postCached(
            endpoint,
            body,
            [cb = std::move(cb)](bool ok, std::string response) {
                if (!ok || !cb) return;
                cb(std::move(response));
            },
            paimon::gd::policyForEndpoint(endpoint)
        );
    }

    void showNotif(std::string const& title, std::string const& msg, bool friendIcon) {
        if (auto* n = AchievementNotifier::sharedState()) {
            n->notifyAchievement(title.c_str(), msg.c_str(),
                friendIcon ? "accountBtn_friends_001.png" : "accountBtn_messages_001.png",
                true);
        }
    }

    bool m_started = false;
};

} // namespace

$on_game(Loaded) {
    MessageWatcher::get().startup();
}
