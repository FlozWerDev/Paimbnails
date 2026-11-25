#include "../src/core/UnifiedAuthFlow.hpp"

#include <cassert>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace paimon::auth;

struct Harness {
    AuthIdentity identity{17046382, "FlozWer"};
    bool current = true;
    bool deletionSucceeds = true;
    bool pending = false;
    bool delayedUpload = false;
    bool delayedComplete = false;
    std::string serverCode = "PAI-AUTH-ABCDEF2345";
    std::string serverToken = "psess_v3_17046382_ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmno12";
    int posts = 0, completes = 0, stored = 0, onLoggedInCount = 0, remembered = 0;
    std::vector<int> deleted;
    std::vector<std::string> sequence;
    std::deque<std::function<void()>> scheduled;
    LoginActions::PublishCallback uploadCb;
    LoginActions::CompleteCallback completeCb;
    SessionRoles lastRoles;
    std::shared_ptr<UnifiedAuthFlow> flow;

    explicit Harness(int pendingComment = 0) {
        // Ensure default serverToken has exact 43-char secret part
        serverToken = "psess_v3_17046382_" + std::string(43, 'A');
        LoginActions actions;
        actions.isCurrentAccount = [this] { return current; };
        actions.startChallenge = [this](auto cb) {
            cb({true, identity, "signed.challenge.token", serverCode, {}});
        };
        actions.publishComment = [this](std::string const& code, auto cb) {
            assert(UnifiedAuthFlow::validProfileCode(code));
            ++posts;
            if (delayedUpload) uploadCb = std::move(cb);
            else cb(true, 901, {});
        };
        actions.completeChallenge = [this](std::string const& tok, auto cb) {
            assert(tok == "signed.challenge.token");
            ++completes;
            SessionRoles roles;
            roles.authenticated = true;
            roles.isAdmin = true;
            roles.isModerator = true;
            roles.isVip = true;
            roles.primaryRole = "admin";
            roles.permissions = {"community.write", "queue.review", "admin.manage_roles"};
            if (delayedComplete) {
                completeCb = std::move(cb);
            } else {
                cb({!pending, pending, identity, pending ? "" : serverToken, 9999999999999LL, roles, {}});
            }
        };
        actions.deleteComment = [this](int id, auto cb) {
            deleted.push_back(id);
            sequence.push_back("delete");
            cb(deletionSucceeds, deletionSucceeds ? "" : "Delete failed");
        };
        actions.storeSession = [this](std::string const& token, AuthIdentity const& id, int64_t, SessionRoles const& roles) {
            assert(token == serverToken);
            assert(id.accountID == identity.accountID);
            lastRoles = roles;
            ++stored;
            sequence.push_back("store");
        };
        actions.rememberComment = [this](int id) { remembered = id; };
        actions.onLoggedIn = [this] { ++onLoggedInCount; };
        actions.schedule = [this](std::function<void()> cb) { scheduled.push_back(std::move(cb)); };
        flow = std::make_shared<UnifiedAuthFlow>(identity, std::move(actions), pendingComment);
    }
};

