#include "../src/features/twitch-requests/services/RequestVerificationFlow.hpp"

#include <cassert>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace paimon::twitch;

struct Harness {
    VerificationIdentity identity{17046382, "FlozWer"};
    bool current = true;
    bool deletionSucceeds = true;
    bool pending = false;
    bool delayedUpload = false;
    bool delayedCheck = false;
    bool badCode = false;
    int posts = 0, checks = 0, reconnects = 0, stored = 0, remembered = 0;
    std::vector<int> deleted;
    std::vector<std::string> sequence;
    std::deque<std::function<void()>> scheduled;
    VerificationActions::UploadCallback uploadCallback;
    VerificationActions::CheckCallback checkCallback;
    std::shared_ptr<RequestVerificationFlow> flow;

    explicit Harness(int pendingComment = 0) {
        VerificationActions actions;
        actions.isCurrentAccount = [this] { return current; };
        actions.requestCode = [this](auto callback) {
            callback({true, identity, badCode ? "contenido inesperado" : "PAI-ABC234", {}});
        };
        actions.publishComment = [this](std::string const& code, auto callback) {
            assert(code == "PAI-ABC234");
            ++posts;
            if (delayedUpload) uploadCallback = std::move(callback);
            else callback(true, 777, {});
        };
        actions.checkComment = [this](auto callback) {
            ++checks;
            if (delayedCheck) checkCallback = std::move(callback);
            else callback({!pending, pending, identity, pending ? "" : "signed-viewer-token", {}});
        };
        actions.deleteComment = [this](int id, auto callback) {
            deleted.push_back(id);
            sequence.push_back("delete");
            callback(deletionSucceeds, deletionSucceeds ? "" : "No se pudo borrar");
        };
        actions.storeToken = [this](std::string const& token) {
            assert(token == "signed-viewer-token");
            ++stored;
            sequence.push_back("store");
        };
        actions.rememberComment = [this](int id, std::string const&) { remembered = id; };
        actions.reconnect = [this] { ++reconnects; };
        actions.schedule = [this](std::function<void()> callback) { scheduled.push_back(std::move(callback)); };
        flow = std::make_shared<RequestVerificationFlow>(identity, std::move(actions), pendingComment);
    }
};

