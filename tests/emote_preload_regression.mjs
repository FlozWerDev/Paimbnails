import assert from "node:assert/strict";
import { readFileSync, mkdtempSync, rmSync } from "node:fs";
import { spawnSync } from "node:child_process";
import { tmpdir } from "node:os";
import { join } from "node:path";

const root = new URL("../", import.meta.url);
const read = (path) => readFileSync(new URL(path, root), "utf8").replace(/^\uFEFF/, "");
const service = read("src/features/emotes/services/EmoteService.cpp");
const cache = read("src/features/emotes/services/EmoteCache.cpp");
const preload = read("src/core/PreloadActions.cpp");

function functionSource(source, signature) {
    const start = source.indexOf(signature);
    assert.notEqual(start, -1, signature);
    const body = source.indexOf("{", start);
    let depth = 1;
    let end = body + 1;
    while (depth && end < source.length) {
        if (source[end] === "{") ++depth;
        if (source[end] === "}") --depth;
        ++end;
    }
    assert.equal(depth, 0, signature);
    return source.slice(start, end);
}

const header = read("src/features/emotes/services/EmoteService.hpp")
    .replace(/^#pragma once$/m, "")
    .replace(/^#include.*$/gm, "")
    .replace("private:", "public:");
const page = functionSource(service, "void EmoteService::fetchPage(");
assert.ok(page.indexOf("isRuntimeShuttingDown()") < page.indexOf("if (!res.ok())"));
assert.ok(page.indexOf("generation != m_catalogGeneration") < page.indexOf("if (!res.ok())"));
assert.match(page, /fetchPage\(page \+ 1, limit, "", std::move\(accumulator\), generation,/);

// compile the production functions, replacing only geode, transport and disk i/o.
const source = `
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace geode {
template<class T> using CopyableFunction = std::function<T>;
}
namespace paimon {
bool shuttingDown = false;
bool isRuntimeShuttingDown() { return shuttingDown; }
namespace preload {
std::atomic<bool> g_emotesCatalogReady{false};
}
}
namespace geode::log {
template<class... T> void info(T const&...) {}
template<class... T> void warn(T const&...) {}
}
using namespace geode;
struct Loader {
    std::deque<std::function<void()>> tasks;
    static Loader* get() { static Loader loader; return &loader; }
    void queueInMainThread(std::function<void()> task) { tasks.push_back(std::move(task)); }
    void drain() {
        int budget = 1000;
        while (!tasks.empty()) {
            assert(--budget > 0);
            auto task = std::move(tasks.front());
            tasks.pop_front();
            task();
        }
    }
};
${read("src/features/emotes/models/EmoteModels.hpp").replace("#pragma once", "")}
${header}
using namespace paimon::emotes;
struct PageRequest {
    std::shared_ptr<std::vector<EmoteInfo>> accumulator;
    EmoteService::CatalogCallback callback;
};
std::deque<PageRequest> pages;
int saves = 0;
void EmoteService::fetchPage(int, int, std::string const&,
    std::shared_ptr<std::vector<EmoteInfo>> accumulator, size_t, CatalogCallback callback) {
    pages.push_back({accumulator, std::move(callback)});
}
void EmoteService::buildIndex() { m_staticEmotes = m_allEmotes; }
void EmoteService::saveCatalogToDisk() { ++saves; }
void EmoteService::loadCatalogFromDisk() {}
static std::filesystem::path getCatalogPath() { return "absent-catalog.json"; }
${functionSource(service, "void EmoteService::dispatchCatalogCallbacks(")}
${functionSource(service, "void EmoteService::fetchAllEmotes(")}
${functionSource(service, "void EmoteService::clearCatalog(")}

struct HttpClient {
    using Callback = std::function<void(bool, std::vector<uint8_t> const&, int, int)>;
    std::deque<Callback> requests;
    bool immediateFailure = false;
    static HttpClient& get() { static HttpClient client; return client; }
    void downloadFromUrlRaw(std::string const&, Callback callback) {
        if (immediateFailure) callback(false, {}, 0, 0);
        else requests.push_back(std::move(callback));
    }
    void finish(bool success, bool empty = false) {
        assert(!requests.empty());
        auto callback = std::move(requests.front());
        requests.pop_front();
        callback(success, success && !empty ? std::vector<uint8_t>{1} : std::vector<uint8_t>{}, 0, 0);
        Loader::get()->drain();
    }
};
class EmoteCache {
public:
    using PreloadCallback = geode::CopyableFunction<void(size_t, size_t, size_t)>;
    using PreloadProgressCallback = geode::CopyableFunction<void(size_t, size_t)>;
    std::atomic<bool> m_preloading{false}, m_preloadCancel{false};
    struct PreloadListeners {
        std::vector<PreloadCallback> callbacks;
        std::vector<PreloadProgressCallback> progressCallbacks;
    };
    std::weak_ptr<PreloadListeners> m_preloadListeners;
    std::set<std::string> disk;
    bool isDiskEntryValid(std::string const& name) { return disk.contains(name); }
    void saveToDisk(std::string const& name, std::vector<uint8_t> const&) { disk.insert(name); }
    void preloadAllToDisk(PreloadCallback callback = nullptr, PreloadProgressCallback progressCallback = nullptr);
    void cancelPreload();
};
${functionSource(cache, "void dispatchPreloadCallback(")}
${functionSource(cache, "void EmoteCache::cancelPreload(")}
${functionSource(cache, "void EmoteCache::preloadAllToDisk(")}
int preloadStarts = 0;
void startEmotePreloadIfReady() { ++preloadStarts; }
${functionSource(preload, "void schedulePrefetchEmotes(")}

EmoteInfo emote(std::string name) {
    EmoteInfo info;
    info.name = info.filename = info.url = name;
    return info;
}
void finishPage(bool success, bool populated = true) {
    assert(!pages.empty());
    auto page = std::move(pages.front());
    pages.pop_front();
    if (populated) page.accumulator->push_back(emote("test"));
    page.callback(success);
}
int main() {
    auto& svc = EmoteService::get();
    auto mainThread = std::this_thread::get_id();
    std::vector<bool> results;
    auto result = [&](bool success) {
        assert(std::this_thread::get_id() == mainThread);
        results.push_back(success);
        svc.getAllEmotes(); // Callback must not hold the catalog mutex.
    };

    svc.fetchAllEmotes(result);
    std::thread concurrent([&] { svc.fetchAllEmotes(result); });
    concurrent.join();
    schedulePrefetchEmotes();
    assert(pages.size() == 1 && results.empty() && preloadStarts == 0);
    finishPage(true);
    assert(results.empty() && !svc.isFetching());
    Loader::get()->drain();
    assert((results == std::vector<bool>{true, true}) && preloadStarts == 1 && saves == 1);

    svc.clearCatalog();
    results.clear();
    svc.fetchAllEmotes(result);
    svc.fetchAllEmotes(result);
    schedulePrefetchEmotes();
    finishPage(false);
    Loader::get()->drain();
    assert((results == std::vector<bool>{false, false}));
    assert(paimon::preload::g_emotesCatalogReady && preloadStarts == 1 && pages.empty());

    results.clear();
    svc.fetchAllEmotes(result);
    svc.fetchAllEmotes(result);
    svc.clearCatalog();
    svc.fetchAllEmotes(result);
    finishPage(true); // Late response must not complete the new request.
    Loader::get()->drain();
    assert((results == std::vector<bool>{false, false}));
    assert(svc.isFetching() && !svc.isLoaded() && saves == 1);
    finishPage(true);
    Loader::get()->drain();
    assert(results.size() == 3 && results.back() && saves == 2);

    results.clear();
    svc.fetchAllEmotes(result);
    finishPage(true);
    svc.clearCatalog(); // Invalidate success already queued for delivery.
    Loader::get()->drain();
    assert((results == std::vector<bool>{false}));

    results.clear();
    svc.fetchAllEmotes([&](bool success) { assert(success); svc.fetchAllEmotes(result); });
    svc.fetchAllEmotes(result);
    finishPage(true, false);
    Loader::get()->drain();
    assert(results.size() == 1 && results.back() && pages.size() == 1);
    finishPage(false);
    Loader::get()->drain();
    assert(results.size() == 2 && !results.back());

    results.clear();
    svc.fetchAllEmotes([&](bool success) { assert(success); svc.clearCatalog(); });
    svc.fetchAllEmotes(result);
    finishPage(true);
    Loader::get()->drain();
    assert((results == std::vector<bool>{false}));

    results.clear();
    svc.fetchAllEmotes(result);
    finishPage(true);
    paimon::shuttingDown = true;
    Loader::get()->drain();
    svc.fetchAllEmotes(result);
    assert(results.empty() && pages.empty());
    paimon::shuttingDown = false;
    svc.fetchAllEmotes(result);
    paimon::shuttingDown = true;
    int before = saves;
    finishPage(true);
    Loader::get()->drain();
    assert(results.empty() && saves == before && svc.m_catalogCallbacks.expired());
    paimon::shuttingDown = false;
    svc.clearCatalog();

    svc.m_allEmotes = {emote("cached"), emote("ok"), emote("fail"), emote("empty")};
    EmoteCache cache;
    cache.disk.insert("cached");
    std::vector<size_t> progress;
    std::vector<std::tuple<size_t, size_t, size_t>> counts;
    auto onDone = [&](size_t d, size_t s, size_t t) { counts.emplace_back(d, s, t); };
    auto onProgress = [&](size_t done, size_t total) {
        assert(std::this_thread::get_id() == mainThread && total == 4 && done <= total);
        progress.push_back(done);
    };
    cache.preloadAllToDisk(onDone, onProgress);
    std::vector<size_t> joinedProgress;
    cache.preloadAllToDisk(onDone, [&](size_t done, size_t total) {
        assert(total == 4);
        joinedProgress.push_back(done);
    });
    auto& http = HttpClient::get();
    assert(http.requests.size() == 1);
    http.finish(true);
    http.finish(false);
    http.finish(true, true);
    assert(!cache.m_preloading && http.requests.empty());
    assert(progress.front() == 0 && progress.back() == 4);
    assert(std::is_sorted(progress.begin(), progress.end()));
    assert(counts.size() == 2 && counts.front() == counts.back());
    assert(counts.back() == std::make_tuple(1u, 1u, 4u));
    assert(joinedProgress.back() == 4 && std::is_sorted(joinedProgress.begin(), joinedProgress.end()));

    for (auto const& info : svc.m_allEmotes) cache.disk.insert(info.filename);
    progress.clear();
    counts.clear();
    cache.preloadAllToDisk(onDone, onProgress);
    Loader::get()->drain();
    assert(progress.back() == 4 && http.requests.empty());
    assert(counts.size() == 1 && counts.back() == std::make_tuple(0u, 4u, 4u));

    cache.disk.clear();
    counts.clear();
    progress.clear();
    cache.preloadAllToDisk(); // Picker starts first without listeners.
    http.finish(false);
    cache.preloadAllToDisk(onDone, onProgress);
    assert(http.requests.size() == 1);
    http.finish(false);
    http.finish(false);
    http.finish(false);
    assert(progress.back() == 4);
    assert(counts.size() == 1 && counts.back() == std::make_tuple(0u, 0u, 4u));

    cache.disk.clear();
    progress.clear();
    counts.clear();
    http.immediateFailure = true;
    cache.preloadAllToDisk(onDone, onProgress);
    Loader::get()->drain();
    assert(!cache.m_preloading && progress.back() == 4);
    assert(counts.size() == 1 && counts.back() == std::make_tuple(0u, 0u, 4u));
    http.immediateFailure = false;

    progress.clear();
    counts.clear();
    cache.preloadAllToDisk(onDone, onProgress);
    cache.cancelPreload();
    http.finish(true);
    assert(!cache.m_preloading && cache.disk.empty() && progress.back() == 0);
    assert(counts.size() == 1 && counts.back() == std::make_tuple(0u, 0u, 4u));

    counts.clear();
    svc.m_allEmotes.clear();
    cache.preloadAllToDisk(onDone);
    Loader::get()->drain();
    assert(counts.size() == 1 && counts.back() == std::make_tuple(0u, 0u, 0u));

    svc.m_allEmotes = {emote("shutdown")};
    counts.clear();
    cache.preloadAllToDisk(onDone);
    paimon::shuttingDown = true;
    http.finish(true);
    cache.preloadAllToDisk(onDone);
    assert(counts.empty() && cache.disk.empty() && http.requests.empty());
    std::cout << "emote preload regression tests passed\\n";
}
`;

const temp = mkdtempSync(join(tmpdir(), "emote-preload-"));
try {
    const compiler = process.env.CXX || "g++";
    const compile = spawnSync(compiler, ["-std=c++20", "-pthread", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-g", "-x", "c++", "-", "-o", `${temp}/regression`],
        { input: source, encoding: "utf8" });
    assert.equal(compile.status, 0, compile.error?.message || compile.stderr);
    const run = spawnSync(`${temp}/regression`, [], { cwd: temp, encoding: "utf8", timeout: 15000 });
    assert.equal(run.status, 0, run.error?.message || run.stderr);
    process.stdout.write(run.stdout);
} finally {
    rmSync(temp, { recursive: true, force: true });
}
