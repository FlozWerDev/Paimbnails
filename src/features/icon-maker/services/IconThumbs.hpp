#pragma once
// Real previews for the gallery. Rendering a project is the same slot compose
// the editor does, just small and off-thread; results stay in RAM keyed by
// project id and are dropped when the project changes or the GL context dies.
//
// Threading: request/invalidate/clear must be called from the main thread.

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

    // Calls back synchronously with the cached texture when there is one,
    // otherwise renders in the background and calls back later. The callback
    // is dropped if the project is invalidated meanwhile.
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
