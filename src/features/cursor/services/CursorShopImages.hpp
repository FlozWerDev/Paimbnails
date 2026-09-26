#pragma once
// Queued remote thumbs with a concurrency cap: 40 at once left cells blank.

#include <Geode/Geode.hpp>

#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace paimon::cursorshop {

class ShopImages final {
public:
    using Callback = geode::CopyableFunction<void(cocos2d::CCTexture2D*)>;

    static ShopImages& get();

    // Cached texture, else queued download; cb gets nullptr on failure.
    cocos2d::CCTexture2D* fetch(std::string const& url, Callback cb);

    void clear();
    void forgetFailures();

private:
    ShopImages() = default;

    void pump();
    void finish(std::string const& url, cocos2d::CCTexture2D* texture);

    std::unordered_map<std::string, geode::Ref<cocos2d::CCTexture2D>> m_cache;
    std::unordered_map<std::string, std::vector<Callback>> m_pending;
    std::unordered_map<std::string, int> m_attempts;
    std::deque<std::string> m_queue;
    std::unordered_set<std::string> m_failed;
    int m_active = 0;
};

// Mounts the thumb on `holder` when it arrives; placeholder until then, "?" on failure.
void mountThumb(cocos2d::CCNode* holder, std::string const& url,
                float maxWidth, float maxHeight);

} // namespace paimon::cursorshop
