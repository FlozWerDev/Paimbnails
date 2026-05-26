#pragma once

#include "PaiDrawModels.hpp"

namespace paidraw {

struct ConnectionEvent {
    bool connected = false;
    bool authenticated = false;
    bool offlinePreview = false;
    std::string message;
};

struct LobbyUpdatedEvent {
    int onlineCount = 0;
    std::vector<PlayerInfo> players;
    std::vector<RoomInfo> rooms;
};

struct RoomUpdatedEvent {
    RoomInfo room;
};

struct ChatUpdatedEvent {
    std::vector<ChatMessage> messages;
};

struct StrokeEvent {
    StrokeSegment stroke;
};

struct RoundUpdatedEvent {
    RoundState round;
};

struct ResultsUpdatedEvent {
    MatchResults results;
};

} // namespace paidraw
