#pragma once
#include "../model/MenuMusicTypes.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_set>

namespace paimon::menumusic {

// the producer constructs this value on the game thread. No Geode objects or
// references to mutable library state cross into the worker.
struct MusicLibrarySnapshot {
    std::filesystem::path path;
    std::vector<MusicTrack> tracks;
    std::vector<MusicPlaylist> playlists;
    PlaybackMode mode = PlaybackMode::Disabled;
    std::string activePlaylistId;
    std::string lastTrackId;
    std::uint64_t idCounter = 0;
    // zero is a successfully loaded library. A nonzero generation identifies
    // one distinct failed read; callers increment it for every new failure.
    std::uint64_t loadFailureGeneration = 0;
};

class MenuMusicLibrarySaveQueue {
public:
    using Work = std::function<void()>;
    // scheduler must defer work to another thread; it must never run inline.
    using Scheduler = std::function<bool(Work)>;
    using Writer = std::function<bool(std::filesystem::path const&, std::string_view)>;
    using Completion = std::function<void(bool, std::uint64_t)>;

    static MenuMusicLibrarySaveQueue& get();
    static bool isInitialized();
    explicit MenuMusicLibrarySaveQueue(Scheduler scheduler, Writer writer = {});
    ~MenuMusicLibrarySaveQueue();
    MenuMusicLibrarySaveQueue(MenuMusicLibrarySaveQueue const&) = delete;
    MenuMusicLibrarySaveQueue& operator=(MenuMusicLibrarySaveQueue const&) = delete;

    // superseded callbacks are dropped. Completion runs on the worker; it must
    // queue UI changes to the main thread and validate the failure generation.
    bool enqueue(MusicLibrarySnapshot snapshot, Completion completion = {});
    bool retryPending();
    // at exit only: wait until the last accepted snapshot finishes (bounded by
    // timeout). If the scheduler rejects (e.g. ThreadTracker shutting down),
    // persists the pending snapshot inline. False means unsaved or timed out.
    bool shutdown(std::chrono::milliseconds timeout = std::chrono::milliseconds(1500));
    bool shutdownUntil(std::chrono::steady_clock::time_point deadline);

private:
    struct Request { MusicLibrarySnapshot snapshot; Completion completion; };
    bool scheduleLocked();
    void run();
    bool persist(MusicLibrarySnapshot const& snapshot);
    bool preserveCorruptFile(MusicLibrarySnapshot const& snapshot);

    Scheduler m_scheduler;
    Writer m_writer;
    std::mutex m_mutex;
    std::condition_variable m_finished;
    std::optional<Request> m_pending;
    bool m_running = false;
    bool m_closed = false;
    // these maps are accessed only by the serial worker.
    std::map<std::filesystem::path, std::unordered_set<std::uint64_t>> m_preserved;
    std::map<std::filesystem::path, std::unordered_set<std::uint64_t>> m_recovered;
    inline static std::atomic<bool> s_initialized{false};
};
} // namespace paimon::menumusic
