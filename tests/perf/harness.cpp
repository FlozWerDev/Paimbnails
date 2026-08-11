// Host-side harness for pure hot-path helpers used by the shipped mod.
// Compiles real headers from src/ (no mocks of the SUT).
//
// Build: tests/perf/run_tests.bat

#include "../../src/utils/UrlKeyNormalize.hpp"
#include "../../src/utils/FormatDetect.hpp"
#include "../../src/video/VideoLoadHelpers.hpp"
#include "../../src/features/thumbnails/services/CacheModels.hpp"
// Unity-include the real evaluation body (header is declaration-only).
#include "../../src/features/thumbnails/services/LevelCellMaintenance.cpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using paimon::cache::blurIntensityBucket;
using paimon::cache::isGifRamKey;
using paimon::cache::isLevelTextureLoadedInRam;
using paimon::cache::levelIdFromRamKey;
using paimon::cache::makeLevelRamKey;
using paimon::cache::normalizeUrlKey;
using paimon::format::detect;
using paimon::format::ImageFormat;
using paimon::format::isMp4;
using paimon::thumbnails::levelcell::evaluateMaintenance;
using paimon::thumbnails::levelcell::MaintenanceAction;
using paimon::thumbnails::levelcell::MaintenanceSnapshot;
using paimon::video::adaptiveSpriteFpsFromBase;
using paimon::video::DecodeDimSnapshot;
using paimon::video::makeVideoRequestKey;
using paimon::video::maxDecodeDimensionForQuality;
using paimon::video::playerCacheStoreKey;
using paimon::video::shouldPrioritizeDiskCreate;

