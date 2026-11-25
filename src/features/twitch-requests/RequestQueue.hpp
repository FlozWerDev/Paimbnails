#pragma once

#include "LevelRequest.hpp"

#include <algorithm>
#include <optional>
#include <span>
#include <unordered_set>

namespace paimon::twitch {

enum class RequestView { All, Pending, Reviewed };

inline bool matchesRequestView(LevelRequest const& request, std::string_view queue, RequestView view) {
    if (!queue.empty() && request.queue != queue) return false;
    if (view == RequestView::Pending) return !request.played;
    if (view == RequestView::Reviewed) return request.played;
    return true;
}

inline std::optional<size_t> requestIndexByEntryID(
    std::span<LevelRequest const> requests, int64_t entryID) {
    if (entryID <= 0) return std::nullopt;
    for (size_t index = 0; index < requests.size(); ++index) {
        if (requests[index].entryID == entryID) return index;
    }
    return std::nullopt;
}

inline size_t removeReviewedRequests(std::vector<LevelRequest>& requests, std::string_view queue) {
    return std::erase_if(requests, [queue](LevelRequest const& request) {
        return matchesRequestView(request, queue, RequestView::Reviewed);
    });
}

inline std::vector<int> pendingLevelIDs(std::span<LevelRequest const> requests, std::string_view queue) {
    std::vector<int> ids;
    std::unordered_set<int> seen;
    for (auto const& request : requests) {
        if (matchesRequestView(request, queue, RequestView::Pending) && seen.insert(request.levelID).second) {
            ids.push_back(request.levelID);
        }
    }
    return ids;
}

struct QueueLimits {
    size_t capacity;
    bool blockDuplicates;
};

inline std::string_view validateQueueInsert(std::span<LevelRequest const> requests,
    LevelRequest const& incoming, QueueLimits limits) {
    if (incoming.levelID <= 0) return "invalid";
    if (limits.blockDuplicates && std::ranges::any_of(requests, [&incoming](LevelRequest const& request) {
        return request.queue == incoming.queue && request.levelID == incoming.levelID;
    })) return "duplicate";
    if (requests.size() >= limits.capacity) return "full";
    return {};
}

} // namespace paimon::twitch
