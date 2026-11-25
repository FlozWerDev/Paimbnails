#include "TextureBudget.hpp"
#include "RetainedLazyTextureLoad.hpp"
#include "LocalAssetStore.hpp"
#include "../core/RuntimeLifecycle.hpp"

#include <algorithm>
#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

using namespace cocos2d;

namespace paimon::image {

namespace {
struct PendingTexture {
    RetainedLazyTextureLoad loader;
    std::vector<std::function<void(CCTexture2D*)>> callbacks;
};

auto& pendingTextures() {
    static auto* pending = new std::unordered_map<std::string, std::shared_ptr<PendingTexture>>();
    return *pending;
}

std::deque<std::string>& lru() {
    static auto* paths = new std::deque<std::string>();
    return *paths;
}

void touch(std::string const& path) {
    auto& q = lru();
    auto it = std::find(q.begin(), q.end(), path);
    if (it != q.end()) q.erase(it);
    q.push_back(path);
}

void evictDownTo(std::size_t budget) {
    auto& q = lru();
    auto* cache = CCTextureCache::sharedTextureCache();
    if (!cache) return;
    while (q.size() > budget) {
        cache->removeTextureForKey(q.front().c_str());
        q.pop_front();
    }
}
} // namespace

CCTexture2D* loadBudgeted(std::string const& absolutePath, std::size_t budget) {
    if (absolutePath.empty()) return nullptr;
    auto* cache = CCTextureCache::sharedTextureCache();
    if (!cache) return nullptr;

    auto* tex = cache->addImage(absolutePath.c_str(), false);
    if (!tex) return nullptr;

    touchBudgeted(absolutePath, budget);
    return tex;
}

void loadBudgetedAsync(std::string const& absolutePath, std::function<void(CCTexture2D*)> callback) {
    if (!callback) return;
    if (absolutePath.empty() || paimon::isRuntimeShuttingDown()) { callback(nullptr); return; }
    auto* cache = CCTextureCache::sharedTextureCache();
    if (auto* texture = cache ? cache->textureForKey(absolutePath.c_str()) : nullptr) {
        touchBudgeted(absolutePath);
        callback(texture);
        return;
    }
    auto& requests = pendingTextures();
    if (auto const found = requests.find(absolutePath); found != requests.end()) {
        found->second->callbacks.push_back(std::move(callback));
        return;
    }

    auto request = std::make_shared<PendingTexture>();
    request->callbacks.push_back(std::move(callback));
    requests.emplace(absolutePath, request);
    request->loader.loadFromFile(paimon::assets::pathFromUtf8(absolutePath),
        [path = absolutePath, weak = std::weak_ptr<PendingTexture>(request)](CCTexture2D* texture, bool) {
            auto request = weak.lock();
            if (!request) return;
            auto& pending = pendingTextures();
            auto const found = pending.find(path);
            if (found == pending.end() || found->second != request) return;
            auto callbacks = std::move(request->callbacks);
            pending.erase(found);
            // callbacks may reenter the cache and evict this key.
            geode::Ref<CCTexture2D> retained = texture;
            if (texture) touchBudgeted(path);
            for (auto& ready : callbacks) if (ready) ready(retained.data());
        });
}

void touchBudgeted(std::string const& absolutePath, std::size_t budget) {
    if (absolutePath.empty()) return;
    touch(absolutePath);
    evictDownTo(std::max<std::size_t>(budget, 1));
}

void dropBudgeted(std::string const& absolutePath) {
    if (absolutePath.empty()) return;
    auto& q = lru();
    auto it = std::find(q.begin(), q.end(), absolutePath);
    if (it != q.end()) q.erase(it);
    if (auto* cache = CCTextureCache::sharedTextureCache()) {
        cache->removeTextureForKey(absolutePath.c_str());
    }
    auto& pending = pendingTextures();
    if (auto const found = pending.find(absolutePath); found != pending.end()) {
        auto request = std::move(found->second);
        pending.erase(found);
        request->loader.reset();
        for (auto& callback : request->callbacks) if (callback) callback(nullptr);
    }
}

void clearBudgeted() {
    auto pending = std::move(pendingTextures());
    pendingTextures().clear();
    bool const shuttingDown = paimon::isRuntimeShuttingDown();
    for (auto& [path, request] : pending) {
        request->loader.reset();
        if (!shuttingDown) {
            for (auto& callback : request->callbacks) if (callback) callback(nullptr);
        }
    }
    auto& q = lru();
    if (auto* cache = CCTextureCache::sharedTextureCache()) {
        for (auto const& path : q) cache->removeTextureForKey(path.c_str());
    }
    q.clear();
}

} // namespace paimon::image
