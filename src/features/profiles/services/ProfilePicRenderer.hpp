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

// resolved source for the profile photo, shared by editor, menulayer button,
// and config-layer preview so all three render exactly the same image.
struct ResolvedProfilePhoto {
    enum class Kind {
        None,        // nothing available
        StaticFile,  // path -> static image on disk
        GifFile,     // path -> animated gif on disk
        GifCacheKey, // gifkey -> animatedgifsprite ram cache
        Texture      // texture -> profileimg ram/disk cache
    };
    // for status/debug display
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

// builds the image node for a resolved photo synchronously (static files are
// decoded on the calling thread). returns nullptr for kind::none or on failure.
cocos2d::CCNode* createResolvedPhotoNode(ResolvedProfilePhoto const& photo);

}
