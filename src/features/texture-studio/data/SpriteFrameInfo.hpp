#pragma once
//
// SpriteFrameInfo.hpp - Plain Old Data describing one sprite frame inside a
// cocos2d-x .plist atlas. The fields mirror what cocos2d's CCSpriteFrameCache
// stores internally, plus the metadata we need to round-trip the .plist.
//
// We deliberately avoid Geode/Cocos types here so this header is cheap to
// include (no heavy template instantiations) and so the data layer can be
// unit-tested offline without bringing the engine in.
//

#include <cstdint>
#include <string>
#include <vector>

namespace paimon::texture_studio {

// One frame in the atlas. Coordinates follow the cocos2d convention used by
// PackGen / GD: Y axis grows downwards (top-left origin), measurements in
// the source sheet pixel space.
struct SpriteFrameInfo {
    // Name as it appears in the plist (e.g. "GJ_playBtn_001.png").
    std::string name;

    // Where in the atlas the sprite lives. If `rotated` is true these
    // coordinates correspond to a 90-degree-rotated rect inside the atlas
    // (cocos2d-x packs some sprites rotated to save space).
    int rectX = 0;
    int rectY = 0;
    int rectW = 0;
    int rectH = 0;

    // Logical (un-rotated) sprite dimensions — what the sprite "should look
    // like" when placed in the world. When `rotated` is false this matches
    // (rectW, rectH); when true it's swapped because rectW/H describe the
    // rotated footprint inside the atlas.
    int spriteW = 0;
    int spriteH = 0;

    // Offset of the cropped rect within the un-cropped source sprite. Zero
    // means the sprite was packed without trimming whitespace.
    float offsetX = 0.0f;
    float offsetY = 0.0f;

    // Original full sprite dimensions before whitespace trimming. Equal to
    // spriteW/H when no trimming was applied.
    int sourceW = 0;
    int sourceH = 0;

    // True when this entry was packed rotated 90deg counter-clockwise inside
    // the atlas. The reader has to undo that rotation when extracting pixels.
    bool rotated = false;

    // Aliases (alternate names that map to the same frame). Most GD sheets
    // don't use these, but the format supports them and we round-trip them.
    std::vector<std::string> aliases;
};

// Top-level metadata of a .plist (cocos2d-x format 3). We keep the raw values
// so PlistBuilder can write them back unchanged unless explicitly modified
// (e.g. when scaling for a Medium quality port).
struct PlistMetadata {
    // 0/1/2/3 — cocos2d format version. GD 2.2 ships format 3.
    int format = 3;

    // Total atlas size, e.g. {2048, 2048}.
    int sizeW = 0;
    int sizeH = 0;

    // Filenames recorded inside the plist. These are usually consistent with
    // the actual PNG filename but technically independent.
    std::string textureFileName;
    std::string realTextureFileName;

    // Premultiplied alpha flag (GD always uses true).
    bool premultiplyAlpha = true;

    // Smartupdate hash — cocos2d emits this so the engine can detect plist
    // changes. We don't validate it, just round-trip.
    std::string smartUpdate;
};

// Result of parsing a full .plist.
struct ParsedSpritesheet {
    PlistMetadata metadata;
    std::vector<SpriteFrameInfo> frames;
};

}  // namespace paimon::texture_studio
