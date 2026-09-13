# Skill Progression Map — Paimbnails (v4)

Fourth iteration. Grounded in PR history, open issues, changelog, full codebase review (901 source files, 196 `$modify` hooks, 17 `$on_game` lifecycle hooks), and **direct verification of every v3 claim against the current `main` branch** (commit `b2f8813`, July 12).

> **What changed since v3 (PR #7, July 24):**
> - **Nothing.** No new commits, no new PRs, no new issues, no reviews, no releases since July 12.
> - v1.1.0 (Collab Editor public launch, promised July 20) is now **11 days overdue**. Latest release remains v1.0.9 (July 6).
> - Issues #3 (My Levels tab) and #4 (thumbnail crash) have been open since April 28 — **3 months, no fix**.
> - **Three previous skill-map PRs (#5, #6, #7) are all open, unreviewed, and unmerged.** This is the 4th analysis iteration with zero code action.
> - **This map corrects two imprecise v3 claims** (voice FIFO, hook count) and adds new evidence (dangling callback in ThumbnailSubmissionService, missing shutdown step, SelfTest.cpp as test seed).

---

## Evidence Sources

| Source | What it tells us |
|--------|-----------------|
| **PR #1** (closed/unmerged) | Copilot-generated destructive revert. Removed rapidfuzz, MSVC ICE workarounds, installer workflows, reverted to v1.0.0. The race-condition fixes in the PR body were never in the diff. Video thread-safety remains unfixed. |
| **PR #5** (open, unreviewed) | v1 skill map. Incorrectly treated PR #1 as a legitimate fix. |
| **PR #6** (open, unreviewed) | v2 skill map. Corrected PR #1 analysis. Did not cover July 11 feature sync or CI disablement. |
| **PR #7** (open, unreviewed) | v3 skill map. Covered July 11 sync. Two imprecise claims corrected below. |
| **Issue #2** (closed) | Megahack transition customizer conflict. `TransitionHook` uses fragile `typeid` matching. Closed, but structural pattern remains. |
| **Issue #3** (open, 3 months) | Breaks My Levels tab. `MyLevelsLayoutHook.cpp` is an inert 1-line stub. Breakage from `LevelCell.cpp` hooks injecting into created-level cells. |
| **Issue #4** (open, 3 months) | Game crash on thumbnail submission. `ThumbnailSubmissionService` captures `this` + `callback` in async HTTP lambdas with no shutdown guard, no PNG validation, no try/catch. |
| **Changelog v1.0.1–v1.0.9** | Feature expansion: thumbnails → backgrounds → shaders → agent mode → collab editor. v1.0.9: Collab Editor (closed beta), Editor History, Paimon guide v1.5. Promises v1.1.0 public launch July 20. |
| **Commit `8ca6d17`** (July 11) | Synced editor-suite (87 files), editor-history (8 files), collab server (15 files), texture-studio (82 files). CI disabled to manual-only. |
| **Commit `b2f8813`** (July 12) | Removed private server folders from public tree. Last commit to date. |
| **Codebase stats** | 901 source files. **196 `$modify` hooks** (v3 said "60+" — corrected). 17 `$on_game` lifecycle hooks. 63 hook files. Zero test-framework files. One runtime `SelfTest.cpp`. CI is `workflow_dispatch` only. |
| **No PR reviews** | Zero reviews on all 4 PRs. No human feedback to incorporate. |

---

## Corrections to v3

| v3 claim | v4 correction | Evidence |
|----------|---------------|----------|
| "CollabVoice.cpp:358 — unbounded voice decode" | **Imprecise.** The playback FIFO IS bounded: `kFifoMaxSamples = kSampleRate * 2` (2s cap, `CollabVoice.cpp:21`), with catchup trim to 500 ms (`:370-372`). The real issues are: (a) **no size cap on incoming `b64` string before `b64Decode()`** (`:358`) — a huge frame causes a large transient allocation + CPU spike before the FIFO cap kicks in; (b) **FMOD mixer thread blocks on `speaker->mutex`** during large decode (`voicePcmRead:167` vs `onRemoteFrame:363`) — audio stutter; (c) **no per-peer inbound frame-rate cap** — a peer can flood frames. | `CollabVoice.cpp:21-22, 167, 358, 363-372` |
| "60+ Geode `$modify` hooks" | **Undercounted.** Actual count is **196 `$modify` macros** across the codebase. This increases the mod-compat attack surface — every hook is a potential conflict point with other mods. | `grep -rn '$modify' src/ --include="*.cpp" \| wc -l` → 196 |

---

## 1. Re-enable CI with an automated build gate

**Evidence:**
- `.github/workflows/build.yml:3-5` — CI is `workflow_dispatch` only. No `push:` or `pull_request:` triggers. Every push and PR runs zero CI.
- The July 11 sync (editor-suite, editor-history, collab server, texture-studio — ~190 new files) landed on `main` with no automated build verification.
- Build actions pinned to `@main` (`build.yml:37`) and `combine@main` (`build.yml:167`) — non-reproducible; a bad upstream commit silently breaks builds.
- `CMakeLists.txt:34` — `pl_mpeg` pinned to `master` branch, not a tag.
- **This is the cheapest fix in the entire map**: re-adding `push:`/`pull_request:` triggers is a 3-line change.

**Actionable steps:**
1. Re-add `push:` (branches: `main`) and `pull_request:` triggers to `build.yml`. Keep `workflow_dispatch` as manual override.
2. Pin `geode-sdk/build-geode-mod` to a specific commit SHA instead of `@main`.
3. Pin `pl_mpeg` to a specific git tag in `CMakeLists.txt:34`.
4. Add a `concurrency` group keyed on `github.ref` so manual runs don't conflict.
5. Once tests exist (skill #6), add a test job gating PR merges.

**Files to modify:** `.github/workflows/build.yml`, `CMakeLists.txt`

**Estimated effort:** ~30 min (3-line trigger change + dep pinning)

**Why first:** Without CI, none of the fixes in skills #2–#7 can be verified mechanically. This is the foundation.

---

## 2. Video decode thread-safety hardening

**Evidence:**
- `src/video/VideoDecoder.hpp:84` — `virtual void stopDecoding() = 0;` **returns `void`. CONFIRMED UNFIXED.** Callers cannot detect whether the decode thread was joined or detached.
- No generation counters exist. Each decoder uses a sticky `m_decodeThreadDetached` atomic — a containment workaround, not a fix.
- `src/utils/VideoThumbnailSprite.cpp` — `player->pause()` called while holding `s_playerCacheMutex`; `stop()` calls also hold the mutex and can block on `timedJoin` for 1–3 seconds, blocking the Cocos2d main thread during scrolling.
- `VideoThumbnailSprite::s_asyncShutdown` is set late — in `clearCache()` (step 11/14 of shutdown, `RuntimeLifecycle.cpp:216`), not at the start of `$on_game(Exiting)`.
- `RuntimeLifecycle.cpp:109-111` — `EventBus::get().beginShutdown()` runs first, then `markRuntimeShuttingDown()`. The global flag is set early, but the video-specific `s_asyncShutdown` is late.
- Issue #4's crash may be the Android variant of this race (capture → encode → video player torn down mid-operation).

**Actionable steps:**
1. Change `IVideoDecoder::stopDecoding()` to return `bool` (`true` = joined, `false` = detached). Propagate through all decoders.
2. Add `m_generation: atomic<uint64_t>` to each decoder. `startDecoding()` increments it; the decode loop exits at the next iteration boundary if generation mismatches.
3. Add `m_threadRunning: atomic<bool>`. In `closeInternal()`, busy-wait on it (200–500 ms cap) before releasing codec resources.
4. In `VideoPlayer::stop()`, only call `seekTo(0)` if `stopDecoding()` returned `true`.
5. Move `player->pause()` and eviction `stop()` outside `s_playerCacheMutex` — collect items under the lock, stop after releasing.
6. Set `VideoThumbnailSprite::s_asyncShutdown = true` at the top of `$on_game(Exiting)`, before `EventBus::beginShutdown()`.

**Files to modify:** `src/video/VideoDecoder.hpp`, `src/video/platform/DecoderMF.{cpp,hpp}`, `src/video/platform/DecoderNDK.{cpp,hpp}`, `src/video/platform/DecoderPLM.hpp`, `src/video/platform/DecoderAVF.{cpp,hpp}`, `src/video/VideoPlayer.cpp`, `src/utils/VideoThumbnailSprite.cpp`, `src/core/RuntimeLifecycle.cpp`

**Estimated effort:** Medium (multi-platform decoder changes)

---

## 3. Thumbnail submission crash-path hardening

**Evidence:**
- Issue #4 (open, 3 months): "Game crashed when i tried to submit a level thumbnail."
- `src/features/thumbnails/services/ThumbnailSubmissionService.cpp:31` — HTTP callback captures `[this, callback, levelId, username]`. While `this` is a singleton (safe from dangling), the `callback` is a `std::function` that likely captures UI elements (buttons, labels, popups). If the UI is destroyed before the upload completes, invoking `callback` is a use-after-free.
- `:59` — Same pattern in `uploadUpdate()`.
- **No `isRuntimeShuttingDown()` guard** in any callback. `RuntimeLifecycle.cpp` has 14 shutdown steps — `ThumbnailSubmissionService` is not among them. An upload completing during teardown fires the callback into a dying UI.
- **No PNG validation**: `pngData` passed straight to `HttpClient` with no magic-byte check, no dimension check, no size cap (`:30, :58`).
- **No try/catch**: neither upload methods nor HTTP callbacks are wrapped (`:31-36, :59-67`). An exception from `ThumbnailLoader::invalidateLevel` (`:62`) propagates out of the web callback and crashes the web thread.
- Download callbacks (`:73-77, :85-89, :95-99, :106-109`) call `ThumbnailTransportClient::bytesToTexture(data)` without shutdown guards — touching a dead texture cache during teardown.

**Actionable steps:**
1. Add `isRuntimeShuttingDown()` guard at the top of every HTTP callback lambda — early-return if shutting down.
2. Add PNG validation: reject empty data, enforce min 1 KB / max 5 MB, check PNG magic bytes (`\x89PNG\r\n\x1a\n`) before upload.
3. Wrap upload method bodies in `try/catch` with `log::error` + `callback(false, reason)`.
4. Add crash breadcrumbs: log level ID, PNG size, and current step before each operation.
5. In download callbacks, check `isRuntimeShuttingDown()` before `bytesToTexture()`.
6. Add a `ThumbnailSubmissionService::shutdown()` method and call it from `RuntimeLifecycle.cpp` (new step in the 14-step sequence).

**Files to modify:** `src/features/thumbnails/services/ThumbnailSubmissionService.{cpp,hpp}`, `src/core/RuntimeLifecycle.cpp`

**Estimated effort:** Low (~30 lines of guards + validation)

---

## 4. Collab Editor stability — network, pointer, and voice hardening

**Evidence:**
- v1.0.9 changelog: "Collab Editor added in closed beta. Will be available for everyone on July 20, 2026 with v1.1.0." **v1.1.0 is 11 days overdue.** Hardening is urgent before public launch.
- `src/features/collab-editor/CollabManager.hpp:220` — `LevelEditorLayer* m_editor = nullptr;` **raw pointer**, not `WeakRef`. If the user exits the editor without `clearEditor()`, `m_editor` dangles. `tick()` guards with `if (m_editor)` but a stale non-null pointer is a use-after-free.
- `:221` — `CollabEditorOverlay* m_overlay` is also a raw pointer.
- **Reconnection IS implemented** (`tryRecoverSession()`, `CollabManager.cpp`) and the network generation counter (`m_gen`, `CollabNetClient.hpp:89`) is correctly used in all HTTP callbacks. This is good.
- **No heartbeat**: `CollabNetClient` uses HTTP long-poll (`CollabNetClient.hpp:12-18`). The only "pong" handler (`CollabManager.cpp:787`) says "nothing to do" — it's a no-op. A silently-dropped connection takes up to the long-poll timeout to detect.
- **Weak packet validation**: `handleMessage` uses `unwrapOr` defaults but performs no strict schema validation — no array-size caps on `snapshot`/`op_batch`, no bounds on `save` string length per op.
- `CollabVoice.cpp:358` — `b64Decode` on inbound voice data with **no length cap before decode**. A malicious peer could send an arbitrarily large `data` string causing a large transient allocation + CPU spike. The FIFO IS capped after decode (`kFifoMaxSamples`, `:21, :370`) — this corrects v3's "unbounded" claim — but the decode itself is uncapped.
- `CollabVoice.cpp:167` — FMOD mixer callback `voicePcmRead` holds `speaker->mutex`. `onRemoteFrame` (`:363`) holds the same mutex during decode. A large frame blocks the audio mixer thread → stutter.
- No per-peer inbound frame-rate cap — a peer can flood voice frames.

**Actionable steps:**
1. Replace raw `LevelEditorLayer* m_editor` with `WeakRef<LevelEditorLayer>`. Null-check before every use in `tick()`, `handleMessage()`, and all editor hooks. Same for `m_overlay`.
2. Add explicit heartbeat: send a lightweight poll-with-empty-batch every 10 s if no data exchanged; if no response within 15 s, show "reconnecting" overlay.
3. Add packet validation: reject messages > 1 MB payload, unknown message types, or arrays exceeding 10,000 entries.
4. Add pre-decode length cap in `CollabVoice::onRemoteFrame()`: reject inbound `data` strings > 64 KB before `b64Decode`. Add per-peer inbound frame-rate cap (max 30 frames/sec).
5. Rate-limit outgoing voice packets (max 20 packets/sec, max 320 bytes each).
6. Before v1.1.0 public launch, add an integration test: connect → apply remote edit → disconnect → reconnect → verify state consistency.

**Files to modify:** `src/features/collab-editor/CollabManager.{cpp,hpp}`, `src/features/collab-editor/CollabNetClient.{cpp,hpp}`, `src/features/collab-editor/CollabVoice.{cpp,hpp}`, `src/features/collab-editor/hooks/CollabEditorHooks.cpp`

**Estimated effort:** Medium (WeakRef migration + heartbeat + validation)

---

## 5. Editor-history raw-pointer UAF — same pattern as Collab, new code

**Evidence:**
- `src/features/editor-history/services/EditorHistoryTracker.hpp:96` — `LevelEditorLayer* m_editor = nullptr;` **raw pointer**. Same UAF pattern as `CollabManager::m_editor`. `setEditor`/`clearEditor` exist but there's no RAII tie to the editor lifecycle.
- `:97` — `std::unordered_map<UndoObject*, UndoMeta> m_meta;` **raw `UndoObject*` map key**. If an undo entry is destroyed without `prune()` running first, the map contains a dangling key — `metaFor()` accesses freed memory.
- `src/features/editor-history/services/ObjectTimelineStore.hpp` — `LevelEditorLayer* m_editor = nullptr;` **raw pointer**, same pattern.
- Both singletons are main-thread-only by convention; no mutex. The raw pointers are the problem, not threading.

**Actionable steps:**
1. Replace raw `LevelEditorLayer* m_editor` with `WeakRef<LevelEditorLayer>` in both `EditorHistoryTracker` and `ObjectTimelineStore`. Null-check before every use.
2. Replace raw `UndoObject*` keys in `m_meta` with a stable identifier (e.g. `uint64_t` undo ID). Alternatively, call `prune()` at the top of every method accessing `m_meta`.
3. Add destructors that call `clearEditor()` as a safety net.
4. In `EditorHistoryHooks.cpp`, ensure `clearEditor()` is called from editor teardown events, not just manual button presses.

**Files to modify:** `src/features/editor-history/services/EditorHistoryTracker.{cpp,hpp}`, `src/features/editor-history/services/ObjectTimelineStore.{cpp,hpp}`, `src/features/editor-history/hooks/EditorHistoryHooks.cpp`

**Estimated effort:** Low-Medium (WeakRef migration + key change)

---

## 6. Test infrastructure — break the zero-test cycle

**Evidence:**
- **Zero test-framework files** in the repository. No `test/`, `spec/`, or `__test__` directories. No `enable_testing()`, `add_test()`, or test framework in `CMakeLists.txt`.
- **One runtime self-test exists**: `src/features/texture-studio/engine/SelfTest.cpp` — `engineSelfTest()` runs in-process, logs PASS/FAIL via `log::error`, and covers a complex pipeline (color clustering → cluster classification → mask building → luminance tinting → PSNR comparison against ground truth). It's called from the editor at runtime, not from CI. **This is a better seed than v3 acknowledged** — it already has assertion-style checks and covers a non-trivial algorithm.
- PR #1's "race-condition fix" was never validated because there's no way to test it without manual testing on Android + Windows.
- The video subsystem (`VideoRingBuffer`, lock-free SPSC, adaptive slot count) is pure logic — perfect for unit testing without a Geode runtime.
- `LightLemmatizer` (stemming, stopwords, synonyms) is pure string logic.
- `PopupRegistry` scoring is pure math.
- `CollabManager::handleMessage` deserialization is testable with crafted JSON inputs.

**Actionable steps:**
1. Add `tests/` directory with CMake option `PAIMBNAILS_ENABLE_TESTS=OFF` (default).
2. **Start by formalizing `SelfTest.cpp`** — extract its assertions into a Catch2/doctest test case that can run in CI without the Geode runtime. This gives immediate coverage of the texture-studio pipeline.
3. Add 5 test TUs that compile against library headers only (no Geode runtime):
   - `test_VideoRingBuffer.cpp` — SPSC correctness, overflow, underflow, adaptive slot count.
   - `test_LightLemmatizer.cpp` — stemming, stopword removal, synonym expansion, bilingual EN/ES.
   - `test_PopupRegistry.cpp` — weighted matching, compound keyword scoring, fuzzy fallback.
   - `test_FusionStore.cpp` — serialization round-trip, magic/version validation, corrupt-data rejection.
   - `test_CollabMessage.cpp` — `handleMessage` with valid/missing/oversized fields.
4. Use Catch2 or doctest (header-only, available via CPM).
5. Add a GitHub Actions workflow step that builds and runs tests on PR/push (ties into skill #1).

**Files to create:** `tests/CMakeLists.txt`, `tests/test_VideoRingBuffer.cpp`, `tests/test_LightLemmatizer.cpp`, `tests/test_PopupRegistry.cpp`, `tests/test_FusionStore.cpp`, `tests/test_CollabMessage.cpp`, `tests/test_TextureStudioSelfTest.cpp`

**Estimated effort:** Medium (framework setup + 6 test TUs)

---

## 7. Mod-compatibility runtime adaptation

**Evidence:**
- **196 `$modify` hooks** across the codebase (corrected from v3's "60+"). Every hook is a potential conflict point with other Geode mods.
- `src/framework/compat/ModCompat.hpp` detects 12+ mods but `src/core/ModCompatWarnings.cpp` only logs warnings — no runtime adaptation.
- `src/features/transitions/hooks/TransitionHook.cpp` uses `isVanillaTransition()` which checks `typeid(*trans).name()` — fragile across compilers and blind to mod transition subclasses.
- Issue #2 (Megahack) is closed. Issue #3 (My Levels) shows `MyLevelsLayoutHook.cpp` is intentionally disabled (1-line stub). `g_suppressLevelCellEnhancements` is checked at 2 sites in `LevelCell.cpp`.
- **Lower priority**: the most severe conflict (Megahack) is resolved, and the My Levels hook is inert.

**Actionable steps:**
1. Add `ModCompat::isTransitionCustomizerLoaded()` check. In `TransitionHook::shouldIntercept()`, return `false` early when detected.
2. Replace `isVanillaTransition()` with a `std::unordered_set<std::string>` of known mod transition class prefixes + a node user-data flag.
3. In `StartupIncompatibilityCheck`, add a popup (not just a log) when a known-incompatible mod is detected, with a "disable conflicting Paimbnails feature" button.
4. For Issue #3: add context detection in `LevelCell::loadCustomLevelCell()` — check if the parent `LevelBrowserLayer` is the "My Levels" tab and set `g_suppressLevelCellEnhancements = true`.

**Files to modify:** `src/features/transitions/hooks/TransitionHook.cpp`, `src/framework/compat/ModCompat.hpp`, `src/core/ModCompatWarnings.cpp`, `src/core/StartupIncompatibilityCheck.cpp`, `src/hooks/LevelCell.cpp`

**Estimated effort:** Medium

---

## 8. Shutdown robustness — close remaining gaps

**Evidence:**
- `RuntimeLifecycle.cpp:105-283` — 14-step shutdown sequence, well-structured with `safeShutdownStep` exception guards. `EventBus::get().beginShutdown()` runs first (`:109`), then `markRuntimeShuttingDown()` (`:111`). Good ordering.
- `isRuntimeShuttingDown()` (`:69-71`) checks both `s_runtimeShuttingDown` and `ThreadTracker::isShuttingDown()`.
- **ThumbnailSubmissionService is NOT in the shutdown sequence** — no explicit shutdown step. In-flight HTTP callbacks fire during teardown without a guard. (Addressed in skill #3.)
- `CustomSongWidget.cpp:457` — 1 raw-pointer capture site guarded by a custom liveness registry (`paimon::csw::Lifecycle::isAlive(widget)`), riskier than `WeakRef`.
- `VideoThumbnailSprite::s_asyncShutdown` set late (step 11/14, `:216`). (Addressed in skill #2, step 6.)

**Actionable steps:**
1. Replace raw-pointer capture in `CustomSongWidget.cpp:457` with `WeakRef<CustomSongWidget>`.
2. Add `ThumbnailSubmissionService::shutdown()` to the shutdown sequence (new step). (Overlaps with skill #3.)
3. Audit `HttpClient` response handlers, `ThreadPool` tasks, and `PBOUploader` completion handlers for `isRuntimeShuttingDown()` guards.

**Files to modify:** `src/hooks/CustomSongWidget.cpp`, `src/utils/HttpClient.cpp`, `src/video/PBOUploader.cpp`, `src/core/RuntimeLifecycle.cpp`

**Estimated effort:** Low

---

## Execution Strategy — From Analysis to Action

> **Meta-finding:** This is the 4th skill-map iteration. PRs #5, #6, #7 are all open and unreviewed. Zero code fixes have landed. The project has been stalled since July 12 (19 days). v1.1.0 is 11 days overdue. Issues #3/#4 have been open for 3 months. **Producing more analysis without code changes has diminishing returns.** The recommended path forward is to implement the cheapest, highest-impact fixes as standalone PRs that can be merged independently.

### Quick-win PRs (can be merged independently, low risk)

| PR | Skill | Lines changed | Risk | Impact |
|----|-------|--------------|------|--------|
| **PR-A** | #1 (CI) | ~5 | Minimal — re-add triggers, pin deps | Unblocks all verification |
| **PR-B** | #3 (thumbnail) | ~30 | Low — additive guards + validation | Fixes Issue #4 crash |
| **PR-C** | #4 (voice cap) | ~5 | Low — size check before decode | Prevents DoS in collab |
| **PR-D** | #8 (shutdown) | ~10 | Low — WeakRef + shutdown step | Closes teardown gaps |

### Medium-effort PRs (require testing on target platforms)

| PR | Skill | Effort | Risk | Impact |
|----|-------|--------|------|--------|
| **PR-E** | #2 (video) | Medium | Medium — multi-platform decoder | Fixes Android crash + Windows freeze |
| **PR-F** | #5 (editor-history) | Low-Medium | Low — WeakRef migration | Prevents UAF in new code |
| **PR-G** | #4 (collab core) | Medium | Medium — WeakRef + heartbeat | Pre-launch hardening |

### Recommended execution order

1. **PR-A** (CI) → enables verification of all subsequent PRs
2. **PR-B** (thumbnail crash) → fixes a 3-month-old user-reported crash
3. **PR-C** (voice cap) → 5-line safety check, trivial
4. **PR-D** (shutdown) → closes teardown gaps
5. **PR-E** (video thread-safety) → fixes active crash on Android + Windows
6. **PR-F** (editor-history UAF) → prevents UAF in recently shipped code
7. **PR-G** (collab core) → pre-launch hardening for overdue v1.1.0
8. **Skill #6** (test infra) → formalize SelfTest.cpp + add unit tests
9. **Skill #7** (mod-compat) → lower priority, worst conflict resolved

---

## Summary Priority Matrix

| # | Skill | Root Evidence | Impact | Effort | Order |
|---|-------|---------------|--------|--------|-------|
| 1 | Re-enable CI + build gate | `build.yml:5` manual-only; 196 hooks with no compile check | Critical | Low | 1st |
| 2 | Video thread-safety | `VideoDecoder.hpp:84` void stop; pause-under-mutex; no gen counters | Critical | Medium | 5th |
| 3 | Thumbnail submit crash | Issue #4 (3 months); `ThumbnailSubmissionService.cpp:31,59` dangling callback; no shutdown guard | Critical | Low | 2nd |
| 4 | Collab Editor stability | v1.1.0 11 days overdue; raw `m_editor` (`:220`); no heartbeat; uncapped voice decode (`:358`) | High | Medium | 7th |
| 5 | Editor-history UAF | `EditorHistoryTracker.hpp:96-97` raw ptr + raw map keys | High | Low-Medium | 6th |
| 6 | Test infrastructure | Zero test framework; one runtime `SelfTest.cpp` (better seed than v3 said) | Medium | Medium | 8th |
| 7 | Mod-compat runtime adaptation | Issues #2 (closed), #3; 196 hooks; `ModCompatWarnings` log-only | Medium | Medium | 9th |
| 8 | Shutdown robustness | `CustomSongWidget.cpp:457` raw-ptr; `ThumbnailSubmissionService` absent from 14-step shutdown | Low-Medium | Low | 4th |

**Recommended execution order (by PR):** PR-A → PR-B → PR-C → PR-D → PR-E → PR-F → PR-G → Skill #6 → Skill #7

---

## Delta from v3 (PR #7)

| v3 skill | v4 status | Reason |
|----------|-----------|--------|
| #1 CI re-enablement | **Unchanged — still #1** | Confirmed; 5-line fix |
| #2 Video thread-safety | **Unchanged — still #2** | Confirmed unfixed |
| #3 Thumbnail submit crash | **Refined — still #3** | Root cause narrowed: dangling `callback` in async lambda + absent from shutdown sequence (not just "no try/catch") |
| #4 Collab Editor stability | **Corrected — still #4** | Voice FIFO IS bounded (`kFifoMaxSamples`); real issue is uncapped pre-decode + mixer-thread mutex block + no frame-rate cap |
| #5 Editor-history UAF | **Unchanged — still #5** | Confirmed raw pointers |
| #6 Test infrastructure | **Refined — still #6** | `SelfTest.cpp` acknowledged as better seed than v3 implied; formalize it as first CI test |
| #7 Mod-compat | **Unchanged — still #7** | Hook count corrected: 196, not "60+" |
| #8 Shutdown robustness | **Expanded — still #8** | New finding: `ThumbnailSubmissionService` absent from 14-step shutdown sequence |
| — **NEW: Execution strategy** | **Added** | 4 iterations with zero action; recommends standalone fix PRs instead of more analysis |