int main() {
    {
        Harness h;
        h.flow->start(VerificationMode::Manual);
        assert(h.posts == 0 && h.deleted.empty() && h.stored == 0);
        assert(h.flow->code() == "PAI-ABC234");
        h.flow->check();
        assert(h.flow->verified() && h.stored == 1 && h.reconnects == 1);
        assert(h.posts == 0 && h.deleted.empty());
    }
    {
        Harness h;
        h.flow->start(VerificationMode::Automatic);
        assert(h.flow->verified() && h.posts == 1 && h.checks == 1);
        assert(h.deleted == std::vector<int>{777});
        assert(h.sequence == (std::vector<std::string>{"store", "delete"}));
        assert(h.remembered == 0 && h.flow->commentID() == 0);
    }
    {
        Harness h;
        h.pending = true;
        h.flow->start(VerificationMode::Manual);
        h.flow->check();
        assert(!h.flow->verified() && h.scheduled.empty() && h.deleted.empty());
        h.pending = false;
        h.flow->check();
        assert(h.flow->verified() && h.stored == 1);
    }
    {
        Harness h;
        h.pending = true;
        h.flow->start(VerificationMode::Automatic);
        h.pending = false;
        assert(h.scheduled.size() == 1);
        auto next = std::move(h.scheduled.front()); h.scheduled.pop_front(); next();
        assert(h.flow->verified() && h.posts == 1 && h.checks == 2);
    }
    {
        Harness h;
        h.delayedUpload = true;
        h.flow->start(VerificationMode::Automatic);
        h.flow->start(VerificationMode::Automatic);
        h.flow->cancel();
        h.uploadCallback(true, 777, {});
        assert(h.posts == 1 && h.checks == 0 && h.stored == 0);
        assert(h.deleted == std::vector<int>{777});
    }
    {
        Harness h;
        h.delayedCheck = true;
        h.flow->start(VerificationMode::Automatic);
        h.current = false;
        h.checkCallback({true, false, h.identity, "signed-viewer-token", {}});
        assert(h.stored == 0 && !h.flow->verified() && h.deleted.empty());
        assert(h.flow->commentID() == 777 && h.remembered == 777);
        h.current = true;
        h.flow->retryCleanup();
        assert(h.deleted == std::vector<int>{777});
    }
    {
        Harness h;
        h.delayedCheck = true;
        h.flow->start(VerificationMode::Automatic);
        h.checkCallback({true, false, {123, "Other"}, "signed-viewer-token", {}});
        assert(h.stored == 0 && !h.flow->verified());
        assert(h.deleted == std::vector<int>{777});
    }
    {
        Harness h;
        h.deletionSucceeds = false;
        h.flow->start(VerificationMode::Automatic);
        assert(h.flow->verified() && h.stored == 1 && h.flow->commentID() == 777);
        assert(!h.flow->busy());
        h.deletionSucceeds = true;
        h.flow->retryCleanup();
        assert(h.flow->verified() && h.flow->commentID() == 0 && h.stored == 1 && h.posts == 1);
    }
    {
        Harness h(888);
        h.flow->start(VerificationMode::Automatic);
        assert(h.posts == 0 && h.flow->commentID() == 888);
        h.flow->retryCleanup();
        assert(h.deleted == std::vector<int>{888});
    }
    {
        Harness h;
        h.pending = true;
        h.flow->start(VerificationMode::Automatic);
        int guard = 0;
        while (!h.scheduled.empty() && ++guard < 100) {
            auto next = std::move(h.scheduled.front()); h.scheduled.pop_front(); next();
        }
        assert(guard < 100 && !h.flow->verified() && h.stored == 0);
        assert(h.deleted == std::vector<int>{777});
    }
    {
        Harness h;
        h.delayedCheck = true;
        h.flow->start(VerificationMode::Manual);
        h.flow->check();
        h.flow->cancel();
        h.checkCallback({true, false, h.identity, "signed-viewer-token", {}});
        assert(h.stored == 0 && h.deleted.empty() && h.posts == 0);
    }
    {
        Harness h;
        h.badCode = true;
        h.flow->start(VerificationMode::Automatic);
        assert(h.posts == 0 && h.checks == 0 && !h.flow->verified());
    }
    {
        Harness h;
        h.delayedCheck = true;
        h.flow->start(VerificationMode::Manual);
        h.flow->check();
        h.flow->cancel();
        h.flow->start(VerificationMode::Automatic);
        assert(h.posts == 0);
        h.checkCallback({true, false, h.identity, "signed-viewer-token", {}});
        assert(h.stored == 0 && !h.flow->busy());
    }
    {
        Harness h(888);
        h.flow->acknowledgeManualCleanup();
        assert(h.flow->commentID() == 0 && h.remembered == 0 && h.deleted.empty());
        h.flow->start(VerificationMode::Manual);
        assert(h.flow->stage() == VerificationStage::Waiting && h.posts == 0);
    }
    {
        Harness h;
        h.pending = true;
        h.flow->start(VerificationMode::Automatic);
        assert(h.scheduled.size() == 1);
        h.flow->cancel();
        h.pending = false;
        h.flow->start(VerificationMode::Manual);
        auto stale = std::move(h.scheduled.front()); h.scheduled.pop_front(); stale();
        assert(h.checks == 1 && h.stored == 0 && h.flow->stage() == VerificationStage::Waiting);
    }
    assert(RequestVerificationFlow::validCode("PAI-ABC234"));
    assert(!RequestVerificationFlow::validCode("publish arbitrary text"));
    assert(!RequestVerificationFlow::validCode("PAI-ABC234\n"));
}
