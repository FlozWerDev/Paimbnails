#include <Geode/Geode.hpp>
#include <Geode/modify/CCApplication.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/CCScheduler.hpp>
#include "../utils/Debug.hpp"
#include "../utils/FramePerf.hpp"

#include <atomic>
#include <chrono>

using namespace geode::prelude;

// lightweight diagnostics for the remaining stutter reports. These only log
// when a main-thread frame or a save takes long enough to be felt; they do not
// change any game/mod behavior.
namespace {
    std::atomic<bool> s_perfReady{false};

    std::chrono::steady_clock::time_point now() {
        return std::chrono::steady_clock::now();
    }

    long long elapsedMs(std::chrono::steady_clock::time_point start) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(now() - start).count();
    }

    int64_t microsSince(std::chrono::steady_clock::time_point start) {
        return std::chrono::duration_cast<std::chrono::microseconds>(now() - start).count();
    }
}

$on_mod(Loaded) {
    s_perfReady.store(true, std::memory_order_release);
    PaimonDebug::log("[Perf] watchdog active (mod loaded)");
}

struct $modify(PaimonFrameGapPerf, CCDirector) {
    void drawScene() {
        if (!PaimonDebug::isEnabled()) {
            CCDirector::drawScene();
            return;
        }

        static auto last = now();
        static auto windowStart = now();
        static bool wasReady = false;
        static bool loggedFirstCall = false;
        static int frames = 0;
        static int over50 = 0;
        static int over100 = 0;
        static long long totalGapUs = 0;
        static long long maxGapUs = 0;
        static long long totalDrawUs = 0;
        static long long maxDrawUs = 0;

        auto const current = now();
        auto const gapUs = std::chrono::duration_cast<std::chrono::microseconds>(current - last).count();
        last = current;

        bool const ready = s_perfReady.load(std::memory_order_acquire);
        if (!loggedFirstCall) {
            loggedFirstCall = true;
            PaimonDebug::log("[Perf] drawScene hook first call (ready={})", ready);
        }

        // start a fresh window when the mod finishes loading, so startup
        // loading work is not reported as a gameplay hitch.
        if (ready && !wasReady) {
            wasReady = true;
            windowStart = current;
            frames = 0;
            over50 = over100 = 0;
            totalGapUs = maxGapUs = 0;
            totalDrawUs = maxDrawUs = 0;
        }

        paimon::frameperf::beginFrame();
        auto const drawStart = now();
        CCDirector::drawScene();
        auto const drawUs = microsSince(drawStart);
        paimon::frameperf::active = false;

        // correlate costs from the SAME frame. A frame gap alone cannot tell
        // rendering work from waiting in swapBuffers/vsync/event processing.
        if (ready && drawUs >= 50'000) {
            using namespace paimon::frameperf;
            auto* play = PlayLayer::get();
            log::warn("[Perf] slow-frame n={} context={} total={}us scheduler={}us capture={}us rtx={}us cursor={}us present={}us capture_callbacks={}us popup_blur={}us",
                getTotalFrames(), play ? (play->m_isPaused ? "paused" : "play") : "menu",
                drawUs, time(Stage::Scheduler), time(Stage::Capture), time(Stage::RTX),
                time(Stage::Cursor), time(Stage::Present), time(Stage::CaptureCallbacks),
                time(Stage::PopupBlur));
        }

        if (!ready) return;

        // a multi-second gap means the window was minimized/paused; do not
        // count it as a hitch and restart the sample window.
        if (gapUs >= 5'000'000) {
            windowStart = current;
            frames = 0;
            over50 = over100 = 0;
            totalGapUs = maxGapUs = 0;
            totalDrawUs = maxDrawUs = 0;
            return;
        }

        ++frames;
        totalGapUs += gapUs;
        totalDrawUs += drawUs;
        if (gapUs > maxGapUs) maxGapUs = gapUs;
        if (drawUs > maxDrawUs) maxDrawUs = drawUs;
        if (gapUs >= 50'000) ++over50;
        if (gapUs >= 100'000) ++over100;

        if (gapUs >= 120'000) {
            log::warn("[Perf] main-thread frame gap {} ms", gapUs / 1000);
        }

        auto const windowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            current - windowStart).count();
        if (windowMs >= 10'000) {
            log::info("[Perf] {}s: frames={} avg_gap={}us max_gap={}ms avg_draw={}us max_draw={}ms hitches>50ms={} >100ms={}",
                windowMs / 1000, frames, frames ? totalGapUs / frames : 0,
                maxGapUs / 1000, frames ? totalDrawUs / frames : 0,
                maxDrawUs / 1000, over50, over100);

            windowStart = current;
            frames = 0;
            over50 = over100 = 0;
            totalGapUs = maxGapUs = 0;
            totalDrawUs = maxDrawUs = 0;
        }
    }
};

struct $modify(PaimonSchedulerPerf, CCScheduler) {
    void update(float dt) {
        if (!PaimonDebug::isEnabled()) {
            CCScheduler::update(dt);
            return;
        }

        static bool loggedFirstCall = false;
        static int calls = 0;
        static long long totalUs = 0;
        static long long maxUs = 0;
        static auto windowStart = now();

        if (!loggedFirstCall) {
            loggedFirstCall = true;
            PaimonDebug::log("[Perf] CCScheduler::update hook first call");
        }

        auto const start = now();
        {
            paimon::frameperf::Scope perf(paimon::frameperf::Stage::Scheduler);
            CCScheduler::update(dt);
        }
        auto const elapsed = microsSince(start);

        if (!s_perfReady.load(std::memory_order_acquire)) return;
        if (elapsed >= 5'000'000) {
            windowStart = now();
            calls = 0;
            totalUs = maxUs = 0;
            return;
        }

        ++calls;
        totalUs += elapsed;
        if (elapsed > maxUs) maxUs = elapsed;

        auto const windowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now() - windowStart).count();
        if (windowMs >= 10'000) {
            log::info("[Perf] scheduler {}s: calls={} avg={}us max={}ms",
                windowMs / 1000, calls, calls ? totalUs / calls : 0, maxUs / 1000);
            windowStart = now();
            calls = 0;
            totalUs = maxUs = 0;
        }
    }
};

struct $modify(PaimonSavePerf, CCApplication) {
    void gameDidSave() {
        auto const start = now();
        CCApplication::gameDidSave();
        auto const saveMs = elapsedMs(start);
        if (s_perfReady.load(std::memory_order_acquire) && saveMs >= 50) {
            log::warn("[Perf] gameDidSave took {} ms", saveMs);
        }
    }
};
