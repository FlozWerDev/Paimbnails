# v1.0.5

Release notes for Paimbnails 2 v1.0.5.

**Mod Previews**
- New **Mod Previews** feature: when you open a Geode mod, if its repository has a `previews/` folder with `preview-1.png` … `preview-10.png`, Paimbnails shows a thumbnail strip in the Details tab. Click a thumbnail to open a full-screen gallery with prev/next navigation. Toggle it from the **Mod Previews** setting.
- Based on the original idea and design by **Alphalaneous** ([Mod-Previews](https://github.com/Alphalaneous/Mod-Previews)), reimplemented natively for Geode v5 (no extra dependencies).

**Paimon Agent Mode**
- New **Agent Mode** toggle button under Paimon in the guide chat. Pink (`agent:off`) means Paimon answers questions and shows you the way; blue (`agent:on`) means Paimon executes actions for you.
- **Visual agent execution with real clicks**: in agent mode, when you press *Ask*, the chat closes and an **AgentPilot** Paimon spawns on top of the running scene (z=99999, above any popup). She flies in a soft bezier curve through a chain of targets, **really clicking buttons along the way** — not just opening the final popup. For example, "abre discord" produces a chain of: open Hub → wait for Discord shortcut → fly to it → highlight → click → done.
- **Whitelist by prefix**: the new `ClickInvoker` only fires `CCMenuItem::activate()` on nodes whose ID starts with `flozwer.paimbnails2/`. Anything outside that prefix (other mods, GD vanilla, popups without IDs) is rejected silently. This guarantees the agent never accidentally bans, deletes, or interacts with unrelated UI.
- New **ActionGraph**: a small registry of pre-defined click chains for the most common intents (hub-discord, hub-quickhub, hub-news, hub-forum, hub-home, cursor-settings-tab, cursor-gallery-tab, pet-settings-tab, pet-advanced-tab, pet-gallery-tab). The DSL parser routes prompts like "go to discord" to the matching chain.
- New **WaitForNode** step polls every 0.15 s with a configurable timeout (default 2 s) until a node with the given ID appears in the scene. Used between Open and ClickButton so the agent waits for the popup to render before flying to the next button.
- Added stable IDs to tab buttons that the agent needs (PaimonHubLayer: `home/news/forum-tab-btn`, CursorConfigPopup: `cursor-gallery/settings-tab-btn`, PetConfigPopup: `pet-gallery/settings/advanced-tab-btn`). Tab containers are still `*-tab` (CCNode/CCLayerRGBA) — the new `*-tab-btn` are the actual `CCMenuItemSpriteExtra` instances the agent activates.
- New **AgentPilot** singleton (CCNode in OverlayManager-equivalent z order) survives scene changes via `ensureAttached()` — if a popup `pushScene`s during a flight, the pilot re-attaches to the new scene automatically.
- New **NodeFinder** helper does BFS through the scene to find nodes by ID, by `ButtonSprite` text content, or by `CCLabelBMFont` text. Used to locate the click target for each step.
- New **AgentDSL** parser turns natural-language prompts into a small action plan with five step kinds: `Open(popup)`, `WaitForNode(id, timeout)`, `ClickButton(id)`, `SetSetting(key, value)`, and `Say(message)`. Examples that work today (English/Spanish):
  - `"abre discord"` / `"open discord"` → opens Hub, clicks Discord shortcut.
  - `"andate al quick hub"` / `"go to quick hub"` → opens Hub, clicks Quick Hub shortcut.
  - `"andate al foro"` / `"go to forum"` → opens Hub, clicks Forum tab button.
  - `"abre cursor settings"` / `"open cursor settings"` → opens Custom Cursor, clicks the Settings tab button.
  - `"activa discord"` / `"enable discord"` → toggles `discord-rpc-enabled` directly via `setSettingValue`.
  - `"pon el idioma en espanol"` / `"set language to spanish"` → updates `language`.
- New **SettingsRegistry**: a whitelist of mod settings the agent is allowed to modify (~16 entries: language, discord, audio toggles, thumbnail size, levelinfo background style, etc.). Anything outside the whitelist is refused.
- The Agent Mode state is persisted via `Mod::setSavedValue<bool>("agent-mode-enabled")`. Toggling animates Paimon (Surprise on enable, Wave on disable) and shows a status message in the chat.

**Paimon Guide (Paigorit V1)**
- Matching algorithm **"Paigorit V1"** (Paimon + Algorithm V1) powering the Paimon guide chat. Paimon learns from the **real titles of the popups and layers** in the mod instead of hand-written keyword lists.
- New **PopupRegistry**: every Paimbnails popup/layer is registered with its real `displayName` (the title the user sees in the popup's title bar), aliases, weight, and an `open()` lambda. Examples: the entry for `ProfileBgPickerPopup` is registered as **"Profile Background"** (en) / **"Fondo de Perfil"** (es), so asking *"profile background"* or *"background profile"* (in any order) routes there — not to the generic Backgrounds menu.
- Each registry entry has a `weight` (1–200) so when several popups match the same query, the most specific one wins. Generic concepts like `Scene Background` weigh 70, feature-specific popups like `Custom Cursor` weigh 95, identity-level popups like `Profile Photo Editor` weigh 120, and the fully-specific `Profile Background` weighs 130.
- Compound keyword matching is **bag-of-words**: a multi-word display name like `Profile Background` matches the user's query as long as both `profile` and `background` appear, regardless of order.
- Match scoring is `weight + bonuses`, where bonuses come from compound match (+20), exact token match (+10), and very-high fuzzy similarity (+5). Among qualified popups (any keyword reaches the per-kind threshold), the highest score wins. Functional threshold ≥70, conversational ≥85, conversational on long queries ≥92.
- New **LightLemmatizer** module handling Paimon's "comprehension":
  - English/Spanish stopwords (`how`, `where`, `donde`, `el`, `la`, `me`…) are stripped before scoring.
  - Light suffix-based stemming (`backgrounds` → `background`, `configurando` → `configur`, `running` → `runn`).
  - ~50-entry synonym table mapping jargon to canonical forms (`pic` → `picture`, `bg` → `background`, `cancion` → `music`, `raton` → `cursor`, `vinilo` → `menumusic`).
- The previous hand-curated `GuideIntent` list for popups (~20 manual entries) is replaced by 25+ entries built automatically from the PopupRegistry. Conversational intents (greetings, jokes, etc.) are kept as a small explicit set with low weight (30–40) so they never beat a real popup match. No new external libraries were added; the existing `rapidfuzz` (header-only, MIT) is reused for fuzzy similarity.

**Compatibility**
- Removed incompatibility with `thesillydoggo.blur-api`. Paimbnails now coexists with mods that depend on Blur API (e.g. QOLMod). Our internal blur stays untouched and continues to work.

**Stability**
- Each phase of `$on_game(Exiting)` is now wrapped with try/catch via a `safeShutdownStep` helper. A failure in one shutdown step (audio, video, cache cleanup, etc.) no longer aborts the whole exit sequence — every other step still runs, the user's saved data is persisted, and the failure is logged with the step name for crashlogs.
- `ThumbnailLoader` now releases `CCImage` instances via `release()` instead of `delete`, respecting Cocos2d-x reference counting. Functionally equivalent in this path (refcount was 1) but idiomatically correct and safer if the object is ever retained elsewhere.
- Build now uses C++23 (Geode 5.6.1 requires it). The previous C++20 setting could cause subtle ABI mismatches with `geode::Function`, coroutines, and some headers.

**Build / Toolchain**
- Project version bumped to 1.0.5 in CMakeLists.

**Compatibility**
- Removed incompatibility with `thesillydoggo.blur-api`. Paimbnails now coexists with mods that depend on Blur API (e.g. QOLMod). Our internal blur stays untouched and continues to work.

**Stability**
- Each phase of `$on_game(Exiting)` is now wrapped with try/catch via a `safeShutdownStep` helper. A failure in one shutdown step (audio, video, cache cleanup, etc.) no longer aborts the whole exit sequence — every other step still runs, the user's saved data is persisted, and the failure is logged with the step name for crashlogs.
- `ThumbnailLoader` now releases `CCImage` instances via `release()` instead of `delete`, respecting Cocos2d-x reference counting. Functionally equivalent in this path (refcount was 1) but idiomatically correct and safer if the object is ever retained elsewhere.
- Build now uses C++23 (Geode 5.6.1 requires it). The previous C++20 setting could cause subtle ABI mismatches with `geode::Function`, coroutines, and some headers.

**Build / Toolchain**
- Project version bumped to 1.0.5 in CMakeLists.

# v1.0.4

Release notes for Paimbnails 2 v1.0.4.

**Shaders**
- New dynamic shader system with runtime parameter updates
- Reworked shader pipeline for improved performance and flexibility
- Popup blur effect on opened popups
- Additional shaders bundled and selectable per layer

**UI / Controls**
- Custom sliders with new visuals and smoother behavior
- New dynamic input system for more responsive interactions
- Fast circular menu for quick access to common actions

**Audio**
- Custom menu music with per-layer overrides

**Integrations**
- Discord Rich Presence support showing current layer and activity

**Experimental**
- Paidraw beta: in-app drawing tool (early preview, opt-in)

**Technical**
- Removed ImagePlus dependency (no longer required to install)
- General optimization pass across rendering, audio, and asset pipelines
- Multiple bug fixes

# v1.0.1

Release notes for Paimbnails 2 v1.0.1.

**Thumbnails**
- Thumbnail previews in level cells (LevelBrowserLayer, LevelSearchLayer)
- Realtime search preview with configurable debounce
- Local thumbnail viewer popup with gallery mode and configurable transitions
- Concurrent download limit setting to control network usage

**Custom Backgrounds**
- Per-layer background configuration (menu, search, gauntlet, level select, profile)
- Support for static images, GIF animations, and video files (MP4)
- Video backgrounds with GPU-accelerated YUV decoding via Media Foundation (Windows), AVFoundation (macOS/iOS), and software fallback (Android/other)
- Background blur shaders (Kawase, Paimon blur) with configurable intensity
- Shared video player cache to avoid redundant decoder instances across layers
- Dark mode overlay and adaptive color modes for backgrounds

**Dynamic Song System**
- Per-layer music configuration with custom paths, song IDs, speed, and filter
- Smooth audio handoff between video background audio and dynamic songs

**Pet Companion**
- Animated pet sprite that follows the cursor on supported layers
- Idle, sleep, and reaction states tied to game events (level complete, death, practice exit)

**Badges**
- Custom badge icons alongside player names in comments and profiles
- Adaptive font scaling for long comments

**Emotes**
- Emote system for comments with CDN-backed asset delivery

**Accessibility / UI**
- Transparent list mode for level browsers
- Compact list mode
- Button scale animation on hover for registered Paimbnails UI elements

**Technical**
- Geode node-ids integration for cross-mod compatibility
- Runtime lifecycle manager for ordered shutdown (audio, video, texture caches)
- Cloudflare Worker backend with CDN fallback for read-only API endpoints
- API key authentication with optional mod-code for moderator features
