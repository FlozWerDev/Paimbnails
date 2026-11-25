#include "../src/core/Settings.hpp"
#include "../src/core/modules/ModuleRegistry.hpp"
#include "../src/features/menu-music/services/MenuMusicLibrarySave.hpp"
#include "../src/utils/AtomicFileWrite.hpp"
#include "../src/utils/ThreadPool.hpp"
#include "../src/utils/ThreadTracker.hpp"

#include <Geode/Geode.hpp>
#include <matjson.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace paimon::modules {

char const* localizedName(Module const& mod) { return mod.name; }
char const* localizedDescription(Module const& mod) { return mod.description; }
char const* localizedSection(Section section) { return sectionName(section); }
char const* localizedGroup(char const* group) { return group; }

} // namespace paimon::modules

namespace {

fs::path freshTempDir() {
    auto dir = fs::temp_directory_path() /
        ("paimbnails-core-lifecycle-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    return dir;
}

std::string readTextFile(fs::path const& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

paimon::menumusic::MusicLibrarySnapshot makeSnapshot(fs::path path, std::uint64_t id) {
    paimon::menumusic::MusicLibrarySnapshot snap;
    snap.path = std::move(path);
    snap.idCounter = id;
    snap.mode = paimon::menumusic::PlaybackMode::Playlist;
    snap.activePlaylistId = "pl";
    snap.lastTrackId = "tr";
    paimon::menumusic::MusicTrack tr;
    tr.id = "tr";
    tr.displayName = "Track " + std::to_string(id);
    snap.tracks.push_back(tr);
    return snap;
}

void testThreadPoolTwoPhaseAndBoundedDetach() {
    {
        paimon::ThreadPool pool(2, "CoreTestPool");
        std::atomic<int> priorityOrder{0};
        std::atomic<int> normalRan{0};
        std::mutex gateMutex;
        std::condition_variable gateCv;
        bool holdGate = true;
        std::atomic<int> holdersEntered{0};

        for (int i = 0; i < 2; ++i) {
            pool.enqueue([&]() {
                if (++holdersEntered == 2) {
                    gateCv.notify_all();
                }
                std::unique_lock lock(gateMutex);
                gateCv.wait(lock, [&]() { return !holdGate; });
            });
        }
        {
            std::unique_lock lock(gateMutex);
            assert(gateCv.wait_for(lock, 2s, [&]() { return holdersEntered.load() == 2; }));
        }

        // Priority jobs run before queued regular jobs once holders release.
        std::vector<int> order;
        std::mutex orderMutex;
        pool.enqueue([&]() {
            std::lock_guard lock(orderMutex);
            order.push_back(2);
            ++normalRan;
        });
        pool.enqueueFront([&]() {
            std::lock_guard lock(orderMutex);
            order.push_back(1);
            ++priorityOrder;
        });

        {
            std::lock_guard lock(gateMutex);
            holdGate = false;
        }
        gateCv.notify_all();

        auto const deadline = std::chrono::steady_clock::now() + 1s;
        // Let queued items run briefly before shutdown drops anything still unstarted.
        for (int i = 0; i < 100 && (priorityOrder.load() == 0 || normalRan.load() == 0); ++i) {
            std::this_thread::sleep_for(5ms);
        }
        assert(priorityOrder.load() == 1);
        assert(normalRan.load() == 1);
        {
            std::lock_guard lock(orderMutex);
            assert((order == std::vector<int>{1, 2}));
        }

        pool.requestStop();
        assert(pool.isStopped());
        pool.enqueue([&]() { ++normalRan; }); // Dropped after requestStop.
        pool.waitShutdown(deadline);
        pool.waitShutdown(deadline); // Idempotent second wait.
        assert(normalRan.load() == 1);
    }

    // Stuck worker past deadline must detach cleanly within budget (never block POSIX join).
    std::mutex stuckMutex;
    std::condition_variable stuckCv;
    bool unblockStuck = false;
    std::atomic<bool> stuckEntered{false};
    std::atomic<bool> stuckExited{false};
    {
        paimon::ThreadPool stuckPool(1, "CoreStuckPool");
        stuckPool.enqueue([&]() {
            stuckEntered.store(true, std::memory_order_release);
            stuckCv.notify_all();
            std::unique_lock lock(stuckMutex);
            stuckCv.wait_for(lock, 5s, [&]() { return unblockStuck; });
            stuckExited.store(true, std::memory_order_release);
            stuckCv.notify_all();
        });
        {
            std::unique_lock lock(stuckMutex);
            assert(stuckCv.wait_for(lock, 2s, [&]() { return stuckEntered.load(std::memory_order_acquire); }));
        }
        auto const t0 = std::chrono::steady_clock::now();
        stuckPool.waitShutdown(t0 + 70ms);
        auto const elapsed = std::chrono::steady_clock::now() - t0;
        assert(elapsed < 400ms);
    }
    {
        std::lock_guard lock(stuckMutex);
        unblockStuck = true;
    }
    stuckCv.notify_all();
    {
        std::unique_lock lock(stuckMutex);
        assert(stuckCv.wait_for(lock, 2s, [&]() { return stuckExited.load(std::memory_order_acquire); }));
    }
}

void testThreadTrackerPolicies() {
    auto& tracker = paimon::ThreadTracker::get();
    std::atomic<bool> fastJoinDone{false};
    std::atomic<bool> timedJoinDone{false};

    std::mutex beMutex;
    std::condition_variable beCv;
    bool releaseBestEffort = false;
    std::atomic<bool> beEntered{false};
    std::atomic<bool> beExited{false};

    assert(tracker.spawnWithPolicy("FastJoin", paimon::ThreadTracker::ShutdownPolicy::Join, [&]() {
        std::this_thread::sleep_for(10ms);
        fastJoinDone.store(true, std::memory_order_release);
    }));
    assert(tracker.spawnWithPolicy("TimedJoin", paimon::ThreadTracker::ShutdownPolicy::Join, [&]() {
        std::this_thread::sleep_for(180ms);
        timedJoinDone.store(true, std::memory_order_release);
    }));
    assert(tracker.spawnWithPolicy("StuckBE", paimon::ThreadTracker::ShutdownPolicy::BestEffort, [&]() {
        beEntered.store(true, std::memory_order_release);
        beCv.notify_all();
        std::unique_lock lock(beMutex);
        beCv.wait_for(lock, 5s, [&]() { return releaseBestEffort; });
        beExited.store(true, std::memory_order_release);
        beCv.notify_all();
    }));

    {
        std::unique_lock lock(beMutex);
        assert(beCv.wait_for(lock, 2s, [&]() { return beEntered.load(std::memory_order_acquire); }));
    }

    tracker.requestShutdown();
    assert(tracker.isShuttingDown());
    assert(!tracker.spawn([]() {}));

    auto const t0 = std::chrono::steady_clock::now();
    // Wait waits for Join (~180ms) while letting BestEffort detach after grace (60ms).
    tracker.waitShutdown(t0 + 1500ms, 60ms);
    auto const elapsed = std::chrono::steady_clock::now() - t0;

    assert(fastJoinDone.load(std::memory_order_acquire));
    assert(timedJoinDone.load(std::memory_order_acquire));
    assert(elapsed < 700ms);

    {
        std::lock_guard lock(beMutex);
        releaseBestEffort = true;
    }
    beCv.notify_all();
    {
        std::unique_lock lock(beMutex);
        assert(beCv.wait_for(lock, 2s, [&]() { return beExited.load(std::memory_order_acquire); }));
    }
}

void testMenuMusicSaveQueueBoundedAndInlineFallback(fs::path const& root) {
    // 1. Stalled worker -> shutdown(60ms) returns false within bounded time.
    std::mutex stallMutex;
    std::condition_variable stallCv;
    bool unblockStall = false;
    std::atomic<bool> stallEntered{false};
    std::thread stalledThread;
    {
        paimon::menumusic::MenuMusicLibrarySaveQueue stalledQueue(
            [&](std::function<void()> work) {
                stalledThread = std::thread(std::move(work));
                return true;
            },
            [&](fs::path const&, std::string_view) {
                stallEntered.store(true, std::memory_order_release);
                stallCv.notify_all();
                std::unique_lock lock(stallMutex);
                stallCv.wait_for(lock, 5s, [&]() { return unblockStall; });
                return true;
            }
        );
        assert(stalledQueue.enqueue(makeSnapshot(root / "stalled.json", 10)));
        {
            std::unique_lock lock(stallMutex);
            assert(stallCv.wait_for(lock, 2s, [&]() { return stallEntered.load(std::memory_order_acquire); }));
        }
        auto const t0 = std::chrono::steady_clock::now();
        bool const ok = stalledQueue.shutdown(60ms);
        auto const elapsed = std::chrono::steady_clock::now() - t0;
        assert(!ok);
        assert(elapsed < 400ms);

        {
            std::lock_guard lock(stallMutex);
            unblockStall = true;
        }
        stallCv.notify_all();
        if (stalledThread.joinable()) stalledThread.join();
    }

    // 2. Rejected scheduler -> shutdown() persists the latest snapshot inline.
    auto const inlineFile = root / "inline-fallback.json";
    paimon::menumusic::MenuMusicLibrarySaveQueue fallbackQueue(
        [](std::function<void()>) { return false; }
    );
    assert(!fallbackQueue.enqueue(makeSnapshot(inlineFile, 77)));
    assert(!fs::exists(inlineFile));
    assert(fallbackQueue.shutdown(500ms));
    assert(fs::exists(inlineFile));
    auto parsed = matjson::parse(readTextFile(inlineFile));
    assert(parsed && parsed.unwrap()["idCounter"].asInt().unwrap() == 77);
}

void testModuleRegistryConcurrentInvalidation() {
    using namespace paimon::modules;
    geode::Mod mod;
    geode::Mod::set(&mod);

    auto const& catalog = all();
    assert(!catalog.empty());

    std::unordered_map<std::string, bool> customValues;
    std::vector<Module const*> nonCustom;
    for (auto const& m : catalog) {
        if (m.backing == Backing::Setting) {
            mod.setSettingValue<bool>(m.key, true);
            nonCustom.push_back(&m);
        } else if (m.backing == Backing::Saved) {
            mod.setSavedValue<bool>(m.key, true);
            nonCustom.push_back(&m);
        } else {
            customValues[m.id] = true;
            std::string id = m.id;
            registerAccessor(id,
                [&customValues, id]() { return customValues.at(id); },
                [&customValues, id](bool v) { customValues[id] = v; });
        }
    }
    paimon::settings::internal::invalidateSettingsCache();

    std::atomic<bool> stop{false};
    std::vector<std::thread> readers;
    readers.reserve(4);
    for (int r = 0; r < 4; ++r) {
        readers.emplace_back([&]() {
            while (!stop.load(std::memory_order_acquire)) {
                for (size_t i = 0; i < nonCustom.size(); i += 3) {
                    (void)isEnabled(nonCustom[i]->id);
                }
            }
        });
    }

    for (int step = 0; step < 30; ++step) {
        for (size_t i = 0; i < std::min<size_t>(5, nonCustom.size()); ++i) {
            setEnabled(*nonCustom[i], (step % 2) == 0);
        }
        paimon::settings::internal::invalidateSettingsCache();
        std::this_thread::sleep_for(1ms);
    }

    stop.store(true, std::memory_order_release);
    for (auto& t : readers) t.join();

    // After quiescence + invalidation, every cached lookup matches uncached evaluation.
    for (auto const* m : nonCustom) {
        assert(isEnabled(m->id) == isEnabled(*m));
    }

    auto const allFound = search("");
    assert(allFound.size() == catalog.size());
}

} // namespace

int main() {
    auto const root = freshTempDir();
    testThreadPoolTwoPhaseAndBoundedDetach();
    testMenuMusicSaveQueueBoundedAndInlineFallback(root);
    testModuleRegistryConcurrentInvalidation();
    // Run ThreadTracker last since ThreadTracker::get() latches shutdown state.
    testThreadTrackerPolicies();
    std::error_code ec;
    fs::remove_all(root, ec);
    return 0;
}
