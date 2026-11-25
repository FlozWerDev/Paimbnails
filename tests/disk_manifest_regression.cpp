#include "../src/features/thumbnails/services/DiskManifest.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <thread>

using namespace paimon::cache;

int main() {
    DiskManifest manifest;
    DiskManifestEntry entry;
    entry.levelID = 12345;
    entry.filename = "12345.png";
    entry.revisionToken = std::string(256, 'a');
    entry.byteSize = 1024;
    entry.touchAccess();
    manifest.upsert(entry.levelID, false, entry);
    assert(manifest.totalBytes() == 1024);
    auto snapshot = manifest.getEntry(entry.levelID, false);
    assert(snapshot);
    std::thread invalidation([&] { manifest.remove(entry.levelID, false); });
    invalidation.join();
    assert(!manifest.getEntry(entry.levelID, false));
    assert(manifest.totalBytes() == 0);
    assert(snapshot->revisionToken == entry.revisionToken);
    assert(snapshot->filename == entry.filename);

    std::atomic<bool> finished{false};
    std::thread writer([&] {
        for (int i = 0; i < 3000; ++i) {
            manifest.upsert(entry.levelID, false, entry);
            manifest.remove(entry.levelID, false);
        }
        finished = true;
    });
    while (!finished) {
        auto copy = manifest.getEntry(entry.levelID, false);
        if (copy) assert(copy->revisionToken == entry.revisionToken);
    }
    writer.join();

    entry.lastAccessEpoch -= 31 * 24 * 3600;
    manifest.upsert(entry.levelID, false, entry);
    auto pinned = entry;
    pinned.levelID = 1;
    pinned.filename = "1.png";
    manifest.upsert(1, false, pinned);
    auto expired = manifest.computePrune(1024 * 1024, std::chrono::hours(24 * 30));
    assert(expired.items.size() == 1);
    assert(expired.items[0].filename == "12345.png");
    assert(expired.items[0].key == "12345");
    assert(expired.freedBytes == 1024);
    entry.touchAccess();
    manifest.upsert(entry.levelID, false, entry);
    assert(manifest.computePrune(1024 * 1024, std::chrono::hours(24 * 30)).items.empty());
    auto overQuota = manifest.computePrune(1024, std::chrono::hours(24 * 30));
    assert(overQuota.items.size() == 1);
    assert(overQuota.items[0].filename == "12345.png");
    // Running total tracks inserts and same-key replacement without a rescan.
    assert(manifest.totalBytes() == 2048);
    auto smaller = entry;
    smaller.byteSize = 512;
    manifest.upsert(smaller.levelID, false, smaller);
    assert(manifest.totalBytes() == 1536);
    manifest.clear();
    assert(manifest.totalBytes() == 0);
}
