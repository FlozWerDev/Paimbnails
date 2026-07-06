#pragma once

// SessionState.hpp — Typed session state for navigation flow. Replaces transient
// Mod::get()->setSavedValue() keys with an explicit singleton that resets on game
// close. Persistent keys (user config, server status) stay in SavedValue.

#include <string>

namespace paimon {

struct VerificationContext {
    bool openFromThumbs       = false;
    bool openFromReport       = false;
    bool openFromQueue        = false;
    bool reopenQueue          = false;
    bool fromReportPopup      = false;
    int  queueLevelID         = -1;
    int  queueCategory        = -1;   // PendingCategory enum
    int  verificationCategory = -1;   // for popups
};

class SessionState {
public:
    static SessionState& get() {
        static SessionState instance;
        return instance;
    }

    int         currentListID          = 0;
    std::string lastNavigationOrigin;

    VerificationContext verification;

    // Consume a one-shot flag: reads and resets it in one call.
    static bool consumeFlag(bool& flag) {
        bool was = flag;
        flag = false;
        return was;
    }

    // Consume a one-shot int: returns the value and resets it to -1.
    static int consumeInt(int& value, int resetTo = -1) {
        int was = value;
        value = resetTo;
        return was;
    }

    // Reset all verification state (e.g. on leaving the moderation flow).
    void resetVerification() {
        verification = VerificationContext{};
    }

    void resetAll() {
        currentListID = 0;
        lastNavigationOrigin.clear();
        resetVerification();
    }

private:
    SessionState() = default;
    ~SessionState() = default;
    SessionState(SessionState const&) = delete;
    SessionState& operator=(SessionState const&) = delete;
};

} // namespace paimon
