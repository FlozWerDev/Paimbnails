#pragma once
#include <Geode/Geode.hpp>
#include <string>

struct ProfilePicConfig;

namespace paimon::profile_pic {

cocos2d::CCNode* composeProfilePicture(
    cocos2d::CCNode* imageNode,
    float targetSize,
    ProfilePicConfig const& config
);

// Resolved source for the profile photo. Single source of truth shared by the
// editor preview, the MenuLayer profile button, and the config-layer preview,
// so what the editor shows is exactly what gets rendered in-game.
struct ResolvedProfilePhoto {
    enum class Kind {
        None,        // nothing available
        StaticFile,  // path -> static image on disk
        GifFile,     // path -> animated GIF on disk
        GifCacheKey, // gifKey -> AnimatedGIFSprite RAM cache
        Texture      // texture -> profileimg RAM/disk cache
    };
    // where the image came from, for status/debug display
    enum class Source { None, Custom, OwnProfile, LegacyBackground };
    Kind kind = Kind::None;
    Source source = Source::None;
    std::string path;
    std::string gifKey;
    cocos2d::CCTexture2D* texture = nullptr;
    // true when the "profile" source has no local data yet (callers may
    // trigger a profileimg download and rebuild afterwards)
    bool ownPhotoMissing = false;
};

ResolvedProfilePhoto resolveProfilePhoto(ProfilePicConfig const& config);

// Builds the image node for a resolved photo synchronously (static files are
// decoded on the calling thread). Returns nullptr for Kind::None or on failure.
cocos2d::CCNode* createResolvedPhotoNode(ResolvedProfilePhoto const& photo);

}
