# Paimbnails

Developed by <cg>F</c><cg>l</c><cl>o</c><cl>z</c><cl>W</c><cg>e</c><cg>r</c> : [Profile](user:17046382)

Paimbnails transforms Geometry Dash into a <cl>fully visual experience</c>. Thumbnails, effects, audio, emotes, pets, profiles and more. All seamlessly integrated into the game you already love.

\---

## Level Requests

Connect Twitch, YouTube, Kick and TikTok chats or your public web request page. Viewers send `!req 12345 description`, optionally with a video URL; the description is saved and can be read in the request list. Each chat can use its own commands, or inherit the default commands from settings.

Open **Origenes** in the request screen to choose a default queue for each platform and add rules for individual commands or Twitch rewards. For example, send `!req` to **General**, one reward to **Priority** and another to **Challenges**. Give multiple rules the same queue name to combine them. **Cola** switches between a named queue and **Todas**; the list, next level, navigation arrows, clearing and OBS queue follow that selection. Existing requests keep their saved destination.

For Twitch channel points, enable **Pedir texto** on a custom reward. Connect Twitch, open **Origenes > Vincular un canje > Detectar siguiente**, redeem that reward with `12345 description`, then save its name and destination. Enable **Aceptar canjes vinculados**; disable Twitch commands if you want requests only through points. Each reward is matched by its ID. Unlinked rewards are ignored. This chat integration requires a text redemption while connected; it does not fulfill or refund rewards on Twitch.

Duplicate levels, user limits and cooldowns apply within each queue. Storage capacity and level filters are shared. Replayed chat messages are deduplicated by their event IDs. The **Solo usuarios verificados** filter accepts verified GD web users; live chat accounts cannot verify a GD account. The live chat connectors retain their platform support and network requirements.

## Thumbnails

<cb>**Level Thumbnails**</c> appear on every level cell: in search results, lists, gauntlets, map packs, daily/weekly/event and official levels. Profile pages also get their own <cb>thumbnail visuals</c>. Browse all thumbnails for a level in the <cb>**Thumbnail Gallery**</c> or capture your own using the <co>**In-Game Capture**</c> system with full scene detection and the <co>**Layer Editor**</c> to compose the perfect shot. Thumbnails support <cb>PNG, GIF, WebP and MP4 video</c>.

## Visual Effects

<cp>**17 background styles**</c> for the level info screen: Pixel, Blur, Grayscale, Sepia, Vignette, Scanlines, Bloom, Chromatic, Radial Blur, Glitch, Posterize, Rain, Matrix, Neon Pulse, Wave Distortion, CRT and Normal. <cp>**Stack up to 4 effects**</c> for unique combos. Over <cp>**20 hover effects**</c> on level cells: brightness, darken, sepia, gold, rainbow, invert, pixelate and more. Plus <cp>animated gradients</c>, <cp>mythic particles</c> and <cp>configurable hover animations</c>.

## Paimon RTX

<co>**Real-time screen-space ray tracing**</c> over the whole game. Paimbnails reads the finished frame, derives a surface from it (relief from contrast, normals from its derivatives, emitters from brightness) and marches rays across it for <co>bounced light tinted by whatever reflects it</c>, <co>contact occlusion</c> and <co>screen-space reflections</c>. On top of that: multi-level <co>bloom</c>, <co>volumetric light shafts</c>, four <co>tonemapping curves</c> (ACES, Filmic, Uncharted 2, Reinhard), color grading and lens effects. Built to stay cheap: tracing runs at <co>20-100% of screen resolution</c> with <co>adaptive quality</c>, frame skipping, temporal accumulation and four presets. <co>51 controls</c> in <co>Extras > Paimon RTX</c>.

## Frame Interpolation

Geometry Dash simulates at <cj>240 fixed ticks per second</c> and draws whenever the display refreshes, so every frame shows a state that has been standing still for anywhere between zero and a full tick. That leftover changes frame to frame, and it is what you see as <cj>micro-stutter</c> at 144, 165 or 240 Hz. Paimbnails keeps the last two ticks and draws the in-between state the real clock asks for, so motion advances by the same amount every frame. <cj>Camera</c>, <cj>background and ground</c>, <cj>both players</c> and, optionally, <cj>trigger-driven objects</c> each get their own switch, with three <cj>latency modes</c> and a strength dial. The simulation is never touched: the authoritative values are restored the moment the frame is drawn, so physics, replays and percentages come out identical.

## Custom Transitions

Over <cg>**30 screen transitions**</c>: fades, slides, flips, zooms, page curls, tile effects, radial wipes and more. Set a <cg>separate transition for entering levels</c> or create <cg>custom scripted sequences</c> combining fade, move, scale, and rotate.

## Custom Progress Bar

<cg>**Full control**</c> over the in-game progress bar: move, scale, rotate, or set <cg>vertical orientation</c>. Pick <cg>custom colors</c> with solid, pulse, or rainbow animation modes for the fill, background and percentage label. Swap in your own <cg>textures</c> (PNG, JPG, WebP or GIF) and add <cg>free-floating decorations</c>. The percentage font, position and offset are fully configurable too. Enable <cg>free drag</c> from the pause menu or enter the <cg>edit overlay</c> to fine-tune everything live.

## Mod Previews

When you open a Geode mod, Paimbnails shows <cb>**preview images**</c> for any mod whose repository includes a <cb>`previews`</c> folder. Click a thumbnail for a <cb>full-screen gallery</c>. Inspired by <cb>Alphalaneous</c>.

## Audio