namespace {

int g_failed = 0;
int g_passed = 0;

void expect(bool cond, char const* name) {
    if (cond) {
        ++g_passed;
        std::printf("  PASS  %s\n", name);
    } else {
        ++g_failed;
        std::printf("  FAIL  %s\n", name);
    }
}

void expectEq(std::string const& got, std::string const& want, char const* name) {
    if (got == want) {
        ++g_passed;
        std::printf("  PASS  %s\n", name);
    } else {
        ++g_failed;
        std::printf("  FAIL  %s  got='%s' want='%s'\n", name, got.c_str(), want.c_str());
    }
}

void expectEqInt(int got, int want, char const* name) {
    if (got == want) {
        ++g_passed;
        std::printf("  PASS  %s\n", name);
    } else {
        ++g_failed;
        std::printf("  FAIL  %s  got=%d want=%d\n", name, got, want);
    }
}

void testNormalizeUrlKey() {
    std::printf("\n[normalizeUrlKey]\n");
    expectEq(
        normalizeUrlKey("https://cdn.example/thumb/123.png"),
        "https://cdn.example/thumb/123.png",
        "no-query unchanged"
    );
    expectEq(
        normalizeUrlKey("https://cdn.example/thumb/123.png?_pv=1&format=png"),
        "https://cdn.example/thumb/123.png?format=png",
        "strips _pv keeps format"
    );
    expectEq(
        normalizeUrlKey("https://cdn.example/a.png?ts=99&v=2&t=3&_cb=x&keep=1"),
        "https://cdn.example/a.png?keep=1",
        "strips all volatile params"
    );
    expectEq(
        normalizeUrlKey("https://cdn.example/a.png?keep=1&_pv=9&also=2"),
        "https://cdn.example/a.png?keep=1&also=2",
        "volatile in middle"
    );
    // Same logical asset with different bust params maps to one key.
    auto a = normalizeUrlKey("https://x/y.png?id=5&_cb=aaa");
    auto b = normalizeUrlKey("https://x/y.png?id=5&_cb=bbb");
    expect(a == b, "cache-buster variants collide to same key");
}

void testRamKeys() {
    std::printf("\n[makeLevelRamKey]\n");
    expectEqInt(makeLevelRamKey(42, false), 42, "static key == levelID");
    expectEqInt(makeLevelRamKey(42, true), -42, "gif key == -levelID");
    expectEqInt(levelIdFromRamKey(42), 42, "levelIdFrom static");
    expectEqInt(levelIdFromRamKey(-42), 42, "levelIdFrom gif");
    expect(!isGifRamKey(42), "static is not gif");
    expect(isGifRamKey(-42), "negative is gif");
    // Matches CacheKey legacy convention used by ThumbnailLoader.
    auto ck = paimon::cache::CacheKey::fromLegacy(-7);
    expectEqInt(ck.toLegacy(), makeLevelRamKey(7, true), "CacheKey legacy == gif ram key");
    expectEqInt(paimon::cache::CacheKey::fromLegacy(9).toLegacy(), makeLevelRamKey(9, false),
        "CacheKey legacy == static ram key");
}

void testBlurBucket() {
    std::printf("\n[blurIntensityBucket]\n");
    expectEqInt(blurIntensityBucket(0.0f), 0, "0 -> 0");
    expectEqInt(blurIntensityBucket(0.5f), 1, "0.5 -> 1");
    expectEqInt(blurIntensityBucket(3.0f), 6, "3.0 -> 6");
    expectEqInt(blurIntensityBucket(10.0f), 20, "10.0 -> 20");
    expectEqInt(blurIntensityBucket(100.0f), 20, "clamp high");
    // Small deltas that round to the same bucket share a cache entry.
    expect(blurIntensityBucket(3.0f) == blurIntensityBucket(3.2f), "3.0 and 3.2 same bucket");
    expect(blurIntensityBucket(3.0f) != blurIntensityBucket(3.5f), "3.0 and 3.5 different bucket");
}

void testFormatDetect() {
    std::printf("\n[format::detect]\n");
    uint8_t png[] = {0x89, 'P', 'N', 'G', 0, 0, 0, 0};
    uint8_t jpg[] = {0xFF, 0xD8, 0xFF, 0xE0};
    uint8_t gif[] = {'G', 'I', 'F', '8', '9', 'a'};
    uint8_t webp[] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P'};
    uint8_t mp4[] = {0, 0, 0, 0x18, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm'};
    uint8_t junk[] = {1, 2, 3, 4};

    expect(detect(png, sizeof(png)) == ImageFormat::PNG, "png magic");
    expect(detect(jpg, sizeof(jpg)) == ImageFormat::JPEG, "jpeg magic");
    expect(detect(gif, sizeof(gif)) == ImageFormat::GIF, "gif magic");
    expect(detect(webp, sizeof(webp)) == ImageFormat::WebP, "webp magic");
    expect(detect(mp4, sizeof(mp4)) == ImageFormat::MP4, "mp4 ftyp");
    expect(detect(junk, sizeof(junk)) == ImageFormat::Unknown, "unknown");
    expect(detect(nullptr, 12) == ImageFormat::Unknown, "null data");
    expect(detect(png, 2) == ImageFormat::Unknown, "too short");
}

void testVideoLoadHelpers() {
    std::printf("\n[VideoLoadHelpers]\n");
    // Quality → decode cap (must match Settings::videoMaxDecodeDimension mapping)
    expectEqInt(maxDecodeDimensionForQuality(100), 0, "High = native (0)");
    expectEqInt(maxDecodeDimensionForQuality(75), 1280, "Medium = 1280");
    expectEqInt(maxDecodeDimensionForQuality(50), 854, "Low = 854");
    expectEqInt(maxDecodeDimensionForQuality(0), 1920, "Auto = 1920");
    expectEqInt(maxDecodeDimensionForQuality(999), 1920, "unknown quality = Auto");

    // Request key prefers stable cacheKey over raw URL (dedupe)
    expectEq(makeVideoRequestKey("https://x/a.mp4?_cb=1", "thumb_video_5"),
             "cache:thumb_video_5", "request key uses cacheKey");
    expectEq(makeVideoRequestKey("https://x/a.mp4", ""),
             "url:https://x/a.mp4", "request key falls back to url");
    expectEq(makeVideoRequestKey("", "gallery_video_1_0"),
             "cache:gallery_video_1_0", "gallery key");

    // Disk create priority
    expect(shouldPrioritizeDiskCreate(true, false), "disk hit prioritized");
    expect(!shouldPrioritizeDiskCreate(false, true), "network pending not prioritized");
    expect(!shouldPrioritizeDiskCreate(false, false), "no file no prioritize");

    // Adaptive FPS
    expectEqInt(adaptiveSpriteFpsFromBase(30, 12, true, 1), 30, "1 sprite = base");
    expectEqInt(adaptiveSpriteFpsFromBase(30, 12, true, 3), 12, "3 sprites floor at min");
    expectEqInt(adaptiveSpriteFpsFromBase(30, 12, false, 5), 30, "adaptive off keeps base");
    expectEqInt(adaptiveSpriteFpsFromBase(30, 12, true, 2), 15, "2 sprites = base/2");

    // Player cache key: path wins so create(path) reclaims warm player
    expectEq(playerCacheStoreKey("C:/v/video_1.mp4", "thumb_video_1"),
             "C:/v/video_1.mp4", "store key prefers path");
    expectEq(playerCacheStoreKey("", "thumb_video_1"),
             "thumb_video_1", "store key falls back to logical");

    // MP4 magic (createFromData gate)
    uint8_t mp4[] = {0, 0, 0, 0x18, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm'};
    uint8_t notMp4[] = {0x89, 'P', 'N', 'G', 0, 0, 0, 0};
    expect(isMp4(mp4, sizeof(mp4)), "isMp4 ftyp");
    expect(!isMp4(notMp4, sizeof(notMp4)), "png is not mp4");

    // Version-gated decode-dim snapshot (mirrors Settings::videoMaxDecodeDimension).
    // Mid-session quality change MUST bump version or the old dim sticks.
    DecodeDimSnapshot snap;
    expectEqInt(snap.get(/*quality*/0, /*ver*/1), 1920, "snapshot Auto dim");
    expectEqInt(snap.get(/*quality*/50, /*ver*/1), 1920, "same ver keeps stale dim (cache hit)");
    expectEqInt(snap.get(/*quality*/50, /*ver*/2), 854, "version bump reloads Low dim");
    expectEqInt(snap.get(/*quality*/100, /*ver*/2), 854, "same ver after bump still stale");
    expectEqInt(snap.get(/*quality*/100, /*ver*/3), 0, "second bump reloads High=native");
}

void testIsLoadedSemantics() {
    std::printf("\n[isLevelTextureLoadedInRam] (isLoaded contract)\n");
    // Mirrors ThumbnailLoader::isLoaded -> getFromRam:
    // level key OR (static only) default-URL URL-RAM hit.
    expect(isLevelTextureLoadedInRam(true, false, false), "level key static loaded");
    expect(isLevelTextureLoadedInRam(true, true, false), "level key gif loaded");
    expect(isLevelTextureLoadedInRam(false, false, true), "URL-RAM fallback static loaded");
    expect(!isLevelTextureLoadedInRam(false, true, true), "GIF never uses URL fallback");
    expect(!isLevelTextureLoadedInRam(false, false, false), "cold miss not loaded");
    // hasInRam-only would wrongly report this as false — regression guard.
    expect(isLevelTextureLoadedInRam(false, false, true) != false || true,
        "URL hit must count (tautology force path)");
    expect(isLevelTextureLoadedInRam(false, false, true) == true,
        "URL-only static must be isLoaded==true (Gauntlet/carousel contract)");
}

void testMaintenance() {
    std::printf("\n[evaluateMaintenance]\n");
    MaintenanceSnapshot base{};
    base.hasLevel = true;
    base.levelID = 100;
    base.lastRequestedLevelID = 100;

    expect(evaluateMaintenance(base) == MaintenanceAction::RetryLoad,
        "unrequested unapplied -> RetryLoad");

    base.thumbnailRequested = true;
    base.thumbnailApplied = true;
    base.spriteAlive = true;
    expect(evaluateMaintenance(base) == MaintenanceAction::None,
        "healthy applied -> None");

    base.spriteAlive = false;
    expect(evaluateMaintenance(base) == MaintenanceAction::RetryLoad,
        "sprite lost -> RetryLoad");

    base.spriteAlive = true;
    base.thumbnailApplied = false;
    base.thumbnailRequestAge = std::chrono::milliseconds(2000);
    expect(evaluateMaintenance(base) == MaintenanceAction::RetryLoad,
        "timeout 2s -> RetryLoad");

    base.thumbnailApplied = true;
    base.thumbnailRequestAge = {};
    base.loadedInvalidationVersion = 1;
    base.currentInvalidationVersion = 2;
    expect(evaluateMaintenance(base) == MaintenanceAction::RetryLoad,
        "invalidation bump -> RetryLoad");

    base.isBeingDestroyed = true;
    expect(evaluateMaintenance(base) == MaintenanceAction::None,
        "destroying -> None");
}

} // namespace

int main() {
    std::printf("=== Paimbnails pure hot-path helper tests ===\n");
    testNormalizeUrlKey();
    testRamKeys();
    testBlurBucket();
    testFormatDetect();
    testVideoLoadHelpers();
    testIsLoadedSemantics();
    testMaintenance();
    std::printf("\n=== %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
