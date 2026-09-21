#pragma once

// Reading a whole level string instead of an editor selection.
//
// Offline analyzer: everything comes out of the save string, no Geode so tests run it standalone.

#include <string>
#include <vector>

namespace paimon::autobuild {

// GD writes the Z layer as an odd number around 0 (B4 is the furthest back,
// T3 the closest to the camera). Default means "whatever the object type
// normally uses", which is why it is not simply 0 < x.
constexpr int kZLayerB4 = -3;
constexpr int kZLayerB3 = -1;
constexpr int kZLayerB2 = 1;
constexpr int kZLayerB1 = 3;
constexpr int kZLayerDefault = 0;
constexpr int kZLayerT1 = 5;
constexpr int kZLayerT2 = 7;
constexpr int kZLayerT3 = 9;

// Built-in channels. Anything below 1000 is a user channel.
constexpr int kChannelBG    = 1000;
constexpr int kChannelG1    = 1001;
constexpr int kChannelLine  = 1002;
constexpr int kChannel3DL   = 1003;
constexpr int kChannelObj   = 1004;
constexpr int kChannelP1    = 1005;
constexpr int kChannelP2    = 1006;
constexpr int kChannelLBG   = 1007;
constexpr int kChannelG2    = 1009;
constexpr int kChannelBlack = 1010;
constexpr int kChannelWhite = 1011;
constexpr int kChannelLight = 1012;

struct LevelObject {
    int id = 0;
    float x = 0.f;
    float y = 0.f;
    float rotation = 0.f;
    float scaleX = 1.f;
    float scaleY = 1.f;
    int mainColor = 0;    // key 21, 0 when the object never overrode it
    int detailColor = 0;  // key 22
    int zLayer = kZLayerDefault;
    int zOrder = 0;
    int editorLayer = 0;
    int groupCount = 0;
    bool flipX = false;
    bool flipY = false;
    bool highDetail = false;  // key 103: the author already marked it as LDM deco
    std::string save;
};

struct ColorChannel {
    int id = 0;
    unsigned char r = 255;
    unsigned char g = 255;
    unsigned char b = 255;
    float opacity = 1.f;
    bool blending = false;
    int copyId = 0;  // channel this one copies from, 0 = none
};

struct LevelData {
    std::string settings;              // the kA header chunk, without objects
    std::string colors;                // kS38 body, ready to hand to a Template
    std::vector<LevelObject> objects;
    bool truncated = false;            // hit the object budget while parsing
};

// `text` is the decompressed level string: "kA...;1,1,2,15,3,15;1,2,...;".
LevelData parseLevelString(std::string const& text, int maxObjects = 400000);

// kS38 body ("1_0_2_102_..._|1_1_..."), one entry per channel.
std::vector<ColorChannel> parseColorChannels(std::string const& colors);

// Value of one key in an object save string, without building a map.
bool objectKey(std::string const& save, int key, std::string& out);

} // namespace paimon::autobuild
