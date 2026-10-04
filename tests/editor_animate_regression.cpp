#define FMT_HEADER_ONLY
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../src/features/editor-animate/AnimateTypes.cpp"
#include "../src/features/editor-animate/services/AnimatePlanner.cpp"

using namespace paimon::animate;

namespace {

int g_failures = 0;

void check(bool ok, std::string const& what) {
    if (ok) return;
    ++g_failures;
    std::cerr << "FAIL: " << what << "\n";
}

Clip clipWith(int frames, PlayMode mode, float fps = 10.f) {
    Clip clip;
    clip.id = 1;
    clip.name = "test";
    clip.settings.fps = fps;
    clip.settings.mode = mode;
    for (int i = 0; i < frames; ++i) clip.frames.push_back({100 + i, 1, "", false});
    return clip;
}

std::vector<std::size_t> order(std::vector<Step> const& steps) {
    std::vector<std::size_t> out;
    for (auto const& step : steps) out.push_back(step.frame);
    return out;
}

using Map = std::map<std::string, std::string>;

std::vector<Map> parseObjects(std::string const& payload) {
    std::vector<Map> objects;
    std::stringstream stream(payload);
    std::string object;
    while (std::getline(stream, object, ';')) {
        if (object.empty()) continue;
        Map map;
        std::stringstream fields(object);
        std::string key, value;
        while (std::getline(fields, key, ',') && std::getline(fields, value, ',')) map[key] = value;
        objects.push_back(map);
    }
    return objects;
}

bool inGroup(Map const& object, int group) {
    auto it = object.find("57");
    if (it == object.end()) return false;
    std::stringstream stream(it->second);
    std::string part;
    while (std::getline(stream, part, '.')) {
        if (std::stoi(part) == group) return true;
    }
    return false;
}

// tiny trigger interpreter: follows the spawn chain and tracks frame visibility.
struct Simulation {
    std::vector<Map> objects;
    std::map<int, bool> visible;
    std::vector<int> shownSequence;

    void runGroup(int group, int depthBudget) {
        if (depthBudget <= 0) return;
        for (auto const& object : objects) {
            if (!inGroup(object, group) || !object.count("62")) continue;
            fire(object, depthBudget);
        }
    }

    void fire(Map const& object, int depthBudget) {
        int const id = std::stoi(object.at("1"));
        int const target = std::stoi(object.at("51"));
        if (id == triggerids::Alpha) {
            visible[target] = object.at("35") == "1";
            if (visible[target]) shownSequence.push_back(target);
        } else if (id == triggerids::Toggle) {
            visible[target] = object.count("56") > 0;
            if (visible[target]) shownSequence.push_back(target);
        } else if (id == triggerids::Spawn) {
            runGroup(target, depthBudget - 1);
        }
    }

    void runLoose() {
        for (auto const& object : objects) {
            if (object.count("62")) continue;
            fire(object, 64);
        }
    }

