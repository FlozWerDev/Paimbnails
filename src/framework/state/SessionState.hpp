#pragma once

// Transient state reset on game close; persistent keys stay in SavedValue.

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

    VerificationContext verification;

    static bool consumeFlag(bool& flag) {
        bool was = flag;
        flag = false;
        return was;
    }

    static int consumeInt(int& value, int resetTo = -1) {
        int was = value;
        value = resetTo;
        return was;
    }

private:
    SessionState() = default;
    ~SessionState() = default;
    SessionState(SessionState const&) = delete;
    SessionState& operator=(SessionState const&) = delete;
};

} // namespace paimon
