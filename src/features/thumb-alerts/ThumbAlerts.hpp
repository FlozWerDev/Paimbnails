#pragma once

// On-screen card for a new live thumbnail: GDUtils-style level data over the uploaded thumbnail.

#include <Geode/DefaultInclude.hpp>
#include <string>

namespace paimon::thumbalerts {

constexpr char const* kModuleId = "paimbnails.thumbalerts.social";

// One freshly published thumbnail, as /api/latest-uploads reports it.
struct NewThumb {
    int levelId = 0;
    std::string eventId;
    std::string levelName;
    std::string creator;   // level author
    std::string uploader;  // who uploaded the thumbnail
    std::string difficulty;  // server side name: "Easy", "Extreme Demon"...
    std::string length;
    int stars = 0;
    int coins = 0;
    int downloads = 0;
    int likes = 0;
    bool verifiedCoins = false;
    int rateTier = 0;  // 0 none, 1 featured, 2 epic, 3 legendary, 4 mythic
};

enum class Spot : int {
    TopLeft, TopCenter, TopRight,
    MidLeft, MidRight,
    BottomLeft, BottomCenter, BottomRight,
};

enum class Enter : int {
    None, Slide, Fade, Pop, Drop, Flip, Zoom, Elastic, Unfold, Swing, Spiral,
};

enum class Exit : int {
    None, Slide, Fade, Shrink, Fall, Flip, Zoom, Spin, Fold,
};

enum class Idle : int { None, Float, Pulse, Sway, Tilt };

enum class Sound : int { None, Soft, Coin, Crystal, Achievement };

struct Config {
    bool enabled = true;
    Spot spot = Spot::TopRight;
    Enter enter = Enter::Slide;
    Exit exit = Exit::Slide;
    Idle idle = Idle::Float;
    Sound sound = Sound::Soft;
    float enterTime = 0.55f;
    float exitTime = 0.40f;
    float hold = 5.f;
    float scale = 1.f;
    float gap = 0.6f;
    float offsetX = 0.f;
    float offsetY = 0.f;
    int dim = 120;
    bool kenBurns = true;
    bool shine = true;
    bool progress = true;
    bool stats = true;
    bool click = true;
    int maxBatch = 3;
    bool whilePlaying = false;
    bool whileEditing = false;
};

constexpr float kMinScale = 0.6f;
constexpr float kMaxScale = 1.6f;

Config readConfig();

// Scenes where the card would get in the way more than it helps.
bool alertsAllowedHere(Config const& config);

// Queues the card; thumbnail downloads first so the background is there on slide-in.
void showThumbAlert(NewThumb item);

// Uploader's own card from the upload response: the only instant path (HTTP replies reach only their caller).
void showThumbAlertForUpload(int levelId, std::string const& uploader,
                             std::string const& levelMeta,
                             std::string const& serverMessage);

// Settings "Preview": same card, ignoring toggle and scene filters.
void showThumbAlertPreview();

} // namespace paimon::thumbalerts
