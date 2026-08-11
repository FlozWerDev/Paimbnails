#include "TextureBudget.hpp"

#include <algorithm>
#include <deque>

using namespace cocos2d;

namespace paimon::image {

namespace {
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

    touch(absolutePath);
    evictDownTo(std::max<std::size_t>(budget, 1));
    return tex;
}

void dropBudgeted(std::string const& absolutePath) {
    if (absolutePath.empty()) return;
    auto& q = lru();
    auto it = std::find(q.begin(), q.end(), absolutePath);
    if (it != q.end()) q.erase(it);
    if (auto* cache = CCTextureCache::sharedTextureCache()) {
        cache->removeTextureForKey(absolutePath.c_str());
    }
}

void clearBudgeted() {
    auto& q = lru();
    if (auto* cache = CCTextureCache::sharedTextureCache()) {
        for (auto const& path : q) cache->removeTextureForKey(path.c_str());
    }
    q.clear();
}

} // namespace paimon::image
