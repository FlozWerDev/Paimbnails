#pragma once

#include <Geode/Geode.hpp>

#include <functional>
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
    std::map<std::string, std::vector<ReadyCallback>> m_pending;
};

}  // namespace paimon::icon_maker
