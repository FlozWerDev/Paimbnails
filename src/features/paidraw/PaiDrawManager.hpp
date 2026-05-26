#pragma once

#include "PaiDrawEvents.hpp"
#include "PaiDrawModels.hpp"
#include "../../framework/EventBus.hpp"
#include <Geode/utils/web.hpp>
#include <Geode/loader/Event.hpp>
#include <Geode/utils/function.hpp>
#include <matjson.hpp>
#include <mutex>

namespace paidraw {

class PaiDrawManager : public cocos2d::CCObject {
public:
    static PaiDrawManager& get();

    void init();
    void shutdown();

    SessionState snapshot() const;
    bool isReady() const;

    void ensureConnected();
    void disconnect();

    void refreshLobby();
    void createRoom(RoomConfig const& config);
    void joinRoom(uint32_t roomId, std::string const& password = "");
    void leaveRoom();
    void toggleReady();
    void startGame();
    void updateRoomConfig(RoomConfig const& config);
    void sendRoomChat(std::string const& text);
    void sendGuess(std::string const& text);
    void sendStroke(StrokeSegment const& stroke);
    void clearCanvas();
    void publishPresence(std::string const& status);

    WordEntry currentWord() const;

private:
    PaiDrawManager();
    ~PaiDrawManager() override;

    void loadSavedState();
    void saveSessionData();
    void saveStats();
    void configureOfflinePreview();
    void seedWordBank();

    std::string baseServerUrl() const;
    std::string wsServerUrl() const;
    std::string endpointUrl(char const* suffix) const;
    bool hasValidLogin() const;

    geode::ByteVector encodeJson(matjson::Value const& value) const;
    void sendPacket(PacketType type, matjson::Value const& payload, uint32_t roomId = 0);
    void authenticate();
    void handlePacket(PaiDrawPacket const& packet);
    void handleLobbySnapshot(matjson::Value const& payload);
    void handleRoomSnapshot(matjson::Value const& payload);
    void handleChatPacket(matjson::Value const& payload);
    void handleGuessFeedback(matjson::Value const& payload);
    void handleRoundSync(matjson::Value const& payload);
    void handleStrokePacket(matjson::Value const& payload);
    void handleResults(matjson::Value const& payload);
    void handlePresence(matjson::Value const& payload);

    PlayerInfo parsePlayer(matjson::Value const& value) const;
    RoomInfo parseRoom(matjson::Value const& value) const;
    ChatMessage parseChat(matjson::Value const& value) const;
    StrokeSegment parseStroke(matjson::Value const& value) const;
    MatchResults parseResults(matjson::Value const& value) const;

    void publishConnection(std::string const& message = "");
    void publishLobby();
    void publishRoom();
    void publishChat();
    void publishRound();
    void publishResults();
    void publishStroke(StrokeSegment const& stroke);

    void queueMain(geode::Function<void()> fn) const;
    void requestJson(
        std::string const& method,
        std::string const& path,
        matjson::Value const* body,
        bool requireAuth,
        geode::CopyableFunction<void(bool, matjson::Value const&, std::string const&)> callback
    );
    void handleLobbyHttpSnapshot(matjson::Value const& payload);
    void handleRoomHttpSnapshot(matjson::Value const& payload);
    void syncCurrentRoomIntoLobby();
    void schedulePollLoop();
    void pollLoopTick(uint32_t generation);
    float pollIntervalForState() const;
    void queueStrokeFlush();
    void flushPendingStrokes();
    void applyRequestFailure(std::string const& message, bool disconnectTransport = false);

    mutable std::mutex m_mutex;
    SessionState m_state;
    std::vector<WordEntry> m_wordBank;
    std::vector<StrokeSegment> m_pendingStrokes;
    std::string m_lobbyVersion;
    std::string m_roomVersion;
    bool m_initialized = false;
    bool m_socketOpen = false;
    bool m_authInFlight = false;
    bool m_lobbyRequestInFlight = false;
    bool m_roomRequestInFlight = false;
    bool m_strokeFlushScheduled = false;
    bool m_shuttingDown = false;
    uint32_t m_pollGeneration = 0;
};

} // namespace paidraw
