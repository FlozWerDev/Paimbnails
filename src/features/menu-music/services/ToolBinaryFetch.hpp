#pragma once
#include "../../../utils/FilesystemPath.hpp"

// shared download front-half for the yt-dlp / ffmpeg bootstraps. the
// exists-guard, unsupported-platform guard, in-progress guard, the
// progress-reporting GET, and the HTTP/size sanity checks are identical;
// only the install step (direct write vs zip extract) differs per tool and
// stays in the caller.

#include "../../../core/RuntimeLifecycle.hpp"
#include "../../../utils/WebHelper.hpp"

#include <Geode/loader/Loader.hpp>
#include <Geode/utils/file.hpp>
#include <Geode/utils/string.hpp>
#include <Geode/utils/web.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fmt/format.h>
#include <functional>
#include <memory>
#include <string>

namespace paimon::menumusic::toolfetch {

// downloaded tools land without the exec bit on posix.
inline void makeExecutable(std::filesystem::path const& path) {
#ifndef GEODE_IS_WINDOWS
    std::error_code permEc;
    std::filesystem::permissions(path,
        std::filesystem::perms::owner_exec |
        std::filesystem::perms::group_exec |
        std::filesystem::perms::others_exec,
        std::filesystem::perm_options::add, permEc);
#else
    (void)path;
#endif
}

struct FetchConfig {
    // empty url means unsupported on this platform.
    std::string url;
    std::string unsupportedMessage;
    std::string userAgent = "Paimbnails-MenuMusic/1.0";
    std::chrono::minutes timeout = std::chrono::minutes(5);
    std::string resolvingMessage = "Contacting server...";
    // "Downloading yt-dlp... " / "Downloading ffmpeg... " (totals appended).
    std::string downloadingVerb = "Downloading... ";
    // progress tops out here; the install step fills the rest.
    float downloadCap = 0.95f;
    std::size_t minBytes = 1'000'000;
    // per-tool error text; code/size known only at failure time.
    std::function<std::string(int httpCode)> httpErrorFor;
    std::function<std::string(std::size_t bytes)> sizeErrorFor;
};

// progress is the tool's own progress struct ({stage, percent, message});
// ProgressCb/CompleteCb are its std::function callback types. onData runs
// the install step with the downloaded bytes; like the guards, it executes
// on the main thread (WebHelper posts dispatch callbacks there).
template <typename Progress, typename ProgressCb, typename CompleteCb, typename OnData>
void fetchToolBinary(
    FetchConfig config,
    std::filesystem::path const& installedPath,
    bool installed,
    std::filesystem::path const& stageDir,
    std::atomic<bool>& downloading,
    ProgressCb onProgress,
    CompleteCb onComplete,
    OnData onData
) {
    if (installed) {
        if (onComplete) {
            auto path = paimon::paths::toUtf8(installedPath);
            geode::Loader::get()->queueInMainThread([onComplete, path]() {
                if (paimon::isRuntimeShuttingDown()) return;
                if (onComplete) onComplete(true, path);
            });
        }
        return;
    }

    if (config.url.empty()) {
        if (onComplete) {
            geode::Loader::get()->queueInMainThread(
                [onComplete, msg = config.unsupportedMessage]() {
                    if (paimon::isRuntimeShuttingDown()) return;
                    if (onComplete) onComplete(false, msg);
                });
        }
        return;
    }

    bool expected = false;
    if (!downloading.compare_exchange_strong(expected, true)) {
        if (onComplete) {
            geode::Loader::get()->queueInMainThread([onComplete]() {
                if (paimon::isRuntimeShuttingDown()) return;
                if (onComplete) onComplete(false, "A download is already in progress.");
            });
        }
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(stageDir, ec);

    if (onProgress) {
        geode::Loader::get()->queueInMainThread(
            [onProgress, msg = config.resolvingMessage]() {
                if (paimon::isRuntimeShuttingDown()) return;
                onProgress({ "resolving", 0.f, msg });
            });
    }

    auto progressShared = std::make_shared<ProgressCb>(std::move(onProgress));
    auto completeShared = std::make_shared<CompleteCb>(std::move(onComplete));

    auto req = geode::utils::web::WebRequest()
        .timeout(asp::Duration::fromSecs(
            std::chrono::duration_cast<std::chrono::seconds>(config.timeout).count()))
        .userAgent(config.userAgent);

    // geode despacha onprogress en un worker thread, asi que re-encolamos
    // en main thread antes de tocar ui.
    req.onProgress([progressShared, verb = config.downloadingVerb, cap = config.downloadCap](
            geode::utils::web::WebProgress const& p) {
        if (!progressShared || !*progressShared) return;
        auto downloaded = static_cast<uint64_t>(p.downloaded());
        auto total = static_cast<uint64_t>(p.downloadTotal());
        geode::Loader::get()->queueInMainThread([progressShared, downloaded, total, verb, cap]() {
            if (paimon::isRuntimeShuttingDown()) return;
            if (!progressShared || !*progressShared) return;
            float pct = (total > 0)
                ? std::min(cap, static_cast<float>(downloaded) / static_cast<float>(total) * cap)
                : 0.f;
            std::string msg = (total > 0)
                ? fmt::format("{}{:.2f} / {:.2f} MB", verb,
                    downloaded / 1'048'576.0, total / 1'048'576.0)
                : fmt::format("{}{:.2f} MB", verb, downloaded / 1'048'576.0);
            (*progressShared)(Progress{ "downloading", pct, std::move(msg) });
        });
    });

    // dispatch takes the URL by reference; moving config into the callback
    // would otherwise empty config.url before the request can read it.
    auto const downloadUrl = config.url;
    WebHelper::dispatch(std::move(req), "GET", downloadUrl,
        [&downloading, config = std::move(config), progressShared, completeShared,
         onData = std::move(onData)](geode::utils::web::WebResponse res) {
            downloading = false;

            auto fail = [completeShared](std::string err) {
                if (completeShared && *completeShared) (*completeShared)(false, std::move(err));
            };

            if (!res.ok()) {
                fail(config.httpErrorFor ? config.httpErrorFor(res.code()) : "Download failed.");
                return;
            }

            auto data = res.data();
            if (data.size() < config.minBytes) {
                fail(config.sizeErrorFor ? config.sizeErrorFor(data.size()) : "Download too small.");
                return;
            }

            // onData is the per-tool install step (deduced callable); always
            // provided by the caller, so invoke directly.
            onData(std::move(data), progressShared, completeShared);
        });
}

} // namespace paimon::menumusic::toolfetch
