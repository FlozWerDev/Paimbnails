#pragma once

// the settings popup draws the same card the stream shows: what you preview
// is what goes out.

#include <Geode/Geode.hpp>

#include "sources/ChatSource.hpp"

#include <functional>
#include <string>
#include <vector>

namespace paimon::twitch {

struct LevelRequest;

// shared by the card and the request list.
bool motionOn();
float animTime(float seconds);
std::string shorten(std::string text, size_t limit);

// the nine screen corners/sides in reading order:
// row = index / 3 (0 top, 1 mid, 2 bottom), column = index % 3.
enum class NotifySpot : int {
    TopLeft, TopCenter, TopRight,
    MidLeft, Center, MidRight,
    BottomLeft, BottomCenter, BottomRight,
};
constexpr int kNotifySpotCount = 9;

enum class NotifyEnter : int { None, Slide, Fade, Pop, Drop, Spin };
constexpr int kNotifyEnterCount = 6;

enum class NotifyExit : int { None, Slide, Fade, Shrink, Rise, Spin };
constexpr int kNotifyExitCount = 6;

enum class NotifySound : int { None, Soft, Coin, Crystal, Achievement };
constexpr int kNotifySoundCount = 5;

struct NotifyConfig {
    bool enabled = false;
    NotifySpot spot = NotifySpot::TopRight;
    float offsetX = 0.f;   // fine tune in pixels over the chosen spot
    float offsetY = 0.f;
    float scale = 1.f;
    float seconds = 3.f;
    NotifyEnter enter = NotifyEnter::Slide;
    NotifyExit exit = NotifyExit::Fade;
    NotifySound sound = NotifySound::Soft;
    bool showLevel = true;
    bool showRequester = true;
    bool overLayer = false;  // also notify with the request list open
};

// slider bounds, shared with the popup.
constexpr float kNotifyMinScale = 0.6f;
constexpr float kNotifyMaxScale = 1.8f;
constexpr float kNotifyMinSeconds = 1.f;
constexpr float kNotifyMaxSeconds = 10.f;
constexpr float kNotifyMaxOffsetX = 140.f;
constexpr float kNotifyMaxOffsetY = 90.f;

std::vector<std::string> notifySpotNames();
std::vector<std::string> notifyEnterNames();
std::vector<std::string> notifyExitNames();
std::vector<std::string> notifySoundNames();

NotifyConfig const& notifyConfig();
void setNotifyConfig(NotifyConfig config);
void loadNotifyConfig();

// per-chat accent; used by the card and the request list.
cocos2d::ccColor3B platformAccent(Platform platform);

// the card as seen, unscaled and unplaced (center anchor).
// empty `levelName` leaves the level line on the ID.
cocos2d::CCNodeRGBA* buildNotifyCard(
    NotifyConfig const& config,
    Platform platform,
    std::string levelName,
    int levelID,
    std::string requester);

// resting spot in screen coordinates; `slot` stacks over notices already
// placed. `card` is the unscaled card size.
cocos2d::CCPoint notifyRestPoint(
    NotifyConfig const& config, cocos2d::CCSize card, int slot = 0);

// animate the card you pass: they read its current scale and size, so they
// work for the real screen and the preview one alike.
void runNotifyEnter(cocos2d::CCNodeRGBA* card, NotifyConfig const& config, cocos2d::CCPoint rest);
void runNotifyExit(cocos2d::CCNodeRGBA* card, NotifyConfig const& config, cocos2d::CCPoint rest,
    std::function<void()> onDone);
// enter duration, so the second counter knows when to start.
float notifyEnterSeconds(NotifyConfig const& config);

void playNotifySound(NotifyConfig const& config);

// real notice, from the queue.
void showRequestNotify(LevelRequest const& request);
// fake one from the "Try" button; shows even with notices off.
void showNotifyDemo();

} // namespace paimon::twitch
