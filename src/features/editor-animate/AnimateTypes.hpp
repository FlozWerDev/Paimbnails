#pragma once

// std-only on purpose: the sequence and trigger planner are tested on the host.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace paimon::animate {

enum class PlayMode { Loop, Once, PingPong, Repeat, Reverse };
enum class EndAction { HoldLast, HideAll, ShowFirst };
enum class StartMode { LevelStart, Group, Position };
enum class Visibility { Alpha, Toggle };
enum class BeforeStart { FirstFrame, HideAll, ShowAll };
enum class Ghosts { Hidden, Dim, Visible };

inline constexpr int kMaxClips = 64;
inline constexpr int kMaxFrames = 512;
inline constexpr int kMaxSteps = 2048;
inline constexpr int kMaxHold = 99;
inline constexpr int kMaxRepeats = 64;
inline constexpr int kColorTags = 8;
inline constexpr float kMinFps = 1.f;
inline constexpr float kMaxFps = 60.f;
inline constexpr int kMaxGroup = 9999;

struct Frame {
    int group = 0;
    int hold = 1;
    std::string label;
    bool skip = false;
};

struct ClipSettings {
    float fps = 12.f;
    PlayMode mode = PlayMode::Loop;
    int repeats = 2;
    EndAction end = EndAction::HoldLast;
    StartMode start = StartMode::LevelStart;
    BeforeStart before = BeforeStart::FirstFrame;
    Visibility visibility = Visibility::Alpha;
    float startDelay = 0.f;
    float fade = 0.f;
    float startX = 0.f;
    bool controls = true;
    EndAction stopAction = EndAction::HoldLast;
    int editorLayer = 0;
    bool compileOnSave = false;
};

// marker tags every generated trigger so cleanup never touches the user's own
// triggers, even the ones they drop into the start group on purpose.
struct Compiled {
    int marker = 0;
    int chain = 0;
    int start = 0;
    int stop = 0;
    int pause = 0;
    int resume = 0;
    int triggers = 0;
    int steps = 0;
    bool stale = false;

    bool present() const { return marker > 0 && triggers > 0; }
};

struct Clip {
    int id = 0;
    std::string name;
    int color = 0;
    bool hidden = false;
    bool locked = false;
    ClipSettings settings;
    std::vector<Frame> frames;
    Compiled compiled;
};

struct Document {
    std::vector<Clip> clips;
    int nextId = 1;
    int activeClip = 0;
    int currentFrame = 0;
};

struct ViewPrefs {
    bool onion = false;
    int onionBefore = 1;
    int onionAfter = 1;
    int onionOpacity = 90;
    Ghosts ghosts = Ghosts::Hidden;
    int dimOpacity = 50;
    bool lockOthers = true;
    bool autoAssign = true;
    bool previewLoop = true;
    bool followPlayhead = true;
};

struct Step {
    std::size_t frame = 0;
    float duration = 0.f;
};

void sanitize(ClipSettings& settings);
void sanitize(Clip& clip);
void sanitize(ViewPrefs& prefs);

float frameDuration(Clip const& clip, Frame const& frame);
std::vector<Step> buildSequence(Clip const& clip);
float sequenceDuration(std::vector<Step> const& steps);
bool loops(PlayMode mode);

// "3, 7, 10-14" as typed in the import prompt; keeps order, drops repeats.
std::vector<int> parseGroupInput(std::string_view text);

} // namespace paimon::animate
