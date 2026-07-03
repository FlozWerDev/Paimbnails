# Skill Progression Map — Paimbnails

Based on analysis of the only merged PR (#1: video decoder race-condition fix), three open issues (#2–#4), changelog themes across v1.0.1–v1.0.8, and the current codebase architecture.

---

## 1. Cross-platform video thread safety & lifecycle hardening

**Evidence:**  
- PR #1 was entirely about data races between decode threads and main-thread teardown in `DecoderMF` (Windows), `DecoderNDK` (Android), and `DecoderPLM` (fallback). The PR was *closed without merging*, meaning those generation-counter and `m_threadRunning` fixes never landed in `main`.  
- Issue #4 ("Game crashed when i tried to submit a level thumbnail") is likely the same Android crash — the `startDecoding()`/`closeInternal()` race that PR #1 described.  
- `VideoPlayer.cpp` (1648 lines) and `VideoThumbnailSprite.cpp` (1277 lines) still use the pre-fix pattern: `stopDecoding()` returns `void`, no generation counter, no `m_threadRunning` atomic.

**Actionable next skill:**  
- **Re-land the generation-counter + `stopDecoding()→bool` pattern** from PR #1 across all three decoders (`DecoderNDK.cpp`, `DecoderMF.cpp`, `DecoderPLM.hpp`).  
- Add `m_threadRunning` atomic busy-wait in `closeInternal()` so codec/D3D resources aren't freed while a detached thread is still running.  
- Move `player->pause()` and eviction `stop()` outside `s_playerCacheMutex` in `VideoThumbnailSprite` (the ANR fix from PR #1 commit `48765c2`).  
- Reduce NDK dequeue timeouts from 10 ms → 1 ms for faster thread exit.  
- Add a regression test hook: a debug setting that rapidly cycles `play()→stop()→play()` on a video player 100 times; any crash = test failure.

---

## 2. Mod compatibility — transition & layer-hook conflicts

**Evidence:**  
- Issue #2 ("Breaks Megahack's transition customizer") — closed, but `TransitionHook.cpp` still intercepts *all* `CCTransitionScene` instances, including those injected by other mods. The `isVanillaTransition()` check uses `typeid` name matching, which is fragile across compilers and won't catch transition subclasses from other Geode mods.  
- Issue #3 ("Breaks my levels tab") with a screenshot showing a broken My Levels layout — likely a hook priority conflict with another mod that also modifies `LevelCell` or `LevelBrowserLayer`.  
- `HookConventions.hpp` already defines `afterNodeIdsOrLate` and `afterAllPaimonUiOrVeryLate`, but `LevelCell.cpp` imports the helper yet many hooks still use raw `Priority::Last` or no priority at all.  
- `ModCompatWarnings.cpp` logs warnings for 7 known-conflicting mods, but these are informational only — no runtime adaptation.

**Actionable next skill:**  
- **Harden `TransitionHook::shouldIntercept()`** to skip scenes where another mod's hook has already replaced the transition (detect by checking if the transition's typeid contains any string from a mod-registered whitelist, or check for a Geode metadata flag on the scene node).  
- **Audit every hook file** (60+ in `src/hooks/`) for missing `afterNodeIdsOrLate()` calls. Add the priority convention where absent, starting with the highest-risk ones: `LevelCell.cpp`, `LevelSearchLayer.cpp`, `LevelInfoLayer.cpp`, `PauseLayer.cpp`.  
- For `ModCompat`, move from log-only warnings to **runtime adaptation**: when Megahack's transition customizer is loaded, automatically disable Paimbnails' transition hook entirely (add a `ModCompat::isMegahackTransitionLoaded()` check and early-return in `shouldIntercept()`).  
- Add a structured "conflict report" in `StartupIncompatibilityCheck` that shows a popup (not just a log) when a known-incompatible mod is detected, with a one-click "disable conflicting feature" button.

---

## 3. Thumbnail submission crash path

**Evidence:**  
- Issue #4 ("Game crashed when i tried to submit a level thumbnail") — open, no body, but the title points to the submission flow.  
- `ThumbnailSubmissionService` does HTTP uploads with `geode::CopyableFunction` callbacks but has no null-check on the callback or on the PNG data size.  
- The capture pipeline (`CaptureOverlay` → `FramebufferCapture` → `SceneCapture`) writes CCImage data into a `std::vector<uint8_t>` on the main thread, then passes it to the submission service — if the capture fails mid-way (e.g., GL context loss during a scene transition), the vector could be empty or corrupt, and the upload path has no validation.

**Actionable next skill:**  
- **Add defensive validation** in `ThumbnailSubmissionService::uploadSuggestion()` and `uploadUpdate()`: reject empty/null PNG data, enforce a maximum size (e.g., 5 MB), and wrap the HTTP callback invocation in a try/catch that logs the error instead of crashing.  
- In `CaptureOverlay`, validate that the captured `CCImage` actually has non-zero dimensions before encoding to PNG.  
- Add a "safe submit" wrapper: capture → validate → confirm dialog → upload, with a progress indicator and error toast on failure.  
- Add crash breadcrumbs: log the level ID, PNG size, and submission step (encoding / uploading / callback) before each step so crash logs pinpoint the failure.

---

## 4. EventBus → geode::Event migration completion

**Evidence:**  
- PR #1 commit `019a177` ("migrate EventBus to geode::Event, `$on_mod(Loaded)`, store setting listeners") was part of the PR but the PR was closed without merging.  
- `src/framework/EventBus.hpp` still contains the custom `paimon::EventBus` implementation with manual `subscribe`/`publish`/`unsubscribe` and a `beginShutdown()` method that was added to fix a crash (destroying subscriber lambdas with dangling `WeakRef<CCNode>` during atexit).  
- `src/framework/ModEvents.hpp` likely defines Geode-native events — the two systems probably coexist, creating confusion about which to use.

**Actionable next skill:**  
- **Audit all `EventBus::subscribe` call sites** (grep for `EventBus::get().subscribe` and `EventBus::get().publish` across the codebase) and migrate each to `geode::Event` + `$on_mod(Loaded)` listeners.  
- The `beginShutdown()` crash-fix pattern is automatically handled by Geode's event system (listeners are cleaned up on mod unload).  
- After migration, delete `EventBus.hpp` and replace any remaining references.  
- For events that need cross-mod pub/sub, use `geode::EventProvider` instead of the custom bus.

---

## 5. Test infrastructure & regression prevention

**Evidence:**  
- There are **zero test files** in the entire repository (no `test/`, `spec/`, or `__test__` directories).  
- PR #1's race-condition fix was never merged, partly because there was no way to validate it without manual testing on two platforms.  
- The video decoder subsystem (`VideoRingBuffer`, `IVideoDecoder`, platform decoders) is the most crash-prone area and has no unit tests for its lock-free ring buffer, generation counters, or thread lifecycle.

**Actionable next skill:**  
- **Add a `tests/` directory** with a CMake option (`PAIMBNAILS_ENABLE_TESTS`) that compiles test TUs against the library headers only (no Geode runtime needed for ring-buffer and decoder-logic tests).  
- Start with:  
  1. `VideoRingBuffer` unit test — single-producer/single-consumer correctness, overflow, underflow, adaptive slot count.  
  2. `LightLemmatizer` unit test — stemming, stopword removal, synonym expansion (pure string logic, no dependencies).  
  3. `PopupRegistry` scoring test — verify that weighted matching + fuzzy similarity produces the expected popup for given queries.  
  4. `MenuPhysicsWorld` unit test — step a physics world with known bodies and assert final positions (no rendering needed).  
- Integrate with CI: add a GitHub Actions workflow that builds the test binary and runs it on push/PR.

---

## 6. My Levels tab stability

**Evidence:**  
- Issue #3 ("Breaks my levels tab") — open, with a screenshot showing a visually broken layout.  
- `MyLevelsLayoutHook.cpp` is currently **disabled** (entire file is a comment saying "no layout changes applied here").  
- Despite the hook being disabled, the issue reporter's screenshot shows broken cells — meaning the breakage comes from another hook (likely `LevelCell.cpp` or `LevelSearchLayer.cpp`) when the "My Levels" tab is active.  
- `LevelCell.cpp` is 600+ lines with inline feature detection (compact list, GIF, video, badges, etc.) and no guard for the "created levels" context where the cell structure differs.

**Actionable next skill:**  
- **Add a context guard in `LevelCell.cpp`** that detects when the parent layer is "My Levels" (check `LevelBrowserLayer` type or the layer's node ID) and skips Paimbnails' thumbnail/video/badge injection entirely for created-level cells, or uses a simplified rendering path.  
- Reproduce the issue: open the "My Levels" tab with Paimbnails enabled and compare cell structure with/without the mod. Identify which injection (thumbnail sprite, video player, badge, background gradient) corrupts the layout.  
- Add a `g_suppressLevelCellEnhancements` guard (already declared but only used in one spot) at the entry of every LevelCell hook method.

---

## 7. Shutdown robustness — eliminate atexit crashes

**Evidence:**  
- Changelog v1.0.5 documents `safeShutdownStep` wrapping for `$on_game(Exiting)`, plus the `EventBus::beginShutdown()` fix for dangling `WeakRef<CCNode>` in subscriber lambdas.  
- The EventBus shutdown comment explicitly says: "Their lambdas capture WeakRef<CCNode>; destroying them during atexit (EventBus dtor) hits an invalid WeakRefPool → crash." This means there are still lambda captures that reference Cocos2d objects which may be destroyed before the static EventBus destructor runs.  
- `RuntimeLifecycle.hpp` provides `isRuntimeShuttingDown()` but it's unclear how many async callbacks check it.

**Actionable next skill:**  
- **Audit every `EventBus::subscribe` callback** and every `std::function` capture in async code (`HttpClient`, `ThumbnailTransportClient`, `PBOUploader`) for `WeakRef` or raw `CCNode*` captures. Replace raw captures with `WeakRef` + null-check, or use `std::shared_ptr` to state objects that outlive the Cocos scene.  
- In every async callback, add an early return guard: `if (isRuntimeShuttingDown()) return;`  
- Move the EventBus cleanup from "static destructor" to an explicit `$on_game(Exiting)` call at the *beginning* of shutdown (before Cocos starts destroying nodes). The current `beginShutdown()` call needs to be verified as running early enough.

---

## Summary Priority Matrix

| # | Skill | Root Evidence | Impact | Effort |
|---|-------|---------------|--------|--------|
| 1 | Video thread-safety re-land | PR #1 (unmerged), Issue #4 (crash) | 🔴 Critical — active crash | Medium (pattern already designed) |
| 2 | Mod compat / hook priority | Issues #2, #3 (broken UI) | 🟠 High — user-visible breakage | Medium-Low |
| 3 | Thumbnail submit crash | Issue #4 | 🔴 Critical — crash on submit | Low (validation + try/catch) |
| 4 | EventBus → geode::Event | PR #1 unmerged commit, shutdown crashes | 🟡 Medium — latent crash risk | Medium |
| 5 | Test infrastructure | Zero tests in repo | 🟡 Medium — prevents regressions | Medium (initial setup) |
| 6 | My Levels tab fix | Issue #3, disabled hook | 🟠 High — broken tab | Low-Medium |
| 7 | Shutdown robustness | Changelog v1.0.5, EventBus crash | 🟡 Medium — intermittent crash | Medium |

**Recommended execution order:** 1 → 3 → 2 → 6 → 7 → 4 → 5
