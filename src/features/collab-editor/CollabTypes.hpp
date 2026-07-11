#pragma once

#include <matjson.hpp>
#include <cocos2d.h>
#include <cstdint>
#include <string>
#include <vector>

namespace paimon::collab {

// Fixed Render host for the collab server. Shared by the room net client and
// the presence client (invites). Pasting it into settings tends to mangle it,
// so it lives here as the single source of truth.
constexpr char const* kServerBaseUrl = "https://collab-editor-server-2r4e.onrender.com";

// Protocol version, in case the wire format changes later.
// v2: chat + voice + attribution (origin/by in op_batch, peers in join_ok).
// v3: acked op delivery (client outbox), server limits in join_ok, digest
//     verification messages for automatic desync repair.
// v4: typed move ops (cheap remote apply via setPosition), peer selection
//     presence (ephemeral select broadcast + overlay rects). Inspired by
//     alk.editor-collab's SelectCommand / MoveCommand model.
// v5: account + icon appearance in peer presence, so the room list can draw
//     the real GD icon and open the matching profile without guessing from a
//     configurable display name.
// v6: random per-session bearer tokens and protected host reconnection.
constexpr uint32_t kProtocolVersion = 6;

// How many ops the coalescing buffer holds before being force-moved into the
// outbox (safety cap for mass pastes).
constexpr size_t kMaxOpsPerFlush = 2048;

// Reliable op transport: ops leave the coalescing buffer into an ordered
// outbox and are sent ONE request at a time; each chunk stays in flight until
// the server acks it (HTTP 200), so nothing is ever silently dropped. These
// are the legacy-safe defaults; a v3 server advertises its real limits in
// join_ok ("limits") and the client adopts them.
constexpr size_t kDefaultOpsPerRequest = 500;
constexpr size_t kMaxSaveBytesPerRequest = 1'400'000; // stay well under the server body cap
constexpr float kDefaultOpsPerSecond = 500.f;         // legacy servers 429 above 600 ops/s

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

// How a connect attempt resolves against the server:
//  - Create: make a brand new room and host it (errors if the code is taken).
//  - Join:   join an existing room as a peer (errors if it doesn't exist).
enum class ConnectMode {
    Create,
    Join,
};

// Cosmetic/profile data announced with a peer's room presence. This is display
// data only; room permissions continue to be keyed exclusively by clientId.
// `hasIcon` is derived locally when parsing a peer list, never trusted from
// the wire, so older clients/servers gracefully fall back to a default icon.
struct PeerAppearance {
    int accountID = 0;
    int iconID = 0;
    int iconType = 0;
    int color1 = 0;
    int color2 = 0;
    int glowColor = 0;
    bool glowEnabled = false;
    bool hasIcon = false;
};

// A connected editor in the room (from the server's peers list).
struct PeerInfo {
    int clientId = 0;
    std::string username;
    bool isHost = false;
    PeerAppearance appearance;
};

// One chat line (also used for local system notices with from == 0).
struct ChatMessage {
    int from = 0;
    std::string name;
    std::string text;
};

// Deterministic bright color per client id, used for attribution tags and
// chat names so each editor is recognizable at a glance (Place-style).
inline cocos2d::ccColor3B peerColor(int clientId) {
    static constexpr cocos2d::ccColor3B kPalette[] = {
        {255, 120, 120}, {120, 220, 255}, {150, 255, 140}, {255, 210, 100},
        {220, 140, 255}, {255, 150, 220}, {140, 255, 220}, {255, 170, 120},
        {170, 190, 255}, {230, 255, 120},
    };
    if (clientId <= 0) return {255, 255, 255};
    return kPalette[static_cast<size_t>(clientId) % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

// Permissions the host grants to non-host editors in the room. Sent by the
// server in join_ok and perms messages.
struct HostPermissions {
    bool allowSong = false;
    bool allowOptions = false;
    bool allowLevelSettings = false;
    // When true, non-hosts become viewers (alk "view only mode"): they can
    // tinker locally but ops/selections that change the level are not shared,
    // and local divergences are wiped after playtesting.
    bool viewOnly = false;

    matjson::Value toJson() const {
        return matjson::makeObject({
            {"allowSong", allowSong},
            {"allowOptions", allowOptions},
            {"allowLevelSettings", allowLevelSettings},
            {"viewOnly", viewOnly},
        });
    }

    static HostPermissions fromJson(matjson::Value const& value) {
        HostPermissions out;
        if (value.isObject()) {
            out.allowSong = value.contains("allowSong") && value["allowSong"].asBool().unwrapOr(false);
            out.allowOptions = value.contains("allowOptions") && value["allowOptions"].asBool().unwrapOr(false);
            out.allowLevelSettings = value.contains("allowLevelSettings") && value["allowLevelSettings"].asBool().unwrapOr(false);
            out.viewOnly = value.contains("viewOnly") && value["viewOnly"].asBool().unwrapOr(false);
        }
        return out;
    }
};

// RAII guard for remote-apply reentrancy (alk's TrackerGuard pattern). Nested
// scopes nest safely; the flag is only cleared when the outermost guard dies.
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

// One peer's current selection, drawn as colored rects in the object layer.
// Ephemeral presence — not part of the level's LWW object state.
struct PeerSelection {
    int clientId = 0;
    std::string name;
    // World-space AABBs (object layer coordinates), one per selected object
    // (or a single union rect when the selection is huge).
    std::vector<cocos2d::CCRect> rects;
    float age = 0.f; // seconds since last update; overlay fades/clears old ones
};

} // namespace paimon::collab
