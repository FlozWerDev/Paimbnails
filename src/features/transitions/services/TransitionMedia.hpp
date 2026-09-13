#pragma once
#include <Geode/Geode.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace paimon::transitions {
struct TransitionMedia {
    std::string manifest;
    int width = 0, height = 0, columns = 0, perPage = 0;
    std::size_t bytes = 0;
    std::vector<int> endsMs;
    std::vector<geode::Ref<cocos2d::CCTexture2D>> pages;
    float duration() const { return endsMs.empty() ? 0.f : endsMs.back() / 1000.f; }
    void apply(cocos2d::CCSprite* sprite, double seconds) const;
};
using MediaCallback = std::function<void(std::shared_ptr<TransitionMedia>, std::string)>;
// Main-thread API. Import/decode/disk work is serialized on a worker. GPU
// uploads and callbacks run on the main thread, never inside a scene hook.
void prepareTransitionMedia(std::string const& path, MediaCallback callback = {});
std::shared_ptr<TransitionMedia> findTransitionMedia(std::string const& path);
} // namespace paimon::transitions
