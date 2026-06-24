#pragma once

#include <Geode/Geode.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace paidraw {

constexpr float kCanvasWidth = 512.f;
constexpr float kCanvasHeight = 384.f;
constexpr int kMaxRoomPlayers = 25;
constexpr int kMaxChatMessages = 50;

enum class PacketType : uint8_t {
    Authenticate = 1,
    AuthAccepted = 2,
    AuthRejected = 3,
    RoomsListRequest = 10,
    RoomsListSnapshot = 11,
    CreateRoomRequest = 12,
    JoinRoomRequest = 13,
    LeaveRoomRequest = 14,
    RoomStateSnapshot = 15,
    ToggleReadyRequest = 16,
    StartGameRequest = 17,
    UpdateRoomConfigRequest = 18,
    ChatMessageSend = 19,
    ChatMessageReceive = 20,
    DrawStroke = 30,
    GuessRequest = 31,
    GuessFeedback = 32,
    RoundSync = 33,
    ClearCanvas = 34,
    ResultsSnapshot = 40,
    PresenceUpdate = 50,
    Ping = 60,
    Pong = 61,
    Error = 255,
};

enum class PlayerStatus {
    Free,
    InRoom,
    Playing,
};

enum class RoomState {
    Waiting,
    InGame,
};

enum class GameMode {
    Classic,
    Animation,
    Chain,
};

enum class WordLanguage {
    Spanish,
    English,
    Both,
};

enum class WordDifficulty {
    Easy,
    Medium,
    Hard,
};

struct PaiDrawPacket {
    PacketType type = PacketType::Error;
    uint32_t roomId = 0;
    uint32_t senderId = 0;
    uint64_t timestamp = 0;
    geode::ByteVector payload;
};

struct WordEntry {
    std::string word;
    WordDifficulty difficulty = WordDifficulty::Easy;
    std::string category;
};

struct PlayerInfo {
    uint32_t accountID = 0;
    std::string name;
    int level = 0;
    int iconID = 1;
    int iconType = 0;
    int color1 = 1;
    int color2 = 2;
    bool glow = false;
    PlayerStatus status = PlayerStatus::Free;
    bool ready = false;
    bool host = false;
    bool guessed = false;
    int pingMs = 0;
    int score = 0;
};

struct RoomConfig {
    std::string name;
    int maxPlayers = 8;
    int rounds = 6;
    int roundTimeSeconds = 90;
    GameMode mode = GameMode::Classic;
    std::string password;
    WordLanguage language = WordLanguage::Spanish;
};

struct RoomInfo {
    uint32_t id = 0;
    RoomConfig config;
    uint32_t hostId = 0;
    std::string hostName;
    bool hasPassword = false;
    RoomState state = RoomState::Waiting;
    std::vector<PlayerInfo> players;

    int playerCount() const {
        return static_cast<int>(players.size());
    }
};

struct ChatMessage {
    uint32_t senderId = 0;
    std::string senderName;
    std::string text;
    uint64_t timestamp = 0;
    bool system = false;
    bool correct = false;
    bool nearGuess = false;
};

struct StrokeSegment {
    float x1 = 0.f;
    float y1 = 0.f;
    float x2 = 0.f;
    float y2 = 0.f;
    cocos2d::ccColor3B color = {255, 255, 255};
    float size = 8.f;
    bool eraser = false;
};

struct RoundState {
    int currentRound = 1;
    int totalRounds = 6;
    uint32_t drawingPlayerId = 0;
    std::string drawingPlayerName;
    std::string maskedWord = "------";
    std::string drawerWord;
    std::string revealedLetter;
    std::string correctWord;
    int timeLeftSeconds = 0;
    bool localPlayerIsDrawer = false;
    // Absolute local deadline (epoch ms). 0 = no active timer.
    // Used to interpolate the countdown between server snapshots.
    uint64_t endsAtLocalMs = 0;
};

struct MatchResults {
    std::vector<PlayerInfo> leaderboard;
    std::string bestDrawer;
    std::string fastestGuesser;
    std::string hardestWord;
};

struct SessionState {
    bool initialized = false;
    bool connecting = false;
    bool connected = false;
    bool authenticated = false;
    bool offlinePreview = false;
    int onlineCount = 0;
    uint32_t localAccountID = 0;
    uint32_t currentRoomId = 0;
    std::string displayName;
    std::string authToken;
    std::string serverURL;
    std::vector<PlayerInfo> onlinePlayers;
    std::vector<RoomInfo> rooms;
    RoomInfo currentRoom;
    RoundState currentRound;
    std::vector<ChatMessage> roomChat;
    std::vector<StrokeSegment> recentStrokes;
    MatchResults results;
    matjson::Value settings = matjson::Value::object();
    matjson::Value stats = matjson::Value::object();
};

inline uint64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

inline cocos2d::ccColor3B accentColor() {
    // GD gold (same base as goldFont.fnt).
    return {255, 217, 119};
}

inline char const* statusLabel(PlayerStatus status) {
    switch (status) {
        case PlayerStatus::InRoom: return "IN ROOM";
        case PlayerStatus::Playing: return "PLAYING";
        default: return "FREE";
    }
}

inline char const* roomStateLabel(RoomState state) {
    switch (state) {
        case RoomState::InGame: return "IN GAME";
        default: return "WAITING";
    }
}

inline char const* modeLabel(GameMode mode) {
    switch (mode) {
        case GameMode::Animation: return "Animation";
        case GameMode::Chain: return "Chain";
        default: return "Classic";
    }
}

inline char const* languageLabel(WordLanguage language) {
    switch (language) {
        case WordLanguage::English: return "EN";
        case WordLanguage::Both: return "ES + EN";
        default: return "ES";
    }
}

inline char const* difficultyLabel(WordDifficulty difficulty) {
    switch (difficulty) {
        case WordDifficulty::Medium: return "MEDIUM";
        case WordDifficulty::Hard: return "HARD";
        default: return "EASY";
    }
}

inline std::string sanitizeRoomName(std::string name) {
    geode::utils::string::trimIP(name);
    if (name.size() > 32) {
        name.resize(32);
    }
    return name;
}

} // namespace paidraw
