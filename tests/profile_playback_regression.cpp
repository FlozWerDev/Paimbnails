#include "../src/features/profile-music/services/ProfilePlaybackRequest.hpp"

#include <cassert>

int main() {
    using Request = paimon::profile_music::ProfilePlaybackRequest;
    Request request;
    auto loading = request.begin(42, 1);
    assert(request.current(loading, 1, true));
    request.cancel(); // Close before configuration or download completes.
    assert(!request.current(loading, 1, true));
    auto reopened = request.begin(42, 2);
    assert(!request.current(loading, 2, true));
    assert(request.current(reopened, 2, true));
    assert(!request.current(reopened, 2, false));
    assert(!request.current(reopened, 3, true));
    auto updated = request.begin(42, 2); // New config in the same profile session.
    assert(!request.current(reopened, 2, true));
    assert(request.current(updated, 2, true));
    auto other = request.begin(84, 3);
    assert(!request.current(updated, 3, true));
    assert(request.current(other, 3, true));
    request.cancel();
    request.cancel();
    assert(!request.current(other, 3, true));
}
