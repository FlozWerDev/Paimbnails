#include "../src/features/capture/services/CaptureOperationState.hpp"

#include <cassert>
#include <iostream>

using paimon::capture::CaptureOperationState;

int main() {
    CaptureOperationState capture;
    assert(!capture.active());
    assert(!capture.owns(0));

    auto first = capture.begin();
    assert(first && capture.owns(first));
    assert(capture.begin() == 0); // duplicate clicks cannot replace the pending request
    assert(capture.owns(first));
    assert(capture.finish(first));
    assert(!capture.finish(first)); // a second queued completion is ignored
    assert(!capture.cancel()); // an idle popup does not own another capture

    auto timedOut = capture.begin();
    assert(capture.cancel());
    auto newer = capture.begin();
    assert(newer != timedOut);
    assert(!capture.finish(timedOut)); // an old failure/success cannot release the new request
    assert(capture.owns(newer));
    assert(capture.finish(newer));

    auto closed = capture.begin();
    assert(capture.cancel());
    assert(!capture.finish(closed)); // closing the popup invalidates its in-flight callback
    assert(!capture.active());
    assert(!capture.cancel());

    // Exercise repeated close/reopen and timeout/retry sequences.
    for (int i = 0; i < 10000; ++i) {
        auto old = capture.begin();
        capture.cancel();
        auto current = capture.begin();
        assert(!capture.finish(old));
        assert(capture.owns(current));
        assert(capture.finish(current));
    }
    std::cout << "Capture operation regression checks passed.\n";
}
