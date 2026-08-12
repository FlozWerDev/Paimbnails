#pragma once

#include "../ThumbAlerts.hpp"

#include <matjson.hpp>

#include <deque>
#include <string>

namespace paimon::thumbalerts {

// Polls /api/latest-uploads and hands whatever is new to the alert queue. The
// ids already announced live in the save file, so reopening the game does not
// replay the same cards.
class NewThumbWatcher {
public:
    static NewThumbWatcher& get();

    void startup();
    void pollNow();

    // A level this client just uploaded. The card was already shown off the
    // upload reply, so the feed entry is recorded silently when it turns up.
    void suppressLevel(int levelId);

    // One frame from the live socket. Goes through the same dedup as the poll,
    // so whichever arrives second is dropped. Main thread only.
    void onPushMessage(std::string const& message);

private:
    NewThumbWatcher() = default;

    void scheduleNextPoll();
    void onResponse(std::string const& body);
    // False when the entry is malformed, already known, or one of ours.
    bool acceptEntry(matjson::Value const& entry, NewThumb& out, bool& marked);
    void loadSeen();
    void saveSeen();
    bool markSeen(std::string const& eventId);

    bool m_started = false;
    bool m_inFlight = false;
    bool m_loaded = false;
    std::deque<std::string> m_seen;
    std::deque<int> m_selfUploads;
};

} // namespace paimon::thumbalerts
