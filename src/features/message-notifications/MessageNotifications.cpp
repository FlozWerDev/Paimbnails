// Message Notifications — notifica cuando recibes un mensaje nuevo o una
// solicitud de amistad. Adaptado del mod de BlueToadMaker
// (https://github.com/BlueToadMakerr/Message-Notification) a las convenciones
// de Paimbnails.
//
// El timing del polling corre en un hilo de background (solo duerme y dispara);
// las peticiones a los servidores de GD se despachan en el main thread via
// WebHelper y todo el estado vive exclusivamente en el main thread.

#include <Geode/Geode.hpp>
#include <Geode/binding/AchievementNotifier.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/LevelEditorLayer.hpp>
#include <Geode/utils/general.hpp>
#include <Geode/utils/string.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "../../utils/WebHelper.hpp"
#include "../../utils/ThreadTracker.hpp"
#include "../../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;

namespace {

namespace gstr = geode::utils::string;

constexpr char const* GD_BASE   = "https://www.boomlings.com/database/";
constexpr char const* GD_SECRET = "Wmfd2893gb7";

inline bool    sBool(char const* k) { return Mod::get()->getSettingValue<bool>(k); }
inline int64_t sInt(char const* k)  { return Mod::get()->getSettingValue<int64_t>(k); }

// Decodifica base64 URL-safe (los datos de GD usan '-' y '_'). Tolera tambien
// el alfabeto estandar '+' '/' y padding/whitespace.
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
        if (bits >= 8) { bits -= 8; out.push_back((char)((value >> bits) & 0xFF)); }
    }
    return out;
}

inline int toInt(std::string const& s) {
    return geode::utils::numFromString<int>(s).unwrapOr(-1);
}

// Parsea "k:v:k:v..." quedandose solo con las claves que nos interesan.
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
                case 4: m.title = base64UrlDecode(str); break;
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
                case 35: f.message = base64UrlDecode(str); break;
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

    // Llamado en $on_game(Loaded). Arranca el hilo de polling una sola vez.
    void startup() {
        if (m_started) return;
        m_started = true;

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
        std::reverse(items.begin(), items.end()); // ascendente para contar nuevos
        int latest = Mod::get()->getSavedValue<int>("msgnotif-latest-id", 0);
        int count = 0;
        MessageData last;
        for (auto const& s : items) {
            auto d = MessageData::parse(s);
            if (d.messageID > latest) { latest = d.messageID; last = d; count++; }
        }
        Mod::get()->setSavedValue<int>("msgnotif-latest-id", latest);
        // Primer arranque: solo registra el baseline, no notifica.
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

    // Separa la parte de datos (antes del '#') en items '|'-separados.
    static std::vector<std::string> topLevelItems(std::string const& data) {
        auto hash = gstr::split(data, "#");
        if (hash.empty()) return {};
        return gstr::split(hash[0], "|");
    }

    // Main thread. Dispara una peticion de mensajes y otra de solicitudes.
    void pollOnce() {
        if (!sBool("msgnotif-enabled")) return;
        if (PlayLayer::get() && sBool("msgnotif-disable-while-playing")) return;
        if (LevelEditorLayer::get() && sBool("msgnotif-disable-while-editing")) return;

        auto* acc = GJAccountManager::sharedState();
        if (!acc || acc->m_accountID <= 0) return;
        std::string gjp2(acc->m_GJP2);
        if (gjp2.empty()) return;

        std::string body = fmt::format("accountID={}&gjp2={}&secret={}",
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

    // POST a GD. El callback corre en el main thread (lo garantiza WebHelper).
    void request(char const* endpoint, std::string const& body,
                 std::function<void(std::string)> cb) {
        auto req = web::WebRequest();
        req.timeout(std::chrono::seconds(15));
        req.header("Content-Type", "application/x-www-form-urlencoded");
        req.userAgent("");
        req.bodyString(body);
        WebHelper::dispatch(std::move(req), "POST", std::string(GD_BASE) + endpoint,
            [cb = std::move(cb)](web::WebResponse res) {
                if (!res.ok()) return;
                cb(res.string().unwrapOr(""));
            });
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
