#include "SpotifyMediaControl.hpp"
#include <Geode/platform/platform.hpp>

#ifdef GEODE_IS_WINDOWS
#include "../../../utils/ThreadTracker.hpp"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <algorithm>
#include <deque>
#include <memory>
#include <mutex>
#include <string_view>
#endif

namespace paimon::menumusic {

#ifdef GEODE_IS_WINDOWS
namespace {

bool isSpotifyMediaSource(std::string_view source) {
    std::string appId(source);
    std::ranges::transform(appId, appId.begin(), [](unsigned char ch) {
        return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + ('a' - 'A'))
                                     : static_cast<char>(ch);
    });
    return appId == "spotify.exe"
        || (appId.starts_with("spotifyab.spotifymusic_") && appId.ends_with("!spotify"));
}

struct MediaCommandQueue {
    std::mutex mutex;
    std::deque<std::string> commands;
    bool running = false;
};

std::shared_ptr<MediaCommandQueue> mediaCommandQueue() {
    static auto queue = std::make_shared<MediaCommandQueue>();
    return queue;
}

class MediaApartment {
public:
    MediaApartment() { winrt::init_apartment(winrt::apartment_type::multi_threaded); }
    ~MediaApartment() { winrt::uninit_apartment(); }
    MediaApartment(MediaApartment const&) = delete;
    MediaApartment& operator=(MediaApartment const&) = delete;
};

void executeSpotifyMediaCommand(std::string const& command) {
    using namespace winrt::Windows::Media::Control;
    try {
        MediaApartment apartment;
        auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
        for (auto const& session : manager.GetSessions()) {
            if (!isSpotifyMediaSource(winrt::to_string(session.SourceAppUserModelId()))) continue;

            bool applied = false;
            if (command == "play") applied = session.TryPlayAsync().get();
            else if (command == "pause") applied = session.TryPauseAsync().get();
            else if (command == "play_pause") applied = session.TryTogglePlayPauseAsync().get();
            else if (command == "next") applied = session.TrySkipNextAsync().get();
            else if (command == "prev") applied = session.TrySkipPreviousAsync().get();
            geode::log::info("[Spotify] Command '{}' targeted Spotify session: {}", command, applied);
            return;
        }
        geode::log::debug("[Spotify] Command '{}' skipped: no Spotify desktop session", command);
    } catch (winrt::hresult_error const& error) {
        geode::log::warn("[Spotify] Media command '{}' failed: {}",
            command, winrt::to_string(error.message()));
    }
}

}
#endif

void sendSpotifyMediaCommand(std::string const& command) {
#ifdef GEODE_IS_WINDOWS
    if (command != "play" && command != "pause" && command != "play_pause"
        && command != "next" && command != "prev") return;

    auto queue = mediaCommandQueue();
    {
        std::lock_guard lock(queue->mutex);
        queue->commands.push_back(command);
        if (queue->running) return;
        queue->running = true;
    }

    // WinRT calls wait only on the worker, and commands retain their scene order.
    bool const spawned = paimon::ThreadTracker::get().spawn([queue] {
        for (;;) {
            std::string command;
            {
                std::lock_guard lock(queue->mutex);
                if (queue->commands.empty()) {
                    queue->running = false;
                    return;
                }
                command = std::move(queue->commands.front());
                queue->commands.pop_front();
            }
            executeSpotifyMediaCommand(command);
        }
    });
    if (!spawned) {
        std::lock_guard lock(queue->mutex);
        queue->commands.clear();
        queue->running = false;
    }
#else
    (void)command;
#endif
}

}
