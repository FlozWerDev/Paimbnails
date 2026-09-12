import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";

const read = (path) => readFile(new URL(`../${path}`, import.meta.url), "utf8");

const [lifecycle, delay, capture, compat, collabHooks, collabManager, eventBus,
       fileDialog, iconShare, updateChecker, thumbnailCache, collabNet] = await Promise.all([
    read("src/core/RuntimeLifecycle.cpp"),
    read("src/utils/MainThreadDelay.hpp"),
    read("src/features/capture/services/FramebufferCapture.cpp"),
    read("src/framework/compat/ModCompat.hpp"),
    read("src/features/collab-editor/hooks/CollabEditorHooks.cpp"),
    read("src/features/collab-editor/CollabManager.cpp"),
    read("src/framework/EventBus.hpp"),
    read("src/utils/FileDialog.cpp"),
    read("src/features/icon-maker/services/IconShare.cpp"),
    read("src/features/updates/services/UpdateChecker.cpp"),
    read("src/features/thumbnails/services/ThumbnailCache.cpp"),
    read("src/features/collab-editor/CollabNetClient.cpp"),
]);

const delayCancel = lifecycle.indexOf("cancelAllMainThreadDelays()");
const threadShutdown = lifecycle.indexOf("ThreadTracker::get().shutdown()");
assert.ok(delayCancel >= 0 && delayCancel < threadShutdown,
    "delayed callbacks must be destroyed before worker/Cocos teardown");
assert.match(delay, /registry\(\).*new std::unordered_set/s);
assert.match(delay, /void cancelAllMainThreadDelays\(\)/);

assert.match(eventBus, /std::atomic<bool> m_shuttingDown/);
assert.match(eventBus, /if \(m_shuttingDown\.load\(std::memory_order_acquire\)\) return 0/);

assert.match(fileDialog, /FilePickHolder& s_filePickHolder = \*new FilePickHolder/);
assert.match(iconShare, /PickHolder& s_pickHolder = \*new PickHolder/);
assert.match(thumbnailCache, /static auto\* instance = new ThumbnailCache/);
assert.match(collabManager, /static auto\* instance = new CollabManager/);
assert.match(collabNet, /!paimon::isRuntimeShuttingDown\(\)/);
for (const task of ["m_checkTask", "m_releasesTask", "m_downloadTask"]) {
    assert.match(updateChecker, new RegExp(`${task}\\.cancel\\(\\)`));
}

assert.match(compat, /needsConservativeGameplayCapture/);
assert.match(capture, /External gameplay renderer detected/);
assert.match(capture, /s_deferredCallbacks\.push_back\([\s\S]*std::move\(previousRequestCallback\)/);
assert.doesNotMatch(capture,
    /previousRequestCallback\(false, nullptr, nullptr, 0, 0\)/,
    "a replaced capture must not re-enter its old callback synchronously");

for (const fragileHook of ["undoLastAction", "redoLastAction", "selectObject",
                           "selectObjects", "deselectAll", "deselectObject"]) {
    assert.doesNotMatch(collabHooks, new RegExp(`void ${fragileHook}\\(`),
        `Collab must not own the shared EditorUI::${fragileHook} hook`);
}
assert.match(collabManager, /void CollabManager::pollLocalSelection\(\)/);
assert.match(collabManager, /if \(\+\+m_selectionPollTicks < 2\) return/);

console.log("PASS: crash lifecycle, capture and editor compatibility guards");
