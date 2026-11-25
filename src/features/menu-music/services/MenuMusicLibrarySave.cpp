#include "MenuMusicLibrarySave.hpp"
#include "../../../utils/AtomicFileWrite.hpp"
#include "../../../utils/ThreadTracker.hpp"
#include <matjson.hpp>
#include <atomic>
#include <chrono>
#include <exception>
#include <system_error>
#include <utility>

namespace paimon::menumusic {
namespace {
std::string serializeLibrary(MusicLibrarySnapshot const& snapshot) {
    auto root = matjson::Value::object();
    root["version"] = 2;
    root["mode"] = static_cast<int>(snapshot.mode);
    root["activePlaylistId"] = snapshot.activePlaylistId;
    root["lastTrackId"] = snapshot.lastTrackId;
    root["idCounter"] = static_cast<std::int64_t>(snapshot.idCounter);
    auto tracks = matjson::Value::array();
    for (auto const& track : snapshot.tracks) {
        auto item = matjson::Value::object();
        item["id"] = track.id;
        item["audioPath"] = track.audioPath;
        item["coverPath"] = track.coverPath;
        item["displayName"] = track.displayName;
        item["artist"] = track.artist;
        item["sourceUrl"] = track.sourceUrl;
        item["source"] = static_cast<int>(track.source);
        item["addedUnixMs"] = track.addedUnixMs;
        item["durationMs"] = track.durationMs;
        item["favorite"] = track.favorite;
        item["blacklisted"] = track.blacklisted;
        tracks.push(item);
    }
    root["tracks"] = tracks;
    auto playlists = matjson::Value::array();
    for (auto const& playlist : snapshot.playlists) {
        auto item = matjson::Value::object();
        item["id"] = playlist.id;
        item["name"] = playlist.name;
        item["createdUnixMs"] = playlist.createdUnixMs;
        auto ids = matjson::Value::array();
        for (auto const& id : playlist.trackIds) ids.push(id);
        item["trackIds"] = ids;
        playlists.push(item);
    }
    root["playlists"] = playlists;
    return root.dump();
}
}

bool MenuMusicLibrarySaveQueue::isInitialized() {
    return s_initialized.load(std::memory_order_acquire);
}

MenuMusicLibrarySaveQueue& MenuMusicLibrarySaveQueue::get() {
    // Heap-leaked: never run ~MenuMusicLibrarySaveQueue() under Windows Loader Lock in DllMain.
    auto& tracker = paimon::ThreadTracker::get();
    static auto* queue = [&tracker]() {
        auto* q = new MenuMusicLibrarySaveQueue([&tracker](Work work) {
            return tracker.spawn(std::move(work));
        });
        s_initialized.store(true, std::memory_order_release);
        return q;
    }();
    return *queue;
}
MenuMusicLibrarySaveQueue::MenuMusicLibrarySaveQueue(Scheduler scheduler, Writer writer)
    : m_scheduler(std::move(scheduler)), m_writer(std::move(writer)) {
    if (!m_writer) m_writer = [](auto const& path, std::string_view text) { return paimon::file::writeAtomically(path, text); };
}
MenuMusicLibrarySaveQueue::~MenuMusicLibrarySaveQueue() { (void)shutdown(); }

bool MenuMusicLibrarySaveQueue::scheduleLocked() {
    if (m_running || !m_pending) return true;
    if (!m_scheduler) return false;
    m_running = true;
    try {
        if (m_scheduler([this] { run(); })) return true;
    } catch (...) {
        // keep the latest snapshot in memory so a later attempt can retry.
    }
    m_running = false;
    return false;
}
bool MenuMusicLibrarySaveQueue::enqueue(MusicLibrarySnapshot snapshot, Completion completion) {
    std::lock_guard lock(m_mutex);
    if (m_closed) return false;
    m_pending = Request{std::move(snapshot), std::move(completion)};
    return scheduleLocked();
}
bool MenuMusicLibrarySaveQueue::retryPending() {
    std::lock_guard lock(m_mutex);
    if (m_closed) return false;
    return scheduleLocked();
}
bool MenuMusicLibrarySaveQueue::shutdown(std::chrono::milliseconds timeout) {
    return shutdownUntil(std::chrono::steady_clock::now() + timeout);
}

bool MenuMusicLibrarySaveQueue::shutdownUntil(std::chrono::steady_clock::time_point deadline) {
    std::unique_lock lock(m_mutex);
    m_closed = true;
    if (!m_running && m_pending && !scheduleLocked()) {
        // Scheduler rejected (e.g. ThreadTracker shutting down): finish pending write inline.
        auto request = std::move(m_pending);
        m_pending.reset();
        lock.unlock();
        bool saved = false;
        try {
            saved = persist(request->snapshot);
        } catch (...) {
            saved = false;
        }
        if (request->completion) {
            try {
                request->completion(saved, request->snapshot.loadFailureGeneration);
            } catch (...) {}
        }
        lock.lock();
        if (!saved && !m_pending) {
            m_pending = std::move(request);
        }
        return !m_pending;
    }
    (void)m_finished.wait_until(lock, deadline, [this] { return !m_running; });
    return !m_running && !m_pending;
}

bool MenuMusicLibrarySaveQueue::preserveCorruptFile(MusicLibrarySnapshot const& snapshot) {
    auto generation = snapshot.loadFailureGeneration;
    if (generation == 0 || m_preserved[snapshot.path].contains(generation)) return true;
    std::error_code ec;
    bool exists = std::filesystem::exists(snapshot.path, ec);
    if (ec) return false;
    if (exists) {
        // copy first: a crash or failed atomic write must leave the original in
        // place. A pre-existing .corrupt is never replaced.
        auto base = snapshot.path;
        base += ".corrupt";
        bool copied = false;
        for (int suffix = 0; suffix < 10000; ++suffix) {
            auto destination = base;
            if (suffix) destination += "." + std::to_string(suffix);
            ec.clear();
            if (std::filesystem::copy_file(snapshot.path, destination, std::filesystem::copy_options::none, ec)) {
                copied = true; break;
            }
            if (ec != std::errc::file_exists) return false;
        }
        if (!copied) return false;
    }
    m_preserved[snapshot.path].insert(generation);
    return true;
}
bool MenuMusicLibrarySaveQueue::persist(MusicLibrarySnapshot const& snapshot) {
    if (snapshot.path.empty()) return false;
    auto generation = snapshot.loadFailureGeneration;
    if (generation && snapshot.tracks.empty() && snapshot.playlists.empty() &&
        !m_recovered[snapshot.path].contains(generation)) return false;
    if (!preserveCorruptFile(snapshot)) return false;
    if (!m_writer(snapshot.path, serializeLibrary(snapshot))) return false;
    if (generation) m_recovered[snapshot.path].insert(generation);
    return true;
}
void MenuMusicLibrarySaveQueue::run() {
    for (;;) {
        std::optional<Request> request;
        {
            std::lock_guard lock(m_mutex);
            if (!m_pending) {
                m_running = false; m_finished.notify_all(); return;
            }
            request = std::move(m_pending);
            m_pending.reset();
        }
        bool saved = false;
        try { saved = persist(request->snapshot); } catch (...) { saved = false; }
        if (request->completion) {
            try { request->completion(saved, request->snapshot.loadFailureGeneration); } catch (...) {}
        }
        if (!saved) {
            std::lock_guard lock(m_mutex);
            // a newer snapshot supersedes the failed one and gets one attempt.
            // if none exists, retain it for an explicit or later retry.
            if (!m_pending) {
                m_pending = std::move(request);
                m_running = false; m_finished.notify_all(); return;
            }
        }
    }
}
} // namespace paimon::menumusic
