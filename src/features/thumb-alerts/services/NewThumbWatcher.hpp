#pragma once

#include "../ThumbAlerts.hpp"

#include <matjson.hpp>

#include <deque>
#include <string>

namespace paimon::thumbalerts {

// polls latest-uploads into the alert queue; announced ids persist so reopening replays nothing.
class NewThumbWatcher {
public:
    static NewThumbWatcher& get();

    void startup();
    void pollNow();

    // just-uploaded level: card already shown off the reply, feed entry records silently.
    void suppressLevel(int levelId);

    // live socket frame: same dedup as the poll, second arrival dropped. main thread only.
    void onPushMessage(std::string const& message);

private:
    NewThumbWatcher() = default;

    void scheduleNextPoll();
    void onResponse(std::string const& body);
    // false for malformed, known, or own entries.
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
