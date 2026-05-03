# v1.0.0-alpha.1

Initial alpha release of Paimbnails 2.

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