int main() {
    // 1. Token and profile code validators reject malformed/forged inputs
    assert(UnifiedAuthFlow::validProfileCode("PAI-AUTH-ABCDEF2345"));
    assert(!UnifiedAuthFlow::validProfileCode("PAI-ABC234"));
    assert(!UnifiedAuthFlow::validProfileCode("PAI-AUTH-ABCDEF2345\n"));
    assert(!UnifiedAuthFlow::validProfileCode("arbitrary profile text"));
    assert(!UnifiedAuthFlow::validProfileCode("PAI-AUTH-01OIABCDEF")); // forbidden ambiguous chars 0,1,O,I

    std::string const goodToken = "psess_v3_17046382_" + std::string(43, 'a');
    assert(UnifiedAuthFlow::validSessionToken(goodToken, 17046382));
    assert(!UnifiedAuthFlow::validSessionToken(goodToken, 999999)); // wrong accountID
    assert(!UnifiedAuthFlow::validSessionToken("psess_v3_17046382_" + std::string(42, 'a'), 17046382)); // too short
    assert(!UnifiedAuthFlow::validSessionToken("psess_v3_17046382_" + std::string(43, '!'), 17046382)); // bad chars
    assert(!UnifiedAuthFlow::validSessionToken("pmc_v2_" + std::string(43, 'a'), 17046382)); // legacy format

    // 2. Automatic 1-click flow: posts code, completes with roles, stores session, then deletes ONLY its comment
    {
        Harness h;
        h.flow->start(LoginMode::Automatic);
        assert(h.flow->loggedIn());
        assert(h.posts == 1 && h.completes == 1 && h.stored == 1 && h.onLoggedInCount == 1);
        assert(h.deleted == std::vector<int>{901});
        assert(h.sequence == (std::vector<std::string>{"store", "delete"}));
        assert(h.remembered == 0 && h.flow->commentID() == 0);
        assert(h.lastRoles.isAdmin && h.lastRoles.hasModAccess() && h.lastRoles.hasPermission("admin.manage_roles"));
        assert(h.flow->challengeToken().empty()); // cleared after completion
    }

    // 3. Manual flow: never posts or deletes automatically, confirms cleanly
    {
        Harness h;
        h.flow->start(LoginMode::Manual);
        assert(h.posts == 0 && h.deleted.empty() && h.stored == 0);
        assert(h.flow->stage() == LoginStage::Waiting);
        assert(h.flow->code() == "PAI-AUTH-ABCDEF2345");
        h.flow->confirm();
        assert(h.flow->loggedIn() && h.stored == 1 && h.onLoggedInCount == 1);
        assert(h.posts == 0 && h.deleted.empty());
    }

    // 4. SECURITY: refuses to post arbitrary/tampered server code to GD profile
    {
        Harness h;
        h.serverCode = "visit evil.example.com";
        h.flow->start(LoginMode::Automatic);
        assert(h.posts == 0 && h.completes == 0 && h.stored == 0 && !h.flow->loggedIn());
    }

    // 5. SECURITY: refuses session token bound to a different accountID
    {
        Harness h;
        h.serverToken = "psess_v3_999999_" + std::string(43, 'B');
        h.flow->start(LoginMode::Automatic);
        assert(h.stored == 0 && !h.flow->loggedIn());
        assert(h.deleted == std::vector<int>{901}); // still cleans up temporary comment!
    }

    // 6. SECURITY: refuses response with mismatched identity
    {
        Harness h;
        h.delayedComplete = true;
        h.flow->start(LoginMode::Automatic);
        SessionRoles r;
        r.authenticated = true;
        h.completeCb({true, false, {123, "Other"}, h.serverToken, 9999999999999LL, r, {}});
        assert(h.stored == 0 && !h.flow->loggedIn());
        assert(h.deleted == std::vector<int>{901});
    }

    // 7. SECURITY: aborts and never stores session if GD account switched mid-flow
    {
        Harness h;
        h.delayedComplete = true;
        h.flow->start(LoginMode::Automatic);
        h.current = false;
        SessionRoles r;
        r.authenticated = true;
        h.completeCb({true, false, h.identity, h.serverToken, 9999999999999LL, r, {}});
        assert(h.stored == 0 && !h.flow->loggedIn() && h.deleted.empty());
        assert(h.flow->commentID() == 901 && h.remembered == 901);
        // Switching back allows cleanup
        h.current = true;
        h.flow->retryCleanup();
        assert(h.deleted == std::vector<int>{901});
    }

    // 8. Cancel during in-flight GD publish still cleans up the comment once GD returns ID
    {
        Harness h;
        h.delayedUpload = true;
        h.flow->start(LoginMode::Automatic);
        h.flow->cancel();
        h.uploadCb(true, 901, {});
        assert(h.completes == 0 && h.stored == 0 && h.deleted == std::vector<int>{901});
    }

    // 9. Stale scheduled poll callback after cancel + restart is ignored
    {
        Harness h;
        h.pending = true;
        h.flow->start(LoginMode::Automatic);
        assert(h.scheduled.size() == 1);
        h.flow->cancel();
        h.pending = false;
        h.flow->start(LoginMode::Manual);
        auto stale = std::move(h.scheduled.front());
        h.scheduled.pop_front();
        stale();
        assert(h.completes == 1 && h.stored == 0 && h.flow->stage() == LoginStage::Waiting);
    }

    return 0;
}
