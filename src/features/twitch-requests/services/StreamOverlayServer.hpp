#pragma once

#include <Geode/Geode.hpp>

#include <memory>
#include <string>
#include <vector>

namespace paimon::twitch {

enum class StreamOverlayLayout : int { Cards, Compact, Ticker };
constexpr int kStreamOverlayLayoutCount = 3;

enum class StreamOverlayAnimation : int { Flow, Slide, Pulse, None };
constexpr int kStreamOverlayAnimationCount = 4;

struct StreamOverlayConfig {
    StreamOverlayLayout layout = StreamOverlayLayout::Cards;
    StreamOverlayAnimation animation = StreamOverlayAnimation::Flow;
    int nextCount = 4;
    float scale = 1.f;
    float opacity = .86f;
    float roundness = 22.f;
    cocos2d::ccColor3B accent = {166, 112, 255};
    cocos2d::ccColor3B background = {10, 13, 29};
    cocos2d::ccColor3B text = {255, 255, 255};
    bool showLevelID = true;
    bool showAuthor = true;
    bool showRequester = true;
    bool showProgress = true;
    bool showQueueCount = true;
};

std::vector<std::string> streamOverlayLayoutNames();
std::vector<std::string> streamOverlayAnimationNames();
StreamOverlayConfig const& streamOverlayConfig();
void setStreamOverlayConfig(StreamOverlayConfig config);

class StreamOverlayServer final {
public:
    static StreamOverlayServer& get();

    void init();
    void shutdown();
    void restart();
    void tick(float dt);
    void refreshSnapshot();

    bool isRunning() const;
    bool supported() const;
    std::string statusText() const;
    std::string overlayUrl() const;
    std::string previewUrl() const;

private:
    StreamOverlayServer();
    ~StreamOverlayServer();

    void start();
    void stop();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    float m_refreshClock = 0.f;
    uint64_t m_revision = 0;
    bool m_initialized = false;
};

} // namespace paimon::twitch