<cy>**Dynamic Song**</c> plays the level's song while browsing. <cy>**Stream Undownloaded Songs**</c> to preview them before downloading. <cy>**Profile Music**</c> lets users set a custom song fragment on their profile with waveform visualization and crossfade. All audio sources coordinate automatically, <cy>no conflicts, smooth transitions</c>. The leaderboard features <cy>real-time beat detection</c> with FFT spectrum analysis powering reactive visuals.

## Emotes

A full <cj>**emote system**</c> for comments and text inputs. Pick emotes from the <cj>built-in picker</c>, use <cj>autocomplete</c> as you type, and see them <cj>rendered inline</c> in supported views.

## Pet System

Adopt <co>**custom pets**</c> from the Pet Shop and watch them follow you across menus. Manage your collection in the <co>**Pet Gallery**</c>, choose which screens they appear on and enjoy auto-cleanup for corrupted files.

## Profile Features

<cl>**Rate profiles**</c> with 1–5 stars and a message. View all <cl>profile reviews</c> directly from the profile page. VIP users, Moderators, and Admins can set <cl>animated GIFs</c> as their profile picture.

## Profile Levels

Every profile gets an <cy>**XP level, a tier and badges**</c> derived from its public stats. Stars, moons, diamonds, coins and creator points all pay XP, and <cy>demons pay by difficulty</c> from easy to extreme. <cy>**200 levels across 20 tiers**</c>, <cy>**124 badges**</c> in twelve categories, an animated breakdown of where your XP comes from, and a card that slides in with the gain every time you beat a level.

## Custom Backgrounds

Customize the background for <cg>CreatorLayer</c>, <cg>LevelBrowserLayer</c>, <cg>LevelSearchLayer</c> and <cg>LeaderboardsLayer</c> individually. Options include Custom Image, Random Thumbnail, Level ID, same as Menu or GD Default, each with <cg>dark mode and intensity controls</c>.

## Level Cell Settings

One popup to configure everything: <cy>background type, thumbnail size, blur, darkness, separator, view button, compact mode, hover animations, mythic particles and animated gradients</c>. All changes <cy>apply instantly</c>.

## Moderation

<cr>**Verification Center**</c> for reviewing thumbnails, updates, reports, banners and profile images. <cr>**Ban system**</c> with reasons. <cr>**Moderator management**</c>. <cr>**Report system**</c> for inappropriate content. <cr>Admin and Moderator badges</c> on comments and profiles.

## Performance

<cg>Multi-layer caching</c> (RAM + disk) with LRU eviction and manifest persistence. <cg>Priority-based loading</c>, visible cells first, prefetch in background. <cg>Batched GPU uploads</c> to prevent stuttering. <cg>Request deduplication</c> and <cg>exponential backoff</c> on failures. CDN-accelerated downloads via batch manifest lookups.

\---

## Inspirations & Third-Party Credits

Some Paimbnails features were inspired by ideas from other creators. Each one below is an <cb>independent implementation written from scratch</c> — no code was copied — but the idea deserves credit. There is one stated exception: <cb>Search History</c>, which ports logic from the MIT-licensed original (see below and THIRD-PARTY-NOTICES.md). <cb>Separate Dual Icons</c> was also audited against its unlicensed original and rewritten from its behavior spec the same day; see THIRD-PARTY-NOTICES.md. Paimbnails also interoperates with third-party services and mods, credited at the end of the list.

* <cb>Mod Previews</c>: inspired by <cb>Alphalaneous</c>. Preview images belong to each mod's own repository authors and are only displayed, never redistributed.
* <cb>Message Notifications</c>: inspired by <cb>BlueToadMaker</c>. It queries RobTop's own public endpoints, the same ones the game client uses.
* <cb>Search History</c>: based on <cb>Search History</c> by <cb>hiimjasmine00</c> (MIT License, Copyright (c) 2024-2026 hiimjasmine00), with Paimbnails UI and persistence.
* <cb>Separate Dual Icons</c>: inspired by <cb>Weebify</c>. Independently rewritten from the behavior spec (2026-09-20); see THIRD-PARTY-NOTICES.md.
* <cb>Custom Cursor</c> (click-state tracking): inspired by <cb>Ecuet</c>. The cursor shop browses <cb>rw-designer.com</c> and <cb>custom-cursor.com</c>: every artwork belongs to those sites and their authors, listings link back to the original pages, and files download only when you explicitly install them.
* <cb>Texture Studio</c> recoloring idea inspired by <cb>PackGen</c> by <cb>Asterveila</c>, building on <cb>ravexcode</c>'s TexturePackWeb. Independent implementation, fully self-contained: no external downloads or mirror. See THIRD-PARTY-NOTICES.md for details.

If you are one of these authors and want the wording changed, open an issue or reach out on Discord and it will be fixed.

## Discord

* [Discord Server](https://discord.gg/5N5vpSfZwY)

## Special Thanks

<cr>Admins</c>:
[AlvaroEter](user:17431458)

<cp>Moderators \& Playtesters</c>:
[Robert55GD](user:25339555), [Jhano04](user:1371046), [Debihan](user:4315943), [SirexcelDJ](user:4098680), [Neeki](user:14195348), [Zartnez](user:9851985), [AdrixOnCube](user:22682981), [Killtama](user:10444919), [ZeroCheck55](user:13594949), [Maxclaudio5](user:15161769), [Aztraa2](user:38945801), [GATR2007](user:13919980), [ElD0c](user:6205966), [ChuchitoDomin](user:8102168), [nightrex](user:14428020), [assassindarkexe](user:25498849).
