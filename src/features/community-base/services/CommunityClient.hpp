#pragma once

// paimon base community: client for /api/community/* on the paimbnails worker.
// data lives in cloudflare d1 and media in bunny; every write needs the gd
// account proven with the viewer token (profile comment) or the mod code.

#include <Geode/Geode.hpp>
#include <matjson.hpp>
#include <optional>
#include <string>
#include <vector>

namespace paimon::community {

// spanish or english depending on the mod language.
char const* tx(char const* spanish, char const* english);

struct Identity {
    int accountID = 0;
    std::string username;
};

// the logged-in gd account, or nothing when the player is offline/guest.
std::optional<Identity> currentIdentity();
// true when the client holds a viewer token or a mod code to prove the account.
bool hasAccountProof();

// readable message for an httpclient failure ("HTTP <code>: <body>").
std::string describeError(std::string const& response);
// the server asked for account verification (IDENTITY_REQUIRED).
bool needsVerification(std::string const& response);
bool isNotFound(std::string const& response);

// opens the gd account verification popup (viewer token).
void promptVerification();

// shows a popup above everything in the running scene; profile popups use very
// high z-orders, so a plain show() can end up underneath them.
void showOnTop(FLAlertLayer* popup);

using JsonCallback = geode::CopyableFunction<void(bool ok, matjson::Value const& json, std::string const& error)>;
using BytesCallback = geode::CopyableFunction<void(bool ok, std::vector<uint8_t> const& data)>;

class CommunityClient {
public:
    static CommunityClient& get();

    // cloud settings
    void saveSettings(matjson::Value const& payload, int schema, JsonCallback cb);
    void loadSettings(JsonCallback cb);
    void settingsInfo(JsonCallback cb);
    void deleteSettings(JsonCallback cb);

    // public items: "presets", "backgrounds", "gradients", "icons". `filter` is the
    // preset category or the gamemode; `sort` is "new", "popular" or "top".
    void listItems(std::string const& type, std::string const& filter, std::string const& sort, int offset, int limit, JsonCallback cb);
    void getItem(std::string const& type, std::string const& id, JsonCallback cb);
    // json submissions (presets, gradients, icons); backgrounds go through submitBackground.
    void submitItem(std::string const& type, matjson::Value body, JsonCallback cb);
    void myItems(std::string const& type, JsonCallback cb);
    void deleteItem(std::string const& type, std::string const& id, JsonCallback cb);
    // value: 1 like, -1 dislike, 0 remove. answers with likes, dislikes and myVote.
    void vote(std::string const& type, std::string const& id, int value, JsonCallback cb);
    // counts one download per account; silently skipped without account proof.
    void countDownload(std::string const& type, std::string const& id, JsonCallback cb = nullptr);

    void submitBackground(std::string const& title, std::vector<uint8_t> const& image,
        std::string const& contentType, JsonCallback cb);

    // moderation ("presets", "backgrounds", "gradients" or "icons")
    void moderationSummary(JsonCallback cb);
    void moderationQueue(std::string const& type, int offset, JsonCallback cb);
    void review(std::string const& type, std::string const& id, bool approve,
        std::string const& reason, JsonCallback cb);
    void moderationMedia(std::string const& mediaPath, BytesCallback cb);

    // global icon
    void uploadAsset(std::string const& kind, std::vector<uint8_t> const& data, JsonCallback cb);
    void publishIcons(matjson::Value body, JsonCallback cb);
    void clearIcons(JsonCallback cb);

    // public media served by the worker (approved backgrounds, previews, assets).
    void downloadPublic(std::string const& url, BytesCallback cb);

    // url of an approved comment background.
    static std::string backgroundUrl(std::string const& id);

private:
    CommunityClient() = default;
    void getJson(std::string const& endpoint, JsonCallback cb);
    void postJson(std::string const& endpoint, matjson::Value body, JsonCallback cb);
};

bool isCommunityId(std::string_view id);

} // namespace paimon::community
