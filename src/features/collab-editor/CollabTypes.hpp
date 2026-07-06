#pragma once

#include <matjson.hpp>
#include <cocos2d.h>
#include <cstdint>
#include <string>

namespace paimon::collab {

// Access gate code required to use the feature (also validated server-side).
constexpr char const* kAccessCode = "paimon5";

// Fixed Render host for the collab server. Shared by the room net client and
// the presence client (invites). Pasting it into settings tends to mangle it,
// so it lives here as the single source of truth.
constexpr char const* kServerBaseUrl = "https://collab-editor-server-2r4e.onrender.com";

// Protocol version, in case the wire format changes later.
// v2: chat + voice + attribution (origin/by in op_batch, peers in join_ok).
constexpr uint32_t kProtocolVersion = 2;

// How many ops to flush per editor tick at most (safety cap).
constexpr size_t kMaxOpsPerFlush = 2048;

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

// A connected editor in the room (from the server's peers list).
struct PeerInfo {
    int clientId = 0;
    std::string username;
    bool isHost = false;
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

    matjson::Value toJson() const {
        return matjson::makeObject({
            {"allowSong", allowSong},
            {"allowOptions", allowOptions},
            {"allowLevelSettings", allowLevelSettings},
        });
    }

    static HostPermissions fromJson(matjson::Value const& value) {
        HostPermissions out;
        if (value.isObject()) {
            out.allowSong = value.contains("allowSong") && value["allowSong"].asBool().unwrapOr(false);
            out.allowOptions = value.contains("allowOptions") && value["allowOptions"].asBool().unwrapOr(false);
            out.allowLevelSettings = value.contains("allowLevelSettings") && value["allowLevelSettings"].asBool().unwrapOr(false);
        }
        return out;
    }
};

} // namespace paimon::collab
