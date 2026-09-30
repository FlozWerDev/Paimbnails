#include "RequestFeed.hpp"

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/HttpClient.hpp"
#include "../../../utils/JsonHelper.hpp"

#include <Geode/Geode.hpp>

#include <fmt/format.h>
#include <algorithm>

using namespace geode::prelude;

namespace paimon::thumbreq {

namespace {

constexpr size_t kMaxRequests = 100;

std::string stringField(matjson::Value const& item, char const* key) {
    return item.contains(key) ? item[key].asString().unwrapOr("") : "";
}

int intField(matjson::Value const& item, char const* key) {
    return paimon::json::integerOr<int>(item[key]);
}

Status statusOf(std::string const& name) {
    if (name == "sent") return Status::Sent;
    if (name == "rejected") return Status::Rejected;
    return Status::Pending;
}

bool acceptEntry(matjson::Value const& entry, Request& out) {
    if (!entry.isObject()) return false;

    out.levelId = intField(entry, "levelId");
    out.id = stringField(entry, "id");
    if (out.levelId <= 0 || out.id.empty()) return false;

    out.levelName = stringField(entry, "levelName");
    if (out.levelName.empty()) out.levelName = fmt::format("Level {}", out.levelId);
    out.mode = stringField(entry, "mode");
    out.difficulty = stringField(entry, "difficulty");
    out.video = stringField(entry, "video");
    out.requester = stringField(entry, "requester");
    out.status = statusOf(stringField(entry, "status"));
    out.sentDifficulty = stringField(entry, "sentDifficulty");
    out.sentTier = std::clamp(intField(entry, "sentTier"), 0, 4);
    return true;
}

} // namespace

int difficultyFace(std::string const& name) {
    static std::pair<std::string_view, int> const table[] = {
        {"Auto", -1}, {"Unrated", 0}, {"Easy", 1}, {"Normal", 2}, {"Hard", 3},
        {"Harder", 4}, {"Insane", 5}, {"Demon", 6}, {"Easy Demon", 7},
        {"Medium Demon", 8}, {"Hard Demon", 6}, {"Insane Demon", 9},
        {"Extreme Demon", 10},
    };
    for (auto const& [key, value] : table) {
        if (name == key) return value;
    }
    return 0;
}

void RequestFeed::fetch(std::string const& status, ListCallback callback) {
    if (!callback || paimon::isRuntimeShuttingDown()) return;
    std::string endpoint = fmt::format("/api/requests/list?limit={}", kMaxRequests);
    if (!status.empty()) endpoint += "&status=" + HttpClient::encodeQueryParam(status);

    HttpClient::get().get(endpoint, [status, callback = std::move(callback)](bool ok, std::string const& body) {
        auto& feed = RequestFeed::get();
        if (paimon::isRuntimeShuttingDown()) return;

        if (!ok) {
            log::warn("[ThumbRequests] Could not load the request list");
            callback(false, feed.m_cached[status]);
            return;
        }

        auto parsed = matjson::parse(body);
        if (!parsed) {
            log::warn("[ThumbRequests] Malformed request list: {}", parsed.unwrapErr());
            callback(false, feed.m_cached[status]);
            return;
        }

        auto const& root = parsed.unwrap();
        if (!root.isObject() || !root["requests"].isArray()) {
            callback(false, feed.m_cached[status]);
            return;
        }

        std::vector<Request> requests;
        requests.reserve(std::min(kMaxRequests, root["requests"].size()));
        for (auto const& entry : root["requests"]) {
            if (requests.size() >= kMaxRequests) break;
            Request request;
            if (acceptEntry(entry, request)) requests.push_back(std::move(request));
        }

        feed.m_cached[status] = std::move(requests);
        callback(true, feed.m_cached[status]);
    });
}

} // namespace paimon::thumbreq
