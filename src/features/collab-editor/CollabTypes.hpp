#pragma once

#include <matjson.hpp>
#include <cocos2d.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace paimon::collab {

// Fixed collab host (room net + presence/invites). Single source of truth —
// pasting into settings tends to mangle the URL.
// Prefer HTTPS in production; plain HTTP supports direct server ports.
constexpr char const* kServerBaseUrl = "http://node.akiomae.xyz:4401";

// v8: full editor meta sync — LevelSettings save string, GJEffectManager color
//     save string, and song (audioTrack / songID / songIDs / sfxIDs).
// v9: spatial presence — camera trails/ghosts, work zones, pings, follow peer.
constexpr uint32_t kProtocolVersion = 10;

constexpr size_t kMaxCursorAssetBytes = 128 * 1024;
constexpr size_t kMaxCursorDataLength = ((kMaxCursorAssetBytes + 2) / 3) * 4;

constexpr size_t kMaxOpsPerFlush = 2048;

// Ordered, acknowledged chunks avoid silently dropping edits; v3 servers may
// advertise tighter limits in join_ok.
constexpr size_t kDefaultOpsPerRequest = 500;
constexpr size_t kMaxSaveBytesPerRequest = 1'400'000;
constexpr float kDefaultOpsPerSecond = 500.f;

// FNV-1a in two independent 32-bit lanes over "gid|version|save", packed as
// (lane1 << 32) | lane2. The server computes the identical hash per object and
// broadcasts the XOR-aggregate of the whole room every few seconds; comparing
// it against the local aggregate detects any divergence, which is then healed
// with an automatic resync. Must match objectSyncHash() in server.js exactly.
inline uint32_t fnv1a32(std::string const& s, uint32_t seed) {
    uint32_t h = seed;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

inline uint64_t objectSyncHash(std::string const& gid, uint32_t version, std::string const& save) {
    std::string input;
    input.reserve(gid.size() + save.size() + 16);
    input += gid;
    input += '|';
    input += std::to_string(version);
    input += '|';
    input += save;
    return (static_cast<uint64_t>(fnv1a32(input, 0x811c9dc5u)) << 32) | fnv1a32(input, 0xcbf29ce4u);
}

enum class ConnState {
    Disconnected,
    Connecting,
    Connected,
};

enum class ConnectMode {
    Create,
    Join,
};

// Cosmetic peer data only; permissions remain keyed by clientId.
struct PeerAppearance {
    int accountID = 0;
    int iconID = 0;
    int iconType = 0;
    int color1 = 0;
    int color2 = 0;
    int glowColor = 0;
    bool glowEnabled = false;
    bool hasIcon = false;
    std::string cursorData;
    float cursorScale = 0.3f;
    int cursorOpacity = 255;
    bool hasCustomCursor = false;
};

struct PeerInfo {
    int clientId = 0;
    std::string username;
    bool isHost = false;
    PeerAppearance appearance;
};

struct ChatMessage {
    int from = 0;
    std::string name;
    std::string text;
};

inline cocos2d::ccColor3B peerColor(int clientId) {
    static constexpr cocos2d::ccColor3B kPalette[] = {
        {255, 120, 120}, {120, 220, 255}, {150, 255, 140}, {255, 210, 100},
        {220, 140, 255}, {255, 150, 220}, {140, 255, 220}, {255, 170, 120},
        {170, 190, 255}, {230, 255, 120},
    };
    if (clientId <= 0) return {255, 255, 255};
    return kPalette[static_cast<size_t>(clientId) % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

struct HostPermissions {
    bool allowSong = false;
    bool allowOptions = false;
    bool allowLevelSettings = false;
    bool allowColors = false;
    bool viewOnly = false;
    bool strictLayers = false;

    matjson::Value toJson() const {
        return matjson::makeObject({
            {"allowSong", allowSong},
            {"allowOptions", allowOptions},
            {"allowLevelSettings", allowLevelSettings},
            {"allowColors", allowColors},
            {"viewOnly", viewOnly},
            {"strictLayers", strictLayers},
        });
    }

    static HostPermissions fromJson(matjson::Value const& value) {
        HostPermissions out;
        if (value.isObject()) {
            out.allowSong = value.contains("allowSong") && value["allowSong"].asBool().unwrapOr(false);
            out.allowOptions = value.contains("allowOptions") && value["allowOptions"].asBool().unwrapOr(false);
            out.allowLevelSettings = value.contains("allowLevelSettings") && value["allowLevelSettings"].asBool().unwrapOr(false);
            if (value.contains("allowColors")) {
                out.allowColors = value["allowColors"].asBool().unwrapOr(false);
            } else {
                out.allowColors = out.allowLevelSettings;
            }
            out.viewOnly = value.contains("viewOnly") && value["viewOnly"].asBool().unwrapOr(false);
            out.strictLayers = value.contains("strictLayers") && value["strictLayers"].asBool().unwrapOr(false);
        }
        return out;
    }
};

// RAII guard for nested remote-apply scopes.
class TrackerGuard {
public:
    explicit TrackerGuard(bool& flag) : m_flag(flag), m_prev(flag) {
        m_flag = true;
    }
    ~TrackerGuard() { m_flag = m_prev; }
    TrackerGuard(TrackerGuard const&) = delete;
    TrackerGuard& operator=(TrackerGuard const&) = delete;

private:
    bool& m_flag;
    bool m_prev;
};

// Ephemeral selection presence, separate from LWW object state.
struct PeerSelection {
    int clientId = 0;
    std::string name;
    std::vector<cocos2d::CCRect> rects;
    float age = 0.f;
};

struct PeerCamera {
    int clientId = 0;
    std::string name;
    float x = 0.f;
    float y = 0.f;
    float zoom = 1.f;
    float cursorX = 0.f;
    float cursorY = 0.f;
    bool cursorVisible = true;
    float age = 0.f;
};

struct PeerWorkZone {
    int clientId = 0;
    std::string name;
    float x = 0.f;
    float y = 0.f;
    float w = 0.f;
    float h = 0.f;
    float age = 0.f;
};

struct PeerPing {
    int clientId = 0;
    std::string name;
    float x = 0.f;
    float y = 0.f;
    float age = 0.f;
    float life = 2.8f;
};

enum class LocalEditKind : uint8_t {
    Full = 0,
    Move = 1,
    Rotate = 2,
    Scale = 3,
    Flip = 4,
};

}