    int litCount() const {
        int lit = 0;
        for (auto const& [group, on] : visible) lit += on ? 1 : 0;
        return lit;
    }
};

PlanGroups allocate(Clip const& clip, std::vector<Step> const& steps) {
    PlanGroups groups;
    int next = 900;
    groups.marker = next++;
    groups.chain = next++;
    groups.start = next++;
    if (clip.settings.controls) {
        groups.stop = next++;
        groups.pause = next++;
        groups.resume = next++;
    }
    for (std::size_t i = 0; i < eventGroupsNeeded(clip, steps); ++i) groups.events.push_back(next++);
    return groups;
}

void testSequences() {
    auto loop = clipWith(4, PlayMode::Loop);
    check(order(buildSequence(loop)) == std::vector<std::size_t>{0, 1, 2, 3}, "loop order");

    auto reverse = clipWith(4, PlayMode::Reverse);
    check(order(buildSequence(reverse)) == std::vector<std::size_t>{3, 2, 1, 0}, "reverse order");

    auto ping = clipWith(5, PlayMode::PingPong);
    check(order(buildSequence(ping)) == std::vector<std::size_t>{0, 1, 2, 3, 4, 3, 2, 1},
        "pingpong skips both ends on the way back");

    auto pingTwo = clipWith(2, PlayMode::PingPong);
    check(order(buildSequence(pingTwo)) == std::vector<std::size_t>{0, 1}, "pingpong of two");

    auto repeat = clipWith(3, PlayMode::Repeat);
    repeat.settings.repeats = 3;
    check(buildSequence(repeat).size() == 9, "repeat unrolls");

    auto single = clipWith(1, PlayMode::Repeat);
    single.settings.repeats = 4;
    auto merged = buildSequence(single);
    check(merged.size() == 1 && std::abs(merged[0].duration - 0.4f) < 1e-4f,
        "repeated single frame merges into one long step");

    auto held = clipWith(3, PlayMode::Loop, 12.f);
    held.frames[1].hold = 3;
    held.frames[2].skip = true;
    auto steps = buildSequence(held);
    check(order(steps) == std::vector<std::size_t>{0, 1}, "skipped frames leave the sequence");
    check(std::abs(steps[1].duration - 0.25f) < 1e-4f, "hold multiplies frame time");
    check(std::abs(sequenceDuration(steps) - (1.f / 12.f + 0.25f)) < 1e-4f, "total duration");

    auto huge = clipWith(300, PlayMode::Repeat);
    huge.settings.repeats = 64;
    check(buildSequence(huge).size() <= static_cast<std::size_t>(kMaxSteps), "step cap");

    Clip empty;
    check(buildSequence(empty).empty(), "empty clip");
}

void testPlanLoop() {
    auto clip = clipWith(4, PlayMode::Loop);
    auto steps = buildSequence(clip);
    auto groups = allocate(clip, steps);
    auto triggers = planTriggers(clip, steps, groups, {300.f, 100.f, 200.f, 0.f});
    auto payload = serializeTriggers(triggers, 0);
    auto objects = parseObjects(payload);
    check(!objects.empty(), "loop emits triggers");

    for (auto const& object : objects) {
        check(inGroup(object, groups.marker), "every trigger carries the marker group");
    }

    Simulation sim;
    sim.objects = objects;
    sim.runGroup(groups.start, 4 * 3);
    check(sim.litCount() == 1, "only one frame lit after running the loop");
    check(sim.shownSequence.size() >= 4 &&
        sim.shownSequence[0] == 100 && sim.shownSequence[1] == 101 &&
        sim.shownSequence[2] == 102 && sim.shownSequence[3] == 103,
        "start group plays frames in order");
    check(std::count(sim.shownSequence.begin(), sim.shownSequence.end(), 100) >= 2,
        "loop wraps back to the first frame");

    bool looseStart = false;
    for (auto const& object : objects) {
        if (object.count("62")) continue;
        looseStart |= std::stoi(object.at("1")) == triggerids::Spawn &&
            std::abs(std::stof(object.at("2")) - kLevelStartX) < 0.01f;
    }
    check(looseStart, "level start mode adds a loose spawn at the level start");
}

void testPlanOnceEnd() {
    auto clip = clipWith(3, PlayMode::Once);
    clip.settings.end = EndAction::ShowFirst;
    clip.settings.start = StartMode::Group;
    clip.settings.before = BeforeStart::HideAll;
    clip.settings.visibility = Visibility::Toggle;
    auto steps = buildSequence(clip);
    check(needsEndEvent(clip, steps), "show-first end needs an end event");
    auto groups = allocate(clip, steps);
    auto objects = parseObjects(serializeTriggers(
        planTriggers(clip, steps, groups, {0.f, 0.f, 0.f, 0.f}), 7));

    Simulation sim;
    sim.objects = objects;
    sim.runLoose();
    check(sim.litCount() == 0, "hide-all before start leaves nothing lit");
    for (auto const& object : objects) {
        check(object.count("62") || std::stoi(object.at("1")) != triggerids::Spawn,
            "group start mode adds no loose spawn");
        check(object.count("20") && object.at("20") == "7", "editor layer applied");
    }

    sim.runGroup(groups.start, 32);
    check(sim.visible[100] && !sim.visible[101] && !sim.visible[102],
        "once + show first returns to the first frame");
}

void testControls() {
    auto clip = clipWith(2, PlayMode::Loop);
    clip.settings.stopAction = EndAction::HideAll;
    auto steps = buildSequence(clip);
    auto groups = allocate(clip, steps);
    auto objects = parseObjects(serializeTriggers(
        planTriggers(clip, steps, groups, {0.f, 0.f, 0.f, 0.f}), 0));

    int stops = 0;
    for (auto const& object : objects) {
        if (std::stoi(object.at("1")) != triggerids::Stop) continue;
        ++stops;
        check(std::stoi(object.at("51")) == groups.chain, "stop triggers target the chain");
        check(!inGroup(object, groups.chain), "control triggers stay outside the chain");
    }
    check(stops == 3, "stop, pause and resume");

    clip.settings.controls = false;
    auto lean = allocate(clip, steps);
    auto none = parseObjects(serializeTriggers(planTriggers(clip, steps, lean, {}), 0));
    for (auto const& object : none) {
        check(std::stoi(object.at("1")) != triggerids::Stop, "no controls without the option");
    }
}

void testSaveStrings() {
    check(rewriteGroups("1,1,2,15,3,15,57,4.9", {9}, 30) == "1,1,2,15,3,15,57,4.30", "swap group");
    check(rewriteGroups("1,1,2,15,3,15", {}, 30) == "1,1,2,15,3,15,57,30", "add missing groups key");
    check(rewriteGroups("1,1,57,9,2,15;", {9}, 0) == "1,1,2,15", "drop empty groups key");
    check(rewriteGroups("1,914,31,aGVsbG8=,57,3", {3}, 3) == "1,914,31,aGVsbG8=,57,3",
        "base64 text survives and no duplicate group");
}

void testGroupInput() {
    check(parseGroupInput("3, 7,10-12") == std::vector<int>{3, 7, 10, 11, 12}, "list with range");
    check(parseGroupInput("5-3 5 0 x 10000") == std::vector<int>{5, 4, 3}, "reverse range, junk and repeats");
    check(parseGroupInput("1-9999").size() == static_cast<std::size_t>(kMaxFrames), "range cap");
    check(parseGroupInput("").empty(), "empty input");
}

void testSanitize() {
    Clip clip = clipWith(2, PlayMode::Loop, 500.f);
    clip.frames[0].hold = 0;
    clip.frames[1].group = 20000;
    clip.color = -3;
    sanitize(clip);
    check(clip.settings.fps == kMaxFps, "fps clamped");
    check(clip.frames[0].hold == 1, "hold clamped");
    check(clip.frames[1].group == 0, "invalid group dropped");
    check(clip.color >= 0 && clip.color < kColorTags, "color wraps");
}

} // namespace

int main() {
    testSequences();
    testPlanLoop();
    testPlanOnceEnd();
    testControls();
    testSaveStrings();
    testSanitize();
    testGroupInput();
    if (g_failures == 0) std::cout << "editor animate: all checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
