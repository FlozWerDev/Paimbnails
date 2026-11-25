#include "../src/features/twitch-requests/RequestQueue.hpp"
#include "../src/features/twitch-requests/TwitchRequestParser.hpp"

#include <cassert>

using namespace paimon::twitch;

namespace {

LevelRequest request(int64_t entryID, int levelID, std::string queue, bool played = false) {
    LevelRequest result;
    result.entryID = entryID;
    result.levelID = levelID;
    result.queue = std::move(queue);
    result.played = played;
    return result;
}

void viewsRespectQueueAndReviewStatus() {
    auto const pending = request(1, 100, "General");
    auto const reviewed = request(2, 200, "VIP", true);
    assert(matchesRequestView(pending, "", RequestView::All));
    assert(matchesRequestView(pending, "General", RequestView::Pending));
    assert(!matchesRequestView(pending, "General", RequestView::Reviewed));
    assert(!matchesRequestView(reviewed, "General", RequestView::Reviewed));
    assert(matchesRequestView(reviewed, "VIP", RequestView::Reviewed));
}

void cleanupKeepsPendingAndOtherQueues() {
    std::vector requests{request(1, 100, "General"), request(2, 200, "General", true),
        request(3, 300, "VIP", true), request(4, 400, "VIP")};
    assert(removeReviewedRequests(requests, "General") == 1);
    assert(requests.size() == 3);
    assert(requests[0].entryID == 1 && requests[1].entryID == 3 && requests[2].entryID == 4);
    assert(removeReviewedRequests(requests, "General") == 0);
    assert(removeReviewedRequests(requests, "") == 1);
    assert(requests.size() == 2 && !requests[0].played && !requests[1].played);
}

void exportKeepsPendingOrderWithoutDuplicateLevels() {
    std::vector requests{request(1, 100, "General"), request(2, 200, "General", true),
        request(3, 100, "VIP"), request(4, 300, "VIP"), request(5, 400, "General")};
    assert((pendingLevelIDs(requests, "") == std::vector<int>{100, 300, 400}));
    assert((pendingLevelIDs(requests, "General") == std::vector<int>{100, 400}));
    assert(pendingLevelIDs(requests, "Missing").empty());
}

void actionsFindTheSameEntryAfterRemovalOrReordering() {
    std::vector requests{request(1, 100, "General"), request(2, 100, "VIP"),
        request(3, 300, "General")};
    requests.erase(requests.begin());
    assert(requestIndexByEntryID(requests, 2) == 0);
    std::swap(requests[0], requests[1]);
    assert(requestIndexByEntryID(requests, 2) == 1);
    assert(!requestIndexByEntryID(requests, 1));
    assert(!requestIndexByEntryID(requests, 0));
}

void insertionHonorsCapacityAndQueueDuplicatePolicy() {
    std::vector requests{request(1, 100, "General", true)};
    QueueLimits const limits{2, true};
    assert(validateQueueInsert(requests, request(0, 100, "General"), limits) == "duplicate");
    assert(validateQueueInsert(requests, request(0, 100, "VIP"), limits).empty());
    assert(validateQueueInsert(requests, request(0, 0, "General"), limits) == "invalid");
    assert(validateQueueInsert(requests, request(0, 200, "General"), {1, true}) == "full");
    assert(validateQueueInsert(requests, request(0, 100, "General"), {2, false}).empty());
}

void manualInputPreservesDescriptionAndVideo() {
    auto parsed = parseRequestBody("123456 prueba manual https://youtu.be/abcd");
    assert(parsed && parsed->levelID == 123456);
    assert(parsed->description == "prueba manual");
    assert(parsed->url == "https://youtu.be/abcd");
    assert(!parseRequestBody("0"));
    assert(!parseRequestBody("2147483648"));
    assert(!parseRequestBody("un nivel"));
}

} // namespace

int main() {
    viewsRespectQueueAndReviewStatus();
    cleanupKeepsPendingAndOtherQueues();
    exportKeepsPendingOrderWithoutDuplicateLevels();
    actionsFindTheSameEntryAfterRemovalOrReordering();
    insertionHonorsCapacityAndQueueDuplicatePolicy();
    manualInputPreservesDescriptionAndVideo();
}
