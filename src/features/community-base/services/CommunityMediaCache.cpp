#include "CommunityMediaCache.hpp"
#include "CommunityClient.hpp"
#include "../../thumbnails/services/ThumbnailTransportClient.hpp"
#include "../../../core/RuntimeLifecycle.hpp"

using namespace geode::prelude;

namespace paimon::community {

CommunityMediaCache& CommunityMediaCache::get() {
    static CommunityMediaCache instance;
    return instance;
}

CCTexture2D* CommunityMediaCache::cached(std::string const& key) const {
    for (auto const& [entryKey, texture] : m_entries) {
        if (entryKey == key) return texture.data();
    }
    return nullptr;
}

void CommunityMediaCache::forget(std::string const& key) {
    m_entries.remove_if([&key](auto const& entry) { return entry.first == key; });
}

void CommunityMediaCache::finish(std::string const& key, std::vector<uint8_t> const& data, bool ok) {
    if (paimon::isRuntimeShuttingDown()) return;
    CCTexture2D* texture = ok ? ThumbnailTransportClient::bytesToTexture(data) : nullptr;
    if (texture) {
        forget(key);
        m_entries.emplace_front(key, texture);
        while (m_entries.size() > kMaxEntries) m_entries.pop_back();
    }
    auto node = m_pending.extract(key);
    if (node.empty()) return;
    for (auto& callback : node.mapped()) {
        if (callback) callback(texture);
    }
}

void CommunityMediaCache::load(std::string const& url, TextureCallback cb) {
    if (auto* texture = cached(url)) {
        if (cb) cb(texture);
        return;
    }
    auto& waiters = m_pending[url];
    bool const first = waiters.empty();
    waiters.push_back(std::move(cb));
    if (!first) return;
    CommunityClient::get().downloadPublic(url, [url](bool ok, std::vector<uint8_t> const& data) {
        CommunityMediaCache::get().finish(url, data, ok);
    });
}

void CommunityMediaCache::loadModeration(std::string const& mediaPath, TextureCallback cb) {
    if (auto* texture = cached(mediaPath)) {
        if (cb) cb(texture);
        return;
    }
    auto& waiters = m_pending[mediaPath];
    bool const first = waiters.empty();
    waiters.push_back(std::move(cb));
    if (!first) return;
    CommunityClient::get().moderationMedia(mediaPath, [mediaPath](bool ok, std::vector<uint8_t> const& data) {
        CommunityMediaCache::get().finish(mediaPath, data, ok);
    });
}

} // namespace paimon::community
