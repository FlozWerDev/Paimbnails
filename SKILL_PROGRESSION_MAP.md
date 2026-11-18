# Skill Progression Map — Paimbnails (v2)

Updated analysis grounded in PR history, open issues, changelog, and full codebase review.

---

## Evidence Sources

| Source | What it tells us |
|--------|-----------------|
| **PR #1** (closed/unmerged, `copilot/review-mod-for-improvements`) | Despite its title, the actual diff **reverts** the codebase: removes rapidfuzz, drops MSVC ICE workarounds for 15+ TUs, removes installer/packaging jobs, reverts version to 1.0.0. The race-condition fixes described in the PR body were **never in the diff** — they were hallucinated by Copilot. This means all video thread-safety problems remain unfixed. |
| **PR #5** (open, `tembo/skill-progression-map`) | Previous skill map. Its analysis of PR #1 was incorrect (treated the Copilot PR as a legitimate fix). This update corrects that and adds new evidence from v1.0.9. |
| **Issue #2** (closed, "Breaks Megahack's transition customizer") | `TransitionHook.cpp` intercepts all `CCTransitionScene` instances via `CCDirector::replaceScene`/`pushScene`/`popSceneWithTransition`. The `isVanillaTransition()` guard uses `typeid` name matching — fragile across compilers and blind to other Geode mods' transition subclasses. |
| **Issue #3** (open, "Breaks my levels tab") | Screenshot shows broken My Levels layout. `MyLevelsLayoutHook.cpp` is entirely disabled (single comment: "no layout changes applied here"). Breakage comes from `LevelCell.cpp` hooks which inject thumbnails/video/badges without a context guard for the "My Levels" (created-levels) context where cells have a different structure. |
| **Issue #4** (open, "Game crashed on thumbnail submission") | Crash in the submit flow. `ThumbnailSubmissionService` has HookInterceptor validation but no defensive guards on PNG data or the HTTP callback itself. |
| **Changelog v1.0.1–v1.0.9** | Feature expansion: thumbnails → backgrounds → shaders → agent mode → collab editor. Stability investment: `safeShutdownStep`, `EventBus::beginShutdown`, CCImage ref-counting fix. New surface area: Collab Editor (beta, v1.0.9) adds real-time networking, voice, presence. |
| **Codebase stats** | 793 source files, 60+ Geode `$modify` hooks, zero test files, `EventBus.hpp` coexists with `ModEvents.hpp`. |

---

## 1. Video decode thread-safety hardening

**Evidence:**
- PR #1's body described race conditions between decode threads and main-thread teardown (generation counters, `stopDecoding()→bool`, `m_threadRunning` atomic, mutex-free cache operations) but **none of these changes appear in the actual diff**. The codebase still has the pre-fix pattern.
- `IVideoDecoder::stopDecoding()` returns `void` (src/video/VideoDecoder.hpp:66). There is no generation counter, no `m_threadRunning` atomic in any decoder.
- `TimedJoin.hpp` (src/utils/TimedJoin.hpp) can detach threads that still access shared codec/ring-buffer resources. The doc comment explicitly warns: "The caller MUST NOT release any resource that the detached thread could still be using" — but `DecoderMF::closeInternal()` and `DecoderNDK::closeInternal()` delete codec objects after `timedJoin` returns `false`.
- `VideoThumbnailSprite::returnPlayerToCache()` calls `player->pause()` while holding `s_playerCacheMutex`, blocking the main thread during layer transitions (the exact ANR pattern PR #1 described).
- Issue #4's "crash on submit" may be the Android variant of the same `startDecoding()`/`closeInternal()` race.

**Actionable steps:**
1. Change `IVideoDecoder::stopDecoding()` to return `bool` (`true` = joined, `false` = detached). Propagate through all three decoders.
2. Add `m_generation: atomic<uint64_t>` to `DecoderMF`, `DecoderNDK`, `DecoderPLM`. Each `startDecoding()` increments; the decode loop captures the generation at entry and exits at the next iteration boundary if it mismatches.
3. Add `m_threadRunning: atomic<bool>` (set `true` at loop entry, `false` on exit). In `closeInternal()`, busy-wait on it (200–500 ms cap) before releasing codec/D3D/COM resources.
4. In `VideoPlayer::stop()`, only call `seekTo(0)` if `stopDecoding()` returned `true` (thread was joined).
5. Reduce NDK dequeue timeouts from 10 ms → 1 ms so threads exit fast enough for `timedJoin` to succeed.
6. Move `player->pause()` and eviction `stop()` **outside** `s_playerCacheMutex` in `VideoThumbnailSprite::returnPlayerToCache()` — collect items to stop under the lock, stop after releasing it.

**Files to modify:** `src/video/VideoDecoder.hpp`, `src/video/platform/DecoderMF.cpp`, `src/video/platform/DecoderNDK.cpp`, `src/video/platform/DecoderPLM.hpp`, `src/video/VideoPlayer.cpp`, `src/utils/VideoThumbnailSprite.cpp`

---

## 2. Mod-compatibility runtime adaptation (not just logging)

**Evidence:**
- `TransitionHook.cpp` uses `isVanillaTransition()` which checks `typeid(*trans).name()` for the string "cocos2d" + "CCTransition". This is:
  - Fragile: MSVC and GCC produce different `typeid` names (the code comments acknowledge this).
  - Blind to Geode mods: a mod that subclasses `CCTransitionFade` will have a `typeid` that doesn't contain "cocos2d" (it contains the mod's namespace), so the hook will **skip** it. But Megahack's transition customizer likely **replaces** the transition scene before our hook runs, meaning we intercept the customizer's transition instead of the original.
