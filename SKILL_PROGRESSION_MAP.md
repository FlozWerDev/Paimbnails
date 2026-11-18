# Skill Progression Map — Paimbnails (v3)

Updated analysis grounded in PR history, open issues, changelog, codebase review (903 source files), and **new evidence from the July 11–12 commits** that the v2 map (PR #6) did not cover.

> **What changed since v2 (PR #6, July 10):**
> - July 11 commit `8ca6d17` synced major new feature work: editor-suite (~60 modules), editor-history, texture-fusion, collab server. **CI was disabled to manual-only** (`workflow_dispatch`).
> - July 12 commit `b2f8813` removed private server folders from the public tree.
> - v1.1.0 (Collab Editor public launch) was promised for July 20, 2026. **It has not shipped** — latest release remains v1.0.9 (July 6). The collab feature is either delayed or still closed-beta.
> - Neither PR #5 nor PR #6 (the previous skill maps) has been merged. This file does not exist on `main`.

---

## Evidence Sources

| Source | What it tells us |
|--------|-----------------|
| **PR #1** (closed/unmerged) | Copilot-generated destructive revert (removed rapidfuzz, MSVC ICE workarounds, installer workflows, reverted to v1.0.0). The race-condition fixes in the PR body were never in the diff. All video thread-safety problems remain unfixed. |
| **PR #5** (open) | First skill map (v1). Incorrectly treated PR #1 as a legitimate fix. |
| **PR #6** (open) | Second skill map (v2). Corrected PR #1 analysis. Did not cover the July 11 feature sync or CI disablement. |
| **Issue #2** (closed) | Megahack transition customizer conflict. `TransitionHook` uses fragile `typeid` matching. Closed (Megahack presumably updated), but the structural pattern remains. |
| **Issue #3** (open) | Breaks My Levels tab. Investigation shows `MyLevelsLayoutHook.cpp` is intentionally disabled (1-line stub). Breakage likely from `LevelCell.cpp` hooks injecting into created-level cells. |
| **Issue #4** (open) | Game crash on thumbnail submission. `ThumbnailSubmissionService` has no PNG validation, no try/catch, no shutdown guards. |
| **Changelog v1.0.1–v1.0.9** | Feature expansion: thumbnails → backgrounds → shaders → agent mode → collab editor. Stability investment: `safeShutdownStep`, `EventBus::beginShutdown`, CCImage ref-counting fix. v1.0.9: Collab Editor (closed beta), Editor History, Paimon guide v1.5. |
| **July 11 commit `8ca6d17`** | New surface area: editor-suite (~60 modules), editor-history (raw `LevelEditorLayer*`), texture-fusion. CI disabled to manual-only. |
| **Codebase stats** | 903 source files, 60+ Geode `$modify` hooks, **zero test files**, `EventBus.hpp` coexists with `ModEvents.hpp`. CI is `workflow_dispatch` only. |
| **No PR reviews** | Zero reviews on all 3 PRs. No human feedback to incorporate. Suggestions are grounded entirely in code/issue/PR evidence. |

---

## 1. Re-enable CI with an automated build gate

**Evidence:**
- `.github/workflows/build.yml:3-5` — CI is disabled to manual-only (`workflow_dispatch:`). The commit message states: *"GitHub Actions only runs on manual workflow_dispatch until CI is re-enabled."* No `push:` or `pull_request:` triggers exist.
- This means **every push and PR runs zero CI** — no compile check, no build verification. The July 11 sync (editor-suite, editor-history, collab server) landed on `main` with no automated build verification.
- PR #1's "race-condition fix" was never validated because there's no CI to catch it. The same applies to all future work.
- Build actions are pinned to `@main` (`build.yml:37, :167`) and `pl_mpeg` to `master` (`CMakeLists.txt:34`) — non-reproducible; a bad upstream commit silently breaks builds.

**Actionable steps:**
1. Re-add `push:` (branches: `main`) and `pull_request:` triggers to `build.yml`. Keep `workflow_dispatch` as a manual override.
2. Pin `geode-sdk/build-geode-mod` to a specific commit SHA instead of `@main` (same for `combine@main`).
3. Pin `pl_mpeg` to a specific git tag/commit in `CMakeLists.txt:34` instead of `master`.
4. Add a `concurrency` group keyed on `github.ref` so manual runs don't conflict with push-triggered runs.
5. Once tests exist (skill #7), add a test job that gates PR merges.

**Files to modify:** `.github/workflows/build.yml`, `CMakeLists.txt`

**Why first:** Without CI, none of the fixes in skills #2–#7 can be verified mechanically. This is the foundation that makes everything else trustworthy.

---

## 2. Video decode thread-safety hardening

**Evidence:**
- `src/video/VideoDecoder.hpp:84` — `virtual void stopDecoding() = 0;` **returns `void`. CONFIRMED UNFIXED.** Callers cannot detect whether the decode thread was joined or detached.
- No generation counters exist. Each decoder uses a sticky `m_decodeThreadDetached` atomic (`DecoderMF.hpp:88`, `DecoderNDK.hpp:72`, `DecoderPLM.hpp:200`) — a containment workaround, not a fix. A detached thread from a prior decode session can still access shared codec/ring-buffer resources.
- `src/utils/TimedJoin.hpp:20-25` — explicitly documents the danger: *"The caller MUST NOT release any resource that the detached thread could still be using."* Yet `DecoderMF::closeInternal()` and `DecoderNDK::closeInternal()` delete codec objects after `timedJoin` returns `false` (detached).
- `src/utils/VideoThumbnailSprite.cpp:1273` — `player->pause()` is called **while holding `s_playerCacheMutex`**. `stop()` calls at `:1258, :1290, :1308` also hold the mutex and can block on `timedJoin` for 1–3 seconds. This blocks the Cocos2d main thread during scrolling and layer transitions.
- `VideoThumbnailSprite::s_asyncShutdown` (`:33`) is set **late** — in `clearCache()` (step 11/14 of shutdown, `RuntimeLifecycle.cpp:216`), not at the start of `$on_game(Exiting)`.
- Issue #4's crash on thumbnail submit may be the Android variant of this race (capture → encode → the video player is torn down mid-operation).

**Actionable steps:**
1. Change `IVideoDecoder::stopDecoding()` to return `bool` (`true` = joined, `false` = detached). Propagate through all four decoders (`DecoderMF`, `DecoderNDK`, `DecoderPLM`, `DecoderAVF`).
2. Add `m_generation: atomic<uint64_t>` to each decoder. `startDecoding()` increments it; the decode loop captures the generation at entry and exits at the next iteration boundary if it mismatches.
3. Add `m_threadRunning: atomic<bool>` (set `true` at loop entry, `false` on exit). In `closeInternal()`, busy-wait on it (200–500 ms cap) before releasing codec/D3D/COM resources.
4. In `VideoPlayer::stop()`, only call `seekTo(0)` if `stopDecoding()` returned `true`.
5. Reduce NDK dequeue timeouts from 10 ms → 1 ms so threads exit fast enough for `timedJoin` to succeed in practice.
6. Move `player->pause()` and eviction `stop()` **outside** `s_playerCacheMutex` in `returnPlayerToCache()` — collect items to stop under the lock, stop after releasing it. Apply the same pattern to `clearPlayerCache()`.
7. Set `VideoThumbnailSprite::s_asyncShutdown = true` at the **top** of `$on_game(Exiting)` in `RuntimeLifecycle.cpp`, before `EventBus::beginShutdown()`.

**Files to modify:** `src/video/VideoDecoder.hpp`, `src/video/platform/DecoderMF.{cpp,hpp}`, `src/video/platform/DecoderNDK.{cpp,hpp}`, `src/video/platform/DecoderPLM.hpp`, `src/video/platform/DecoderAVF.{cpp,hpp}`, `src/video/VideoPlayer.cpp`, `src/utils/VideoThumbnailSprite.cpp`, `src/core/RuntimeLifecycle.cpp`

---

## 3. Thumbnail submission crash-path hardening

**Evidence:**
- Issue #4 (open): "Game crashed when i tried to submit a level thumbnail."
- `src/features/thumbnails/services/ThumbnailSubmissionService.cpp` — account/server guards exist (`:14, :42, :18, :46`) but:
  - **No PNG validation**: `pngData` is passed straight to `HttpClient::uploadSuggestion/uploadUpdate` with no magic-byte check, no dimension check, no size cap (`:30, :58`).
  - **No try/catch**: neither upload entry points nor HTTP callbacks are wrapped (`:31-36, :59-67`). An exception (e.g. from `ThumbnailLoader::invalidateLevel` at `:62`) propagates out of the web callback and crashes the web thread.
  - **No shutdown guards**: download callbacks (`:73-77, :85-89, :95-99, :106-109`) call `ThumbnailTransportClient::bytesToTexture(data)` without checking `isRuntimeShuttingDown()` — if a download completes during teardown, it touches a dead texture cache.
- Compare to `VideoThumbnailSprite` which checks `s_asyncShutdown`/`isRuntimeShuttingDown` pervasively — the thumbnail service does not follow this pattern.

**Actionable steps:**
1. Add PNG data validation: reject empty data, enforce min size (1 KB) and max size (5 MB), check PNG magic bytes (`\x89PNG\r\n\x1a\n`) before starting the upload.
2. Wrap the entire upload method body in `try/catch` with `log::error` + `callback(false, reason)` on failure.
3. In HTTP callback lambdas, check `isRuntimeShuttingDown()` before accessing `this` or invoking the user callback.
4. In `CaptureOverlay`, validate captured `CCImage` dimensions (`> 0 × > 0`) before encoding to PNG.
5. Add crash breadcrumbs: log level ID, PNG size, and current step (encoding/uploading/callback) before each operation.

**Files to modify:** `src/features/thumbnails/services/ThumbnailSubmissionService.{cpp,hpp}`, `src/features/capture/ui/CaptureOverlay.cpp`, `src/features/capture/services/FramebufferCapture.cpp`

---

## 4. Collab Editor stability — network, pointer, and voice hardening

**Evidence:**
- v1.0.9 changelog: "Collab Editor added in closed beta. Will be available for everyone on July 20, 2026 with v1.1.0." **v1.1.0 has not shipped** (latest release is v1.0.9, July 6). The feature is either delayed or still closed-beta — hardening is urgent before public launch.
- `src/features/collab-editor/CollabManager.hpp:220` — `LevelEditorLayer* m_editor = nullptr;` **raw pointer**, not `WeakRef`. If the user exits the editor while collab is active and `clearEditor()` isn't called, `m_editor` dangles. `tick()` guards with `if (m_editor)` but a stale non-null pointer is a use-after-free.
- `src/features/collab-editor/CollabManager.hpp:221` — `CollabEditorOverlay* m_overlay` is also a raw pointer, cleared by identity compare in `clearEditor` (`CollabManager.cpp:319-323`).
- **Reconnection IS implemented** (`CollabManager::tryRecoverSession()`, `CollabManager.cpp:873-903`) and the network generation counter (`m_gen`, `CollabNetClient.hpp:89`) is correctly used in all HTTP callbacks. This is good.
- **No heartbeat/keepalive**: `CollabNetClient` uses HTTP long-poll with a 35 s timeout. A silently-dropped connection takes up to 35 s to detect.
- **Weak packet validation**: `handleMessage` (`CollabManager.cpp:529-788`) uses `unwrapOr` defaults but performs no strict schema validation — no array-size caps on `snapshot`/`op_batch` (`:599-650`), no bounds on `save` string length per op.
- `src/features/collab-editor/CollabVoice.cpp:358` — `b64Decode` on inbound voice data with **no length cap before decode**. A malicious/buggy peer could send an arbitrarily large `data` string causing a large transient allocation. The FIFO is capped *after* decode (`kFifoMaxSamples`, `:370`). No per-peer inbound frame-rate cap exists.

**Actionable steps:**
1. Replace raw `LevelEditorLayer* m_editor` in `CollabManager` with `WeakRef<LevelEditorLayer>`. Null-check before every use in `tick()`, `handleMessage()`, and all editor hooks. Do the same for `m_overlay`.
2. Add an explicit heartbeat: `CollabNetClient` sends a lightweight poll-with-empty-batch every 10 s if no data has been exchanged; the server responds with an ack. If no response within 15 s, show a "reconnecting" overlay.
3. Add packet validation in `CollabNetClient::onMessage()` / `CollabManager::handleMessage()`: reject messages with implausible sizes (> 1 MB payload), unknown message types, or arrays exceeding 10,000 entries.
4. Add a pre-decode length cap in `CollabVoice::onRemoteFrame()`: reject inbound `data` strings > 64 KB before `b64Decode`. Add a per-peer inbound frame-rate cap (max 30 frames/sec).
5. Rate-limit outgoing voice packets (max 20 packets/sec, max 320 bytes each) — currently only gated by energy VAD, not by rate.
6. Before v1.1.0 public launch, add an integration test simulating: connect → apply remote edit → disconnect → reconnect → verify state consistency.

**Files to modify:** `src/features/collab-editor/CollabManager.{cpp,hpp}`, `src/features/collab-editor/CollabNetClient.{cpp,hpp}`, `src/features/collab-editor/CollabVoice.{cpp,hpp}`, `src/features/collab-editor/hooks/CollabEditorHooks.cpp`

---

## 5. Editor-history raw-pointer UAF — same pattern as Collab, new code

**Evidence (NEW — not covered by v1 or v2 maps):**
- `src/features/editor-history/services/EditorHistoryTracker.hpp:96` — `LevelEditorLayer* m_editor = nullptr;` **raw pointer**. Same UAF pattern as `CollabManager::m_editor`. `setEditor`/`clearEditor` exist (`EditorHistoryTracker.cpp:201`) but there is no RAII tie to the editor lifecycle — if the editor is destroyed without `clearEditor` being called, every subsequent `tick()`/`pollStacks()`/`pollObject()` dereferences a dangling pointer.
- `src/features/editor-history/services/EditorHistoryTracker.hpp:97` — `std::unordered_map<UndoObject*, UndoMeta> m_meta;` **raw `UndoObject*` map key**. If an undo entry is destroyed without `prune()` running first (`EditorHistoryTracker.cpp:810`), the map contains a dangling key — lookups via `metaFor()` (`:804`) access freed memory.
- `src/features/editor-history/services/ObjectTimelineStore.hpp:104` — `LevelEditorLayer* m_editor = nullptr;` **raw pointer**, same pattern.
- Both singletons are main-thread-only by convention; no mutex. This is acceptable for GD's single-threaded UI, but the raw pointers are not.

**Actionable steps:**
1. Replace raw `LevelEditorLayer* m_editor` in `EditorHistoryTracker` and `ObjectTimelineStore` with `WeakRef<LevelEditorLayer>`. Null-check before every use.
2. Replace raw `UndoObject*` keys in `m_meta` with a stable identifier (e.g. `uint64_t` undo ID or `void*` + a liveness check via the undo stack). Alternatively, call `prune()` at the top of every method that accesses `m_meta`.
3. Add a `~EditorHistoryTracker()` / `~ObjectTimelineStore()` destructor that calls `clearEditor()` as a safety net.
4. In `EditorHistoryHooks.cpp`, ensure `clearEditor()` is called from `LevelEditorLayer::onExit` or `$on_editor(Exit)` (whichever Geode event fires on editor teardown), not just from manual button presses.

**Files to modify:** `src/features/editor-history/services/EditorHistoryTracker.{cpp,hpp}`, `src/features/editor-history/services/ObjectTimelineStore.{cpp,hpp}`, `src/features/editor-history/hooks/EditorHistoryHooks.cpp`

---

## 6. Test infrastructure — break the zero-test cycle

**Evidence:**
- **Zero test files** in the entire repository. No `test/`, `spec/`, or `__test__` directories. No `enable_testing()`, `add_test()`, or test framework in `CMakeLists.txt`.
- The only thing resembling a test is `src/features/texture-studio/engine/SelfTest.cpp` — a **runtime** self-test (`engineSelfTest()`) that logs PASS/FAIL in-process, called from `ProjectEditorLayer.cpp:879`. Not CI, not assert-based, not fail-stopping.
- PR #1's "race-condition fix" was never validated because there's no way to test it without manual testing on Android + Windows.
- The video subsystem (`VideoRingBuffer`, lock-free SPSC, adaptive slot count) is the most crash-prone area and is pure logic — perfect for unit testing without a Geode runtime.
- `LightLemmatizer` (stemming, stopwords, synonyms) is pure string logic.
- `PopupRegistry` scoring (weight + compound match + fuzzy similarity) is pure math.
- `FusionStore` has `kMagic`/`kVersion` serialization validation — testable without rendering.
- `CollabManager::handleMessage` deserialization logic is testable with crafted JSON inputs.

**Actionable steps:**
1. Add `tests/` directory with CMake option `PAIMBNAILS_ENABLE_TESTS=OFF` (default).
2. Start with 5 test TUs that compile against library headers only (no Geode runtime needed):
   - `test_VideoRingBuffer.cpp` — SPSC correctness, overflow, underflow, adaptive slot count, `waitForWritable`/`waitForReadable` with cancel.
   - `test_LightLemmatizer.cpp` — stemming, stopword removal, synonym expansion, bilingual (EN/ES).
   - `test_PopupRegistry.cpp` — weighted matching, compound keyword scoring, fuzzy fallback.
   - `test_FusionStore.cpp` — serialization round-trip, magic/version validation, corrupt-data rejection.
   - `test_CollabMessage.cpp` — `handleMessage` with valid/missing/oversized fields; verify `unwrapOr` defaults and reject-oversized behavior (once validation from skill #4 is added).
3. Use Catch2 or doctest (header-only, available via CPM).
4. Add a GitHub Actions workflow step that builds and runs tests on PR/push (ties into skill #1).

**Files to create:** `tests/CMakeLists.txt`, `tests/test_VideoRingBuffer.cpp`, `tests/test_LightLemmatizer.cpp`, `tests/test_PopupRegistry.cpp`, `tests/test_FusionStore.cpp`, `tests/test_CollabMessage.cpp`

---

## 7. Mod-compatibility runtime adaptation (downgraded from v2)

**Evidence:**
- `src/framework/compat/ModCompat.hpp` detects 12+ mods but `src/core/ModCompatWarnings.cpp` only **logs warnings** — no runtime adaptation.
- `src/features/transitions/hooks/TransitionHook.cpp` uses `isVanillaTransition()` which checks `typeid(*trans).name()` for "cocos2d" + "CCTransition" — fragile across compilers and blind to Geode mod transition subclasses.
- **However**: Issue #2 (Megahack) is **closed**, and Issue #3 (My Levels) investigation shows `MyLevelsLayoutHook.cpp` is intentionally disabled (1-line stub). The `g_suppressLevelCellEnhancements` flag is checked at 2 sites in `LevelCell.cpp` (`:3758, :3802`). The My Levels breakage appears to be from `LevelCell` hooks injecting into created-level cells, not from an active layout feature.
- This is **lower priority** than v2 ranked it: the most severe conflict (Megahack) is resolved, and the My Levels hook is inert.

**Actionable steps:**
1. Add `ModCompat::isTransitionCustomizerLoaded()` check. In `TransitionHook::shouldIntercept()`, return `false` early when detected — cede the transition feature rather than fighting for it.
2. Replace `isVanillaTransition()` with a `std::unordered_set<std::string>` of known mod transition class prefixes + a node user-data flag (`setUserData("paimbnails.transition.skip")`).
3. In `StartupIncompatibilityCheck`, add a **popup** (not just a log) when a known-incompatible mod is detected, with a one-click "disable conflicting Paimbnails feature" button.
4. For the My Levels issue (#3): add context detection in `LevelCell::loadCustomLevelCell()` — check if the parent `LevelBrowserLayer` is the "My Levels" tab and set `g_suppressLevelCellEnhancements = true`.

**Files to modify:** `src/features/transitions/hooks/TransitionHook.cpp`, `src/framework/compat/ModCompat.hpp`, `src/core/ModCompatWarnings.cpp`, `src/core/StartupIncompatibilityCheck.cpp`, `src/hooks/LevelCell.cpp`

---

## 8. Shutdown robustness — close remaining gaps (downgraded from v2)

**Evidence:**
- **Mostly addressed**: `RuntimeLifecycle.cpp:109` calls `EventBus::get().beginShutdown()` **first** in `$on_game(Exiting)`, then sets `s_runtimeShuttingDown` (`:111`). The WeakRef-atexit crash is explicitly handled.
- `EventBus.hpp:99-108` — `beginShutdown()` clears all subscriber lists while Cocos is still alive. `publish()` (`:111-124`) double-checks `m_shuttingDown` and early-returns. Safe.
- 13 of 14 `EventBus::subscribe` sites capture `WeakRef` correctly.
- **1 site uses a raw pointer**: `CustomSongWidget.cpp:457` captures raw `widget` and guards with `paimon::csw::Lifecycle::isAlive(widget)` (`:460`) — a custom liveness registry, riskier than `WeakRef`.
- **Late `s_asyncShutdown`** for video: set in `clearCache()` (step 11/14 of shutdown), not at the start. Some video paths check the global `isRuntimeShuttingDown()` flag, but the video-specific `s_asyncShutdown` guards cache/queue paths and is late. (This is addressed in skill #2, step 7.)

**Actionable steps:**
1. Replace the raw-pointer capture in `CustomSongWidget.cpp:457` with `WeakRef<CustomSongWidget>` + null-check at dispatch time, matching the pattern used by the other 13 subscribe sites.
2. The late `s_asyncShutdown` gap is closed by skill #2, step 7 (set it at the top of `$on_game(Exiting)`).
3. Audit `HttpClient` response handlers, `ThreadPool` tasks, and `PBOUploader` completion handlers for `isRuntimeShuttingDown()` guards. Add the guard where missing.

**Files to modify:** `src/hooks/CustomSongWidget.cpp`, `src/utils/HttpClient.cpp`, `src/video/PBOUploader.cpp`

---

## Summary Priority Matrix

| # | Skill | Root Evidence | Impact | Effort | Order |
|---|-------|---------------|--------|--------|-------|
| 1 | Re-enable CI + build gate | `build.yml:5` manual-only; no compile check on PRs | 🔴 Critical — no verification of any change | Low (re-add triggers, pin deps) | **1st** |
| 2 | Video thread-safety | `VideoDecoder.hpp:84` void stop; `VideoThumbnailSprite.cpp:1273` pause-under-mutex; no generation counters | 🔴 Critical — active crash on Android + Windows | Medium | **2nd** |
| 3 | Thumbnail submit crash | Issue #4; `ThumbnailSubmissionService.cpp` no validation/try-catch/shutdown guards | 🔴 Critical — crash on submit | Low | **3rd** |
| 4 | Collab Editor stability | v1.1.0 overdue; `CollabManager.hpp:220` raw `m_editor`; `CollabVoice.cpp:358` unbounded decode; no heartbeat | 🟠 High — UAF/DoS in networked feature pre-launch | Medium | **4th** |
| 5 | Editor-history UAF | NEW; `EditorHistoryTracker.hpp:96-97` raw ptrs + raw map keys | 🟠 High — UAF in newly shipped code | Low-Medium | **5th** |
| 6 | Test infrastructure | Zero tests; only runtime `SelfTest.cpp` | 🟡 Medium — prevents all future regressions | Medium (setup) | **6th** |
| 7 | Mod-compat runtime adaptation | Issues #2 (closed), #3; `ModCompatWarnings` log-only | 🟡 Medium — lower now that Megahack is resolved | Medium | **7th** |
| 8 | Shutdown robustness | `CustomSongWidget.cpp:457` raw-ptr capture; late `s_asyncShutdown` | 🟡 Low-Medium — mostly addressed in v1.0.5 | Low | **8th** |

**Recommended execution order:** 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8

**Rationale:**
- **#1 first** — CI is the foundation. Without it, none of the fixes below can be verified. It's also the cheapest (re-add triggers, pin deps).
- **#2–#3 next** — active crashes (video race + thumbnail submit). These are user-facing crashes reported in issues.
- **#4–#5** — UAF surfaces in the two most recent feature areas (collab editor + editor-history). Collab is overdue for public launch. Editor-history is new code with the same raw-pointer pattern.
- **#6** — test infrastructure prevents all of the above from regressing. Placed after the critical fixes so the first tests can cover the fixed code.
- **#7–#8** — lower priority. Mod-compat's worst conflict (Megahack) is resolved. Shutdown is mostly addressed; only 1 raw-ptr site and the late `s_asyncShutdown` remain (the latter is closed by skill #2).

---

## Delta from v2 (PR #6)

| v2 skill | v3 status | Reason |
|----------|-----------|--------|
| #1 Video thread-safety | **Unchanged — still #2** | Confirmed unfixed via codebase review |
| #2 Mod-compat runtime adaptation | **Downgraded #7→#8** | Issue #2 closed; My Levels hook is inert stub |
| #3 My Levels tab context guards | **Merged into #7** | `MyLevelsLayoutHook.cpp` is intentionally disabled; suppression flag exists at 2 sites; not a standalone skill |
| #4 Thumbnail submit crash | **Unchanged — still #3** | Confirmed unfixed |
| #5 Collab Editor stability | **Unchanged — still #4** | Partially addressed (reconnection); raw ptr + voice + heartbeat open |
| #6 Shutdown robustness | **Downgraded to #8** | Mostly addressed; only 1 raw-ptr site + late flag remain |
| #7 Test infrastructure | **Unchanged — still #6** | Zero tests confirmed |
| — **NEW: CI re-enablement** | **Added as #1** | v2 did not cover the July 11 CI disablement; biggest process risk |
| — **NEW: Editor-history UAF** | **Added as #5** | v2 predates the July 11 editor-history sync; raw `LevelEditorLayer*` + raw `UndoObject*` keys |
