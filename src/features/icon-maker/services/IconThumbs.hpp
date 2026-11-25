#pragma once

#include <Geode/Geode.hpp>

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <string>
#include <vector>

namespace paimon::icon_maker {

class IconThumbs final {
public:
    using ReadyCallback = std::function<void(cocos2d::CCTexture2D*)>;

    static IconThumbs& get();

    // cache hits call back immediately; misses render in the background.
    // invalidation drops pending callbacks.
    void request(std::string const& projectId, ReadyCallback onReady);

    void invalidate(std::string const& projectId);

    void clear();

    void onGLContextReload() { clear(); }

private:
    IconThumbs() = default;
    ~IconThumbs() = default;
    IconThumbs(IconThumbs const&) = delete;
    IconThumbs& operator=(IconThumbs const&) = delete;

    std::map<std::string, geode::Ref<cocos2d::CCTexture2D>> m_cache;
    struct PendingRender {
        std::uint64_t token;
        std::vector<ReadyCallback> callbacks;
    };
    void touch(std::string const& id);
    void cacheTexture(std::string const& id, cocos2d::CCTexture2D* texture);
#if defined(GEODE_IS_ANDROID) || defined(GEODE_IS_IOS)
    static constexpr std::size_t kMaxCachedThumbs = 64;
#else
    static constexpr std::size_t kMaxCachedThumbs = 128;
#endif
    std::uint64_t m_nextToken = 0;
    std::list<std::string> m_cacheOrder;
    std::map<std::string, PendingRender> m_pending;
};

}  // namespace paimon::icon_maker