- `ModCompat.hpp` detects 12+ mods but `ModCompatWarnings.cpp` only **logs warnings** — no runtime adaptation. When a conflicting mod is loaded, Paimbnails continues to run its hooks and the user sees breakage.
- `HookConventions.hpp` provides `afterNodeIdsOrLate()` and `afterAllPaimonUiOrVeryLate()`, but a grep shows **every hook file** already uses these conventions correctly. The issue isn't hook priority — it's that Paimbnails' features are structurally incompatible with some mods (e.g., both try to replace transitions).
- Issue #2 was closed (Megahack presumably updated), but the underlying pattern remains: any mod that replaces transitions will conflict.

**Actionable steps:**
1. Add `ModCompat::isMegahackTransitionLoaded()` (or a generic "transition customizer loaded" check). In `TransitionHook::shouldIntercept()`, return `false` early when such a mod is detected — **cede the entire transition feature** rather than fighting for it.
2. Replace `isVanillaTransition()` with a more robust check: store a `std::unordered_set<std::string>` of known Geode mod transition class prefixes, and also check for a node user-data flag (`setUserData("paimbnails.transition.skip")`) that other mods can set.
3. In `StartupIncompatibilityCheck`, add a **popup** (not just a log) when a known-incompatible mod is detected, with a one-click "disable conflicting Paimbnails feature" button that writes to settings.
4. For the My Levels issue (#3): add `ModCompat::isMyLevelsConflictMod()` to detect mods that restructure created-level cells, and auto-suppress LevelCell enhancements in that context.

**Files to modify:** `src/features/transitions/hooks/TransitionHook.cpp`, `src/framework/compat/ModCompat.hpp`, `src/core/ModCompatWarnings.cpp`, `src/core/StartupIncompatibilityCheck.cpp`, `src/core/Settings.hpp`

---

## 3. My Levels tab — context-aware LevelCell enhancement suppression

**Evidence:**
- Issue #3 screenshot shows broken layout in the "My Levels" tab.
- `MyLevelsLayoutHook.cpp` is completely disabled: the entire file is a single comment `// Disabled: My Levels cells keep their original appearance; no layout changes applied here.`
- `LevelCell.cpp` is ~3700+ lines. `g_suppressLevelCellEnhancements` is declared (line 55) but only checked in **2 places** (lines 3663, 3707), likely not covering all the injection points.
- The "My Levels" context uses `LevelCell::loadCustomLevelCell()` (for created levels) which is also hooked by Paimbnails. The cell structure in this context is different (no online metadata, different layout) — injecting thumbnail sprites, video players, and badges at positions calculated for online cells will corrupt the layout.
- `LevelCellContext.hpp` exists but doesn't distinguish the "My Levels / created levels" context.

**Actionable steps:**
1. In `LevelCell::loadCustomLevelCell()`, detect the "created levels" context (check if parent `LevelBrowserLayer` is the "My Levels" tab — e.g., `LevelBrowserLayer::get()->m_searchObject->m_searchType` or the layer's scene ID) and set `g_suppressLevelCellEnhancements = true` at the top of the hook.
2. Audit every feature injection in `LevelCell.cpp` (thumbnail, video, badge, gradient, separator, dark overlay, particles) and ensure each one checks `g_suppressLevelCellEnhancements` before modifying the cell. Currently only 2 of ~8 injection sites check it.
3. Add a new settings toggle "Enhance My Levels cells" (default: off) so users can opt into enhancements once the layout is fixed.
4. Test with the "My Levels" tab open and verify cells render correctly without Paimbnails modifications.

**Files to modify:** `src/hooks/LevelCell.cpp`, `src/hooks/LevelCellContext.hpp`, `src/core/Settings.hpp`

---

## 4. Thumbnail submission crash path hardening

**Evidence:**
- Issue #4: "Game crashed when i tried to submit a level thumbnail" — open, no body.
- `ThumbnailSubmissionService::uploadSuggestion()` and `uploadUpdate()` check `m_serverEnabled` and account ID, but:
  - No validation on `pngData` size (could be empty or multi-MB).
  - The HTTP callback captures `this` and `callback` — if the service is destroyed while a request is in-flight, the callback accesses freed memory.
  - `HookInterceptor::runPreHooks` can throw, but there's no try/catch around the entire upload path.
- The capture pipeline (`CaptureOverlay` → `FramebufferCapture` → `SceneCapture`) writes `CCImage` data on the main thread. If a GL context is lost during capture (e.g., the user switches apps on Android), `CCImage::saveToFile()` could produce corrupt/empty data.
- `HttpClient::get().uploadSuggestion()` fires a `geode::CopyableFunction` callback from a web thread — if the callback throws, it crashes the web thread with no recovery.

**Actionable steps:**
1. Add PNG data validation: reject empty data, enforce min size (1 KB) and max size (5 MB) before starting the upload.
2. Wrap the entire upload method body in `try/catch` with `log::error` + `callback(false, reason)` on failure.
3. In the HTTP callback lambdas, check `isRuntimeShuttingDown()` before accessing `this` or invoking the user callback.
4. In `CaptureOverlay`, validate the captured `CCImage` dimensions (`> 0 × > 0`) before encoding to PNG.
5. Add crash breadcrumbs: log level ID, PNG size, and the current step (encoding/uploading/callback) before each operation.

**Files to modify:** `src/features/thumbnails/services/ThumbnailSubmissionService.cpp`, `src/features/capture/ui/CaptureOverlay.cpp`, `src/features/capture/services/FramebufferCapture.cpp`

---

## 5. Collab Editor stability — network & state resilience

**Evidence:**
- v1.0.9 changelog: "Collab Editor added in closed beta. Will be available for everyone on July 20, 2026 with v1.1.0."
- `CollabNetClient.cpp` implements WebSocket communication with a Render server. Network disconnections mid-operation could leave the editor in an inconsistent state (remote objects partially applied).
- `CollabManager` tracks `m_editor` (raw pointer to `LevelEditorLayer*`) — if the user exits the editor while collab is active, the pointer dangles. There's `clearEditor()` but no RAII tie to the editor lifecycle.
- `CollabPresence` and `CollabVoice` add more network surface area — voice data must be rate-limited and validated to prevent malformed packets from crashing the client.
- No test coverage for any of the collab code.

**Actionable steps:**
1. Replace raw `LevelEditorLayer*` in `CollabManager` with `WeakRef<LevelEditorLayer>` (or a `geode::Ref` if the layer is retained). Null-check before every use.
2. Add a heartbeat/keepalive to `CollabNetClient` — if the server doesn't respond within 10 seconds, show a "reconnecting" overlay and queue local changes for replay on reconnect.
3. In `CollabManager::disconnect()`, ensure all pending remote operations are flushed or rolled back before clearing state. Add a `CollabManager::isApplyingRemote()` guard in editor hooks to prevent re-entrancy (already exists at line 44 of `CollabManager.hpp` but needs auditing at all hook sites).
4. Add packet validation in `CollabNetClient::onMessage()` — reject messages with implausible sizes or unknown message types before parsing.
5. Rate-limit outgoing voice packets (max 20 packets/sec, max 320 bytes each).
6. Before v1.1.0 public launch, add an integration test that simulates: connect → apply remote edit → disconnect → reconnect → verify state consistency.

**Files to modify:** `src/features/collab-editor/CollabManager.cpp`, `src/features/collab-editor/CollabNetClient.cpp`, `src/features/collab-editor/CollabVoice.cpp`, `src/features/collab-editor/hooks/CollabEditorHooks.cpp`

---

## 6. Shutdown robustness — eliminate dangling-reference crashes

**Evidence:**
- `EventBus.hpp` comment (line 76): "Their lambdas capture `WeakRef<CCNode>`; destroying them during atexit (EventBus dtor) hits an invalid `WeakRefPool` → crash." The `beginShutdown()` call in `$on_game(Exiting)` is the mitigation, but it depends on running **before** Cocos starts destroying nodes.
- `RuntimeLifecycle.cpp` calls `EventBus::get().beginShutdown()` as the **very first** step in `$on_game(Exiting)`, which is correct — but there are other paths where static destructors run (DLL unload on Windows, `atexit` on other platforms) that bypass this.
- `VideoThumbnailSprite::s_asyncShutdown` is set in `clearCache()` but not in the `$on_game(Exiting)` handler — async I/O pool threads could still be running when the process exits.
- `PBOUploader`, `ThreadPool`, and `HttpClient` have background threads that may outlive the Cocos runtime if the process is killed.

**Actionable steps:**
1. Audit every `EventBus::subscribe` callback for `WeakRef<CCNode>` captures. Replace with `WeakRef` + null-check at dispatch time (not just at subscribe time). Count: grep shows ~30+ subscribe sites across the codebase.
2. In `RuntimeLifecycle.cpp`, add `VideoThumbnailSprite::s_asyncShutdown = true` at the top of `$on_game(Exiting)` (before `EventBus::beginShutdown()`).
3. Add `isRuntimeShuttingDown()` guards in every async callback: `HttpClient` response handlers, `ThreadPool` tasks, `PBOUploader` completion handlers.
4. For the EventBus itself, migrate to `geode::Event` which handles lifecycle automatically (mod-scoped listeners are destroyed on mod unload). Start with the highest-risk subscribers (those capturing `WeakRef<CCNode>`).

**Files to modify:** `src/core/RuntimeLifecycle.cpp`, `src/framework/EventBus.hpp`, `src/utils/VideoThumbnailSprite.cpp`, `src/utils/HttpClient.cpp`, `src/video/PBOUploader.cpp`

---

## 7. Test infrastructure — break the zero-test cycle

**Evidence:**
- **Zero test files** in the entire repository (no `test/`, `spec/`, or `__test__`).
- PR #1's "race-condition fix" was never validated because there's no way to test it without manual testing on Android + Windows.
- The video subsystem (`VideoRingBuffer`, lock-free SPSC, adaptive slot count) is the most crash-prone area and is pure logic — perfect for unit testing without a Geode runtime.
- `LightLemmatizer` (stemming, stopwords, synonyms) is pure string logic.
- `PopupRegistry` scoring (weight + compound match + fuzzy similarity) is pure math.

**Actionable steps:**
1. Add `tests/` directory with CMake option `PAIMBNAILS_ENABLE_TESTS=OFF` (default).
2. Start with 4 test TUs that compile against library headers only (no Geode runtime needed):
   - `test_VideoRingBuffer.cpp` — single-producer/single-consumer correctness, overflow, underflow, adaptive slot count, `waitForWritable`/`waitForReadable` with cancel.
   - `test_LightLemmatizer.cpp` — stemming, stopword removal, synonym expansion, bilingual (EN/ES).
   - `test_PopupRegistry.cpp` — weighted matching, compound keyword scoring, fuzzy fallback.
   - `test_MenuPhysicsWorld.cpp` — step a physics world with known bodies, assert final positions.
3. Use Catch2 (header-only) or doctest — both available via CPM.
4. Add a GitHub Actions workflow step that builds and runs tests on PR/push.

**Files to create:** `tests/CMakeLists.txt`, `tests/test_VideoRingBuffer.cpp`, `tests/test_LightLemmatizer.cpp`, `tests/test_PopupRegistry.cpp`, `tests/test_MenuPhysicsWorld.cpp`, `.github/workflows/build.yml` (add test step)

---

## Summary Priority Matrix

| # | Skill | Root Evidence | Impact | Effort | Recommended Order |
|---|-------|---------------|--------|--------|-------------------|
| 1 | Video thread-safety hardening | PR #1 (fix never landed), Issue #4 crash, `TimedJoin` detach-then-free pattern | 🔴 Critical — active crash on Android + Windows | Medium (pattern described in PR #1 body) | **1st** |
| 2 | Mod-compat runtime adaptation | Issues #2, #3, `ModCompatWarnings` log-only | 🟠 High — user-visible breakage with other mods | Medium | **3rd** |
| 3 | My Levels tab context guards | Issue #3, disabled `MyLevelsLayoutHook.cpp`, 2/8 injection sites guarded | 🟠 High — broken tab | Low | **4th** |
| 4 | Thumbnail submit crash hardening | Issue #4, no validation in submission path | 🔴 Critical — crash on submit | Low | **2nd** |
| 5 | Collab Editor stability | v1.0.9 new code, raw `LevelEditorLayer*`, no network resilience | 🟡 Medium — beta now, critical by v1.1.0 launch | Medium | **5th** |
| 6 | Shutdown robustness | EventBus WeakRef crash, `s_asyncShutdown` gap | 🟡 Medium — intermittent crash on exit | Medium | **6th** |
| 7 | Test infrastructure | Zero tests, no regression prevention | 🟡 Medium — long-term stability | Medium (initial setup) | **7th** |

**Recommended execution order:** 1 → 4 → 3 → 2 → 5 → 6 → 7

Rationale: Fix active crashes first (1, 4), then the broken UI (3, 2), then stabilize the upcoming v1.1.0 feature (5), then harden shutdown (6), and finally build the test foundation that prevents all of these from regressing (7).
