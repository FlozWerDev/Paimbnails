#pragma once

// small in-memory texture cache for paimon base images (comment backgrounds,
// preset previews). comment lists ask for the same background once per cell,
// so requests for one url are merged and the texture is kept for reuse.

#include <Geode/Geode.hpp>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

namespace paimon::community {

class CommunityMediaCache {
public:
    using TextureCallback = geode::CopyableFunction<void(cocos2d::CCTexture2D* texture)>;

    static CommunityMediaCache& get();

    // public url served by the worker; the callback runs on the main thread,
    // with nullptr when the image is missing or could not be decoded.
    void load(std::string const& url, TextureCallback cb);
    // moderation media (needs the mod code); cached under the same rules.
    void loadModeration(std::string const& mediaPath, TextureCallback cb);
    cocos2d::CCTexture2D* cached(std::string const& key) const;
    void forget(std::string const& key);

private:
    CommunityMediaCache() = default;
    void finish(std::string const& key, std::vector<uint8_t> const& data, bool ok);

    static constexpr size_t kMaxEntries = 48;
    std::list<std::pair<std::string, geode::Ref<cocos2d::CCTexture2D>>> m_entries;
    std::unordered_map<std::string, std::vector<TextureCallback>> m_pending;
};

} // namespace paimon::community
