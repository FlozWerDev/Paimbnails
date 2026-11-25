#include "../src/features/transitions/services/TransitionEvaluation.hpp"
#include "../src/features/transitions/services/TransitionEditing.hpp"
#include <cassert>
#include <cmath>
#include <limits>

using namespace paimon::transitions;

namespace {
bool close(float a, float b) { return std::abs(a - b) < .001f; }
TransitionCommand object(float start = 0.f, float duration = 2.f) {
    TransitionCommand c;
    c.action = CommandAction::Image;
    c.startTime = start; c.duration = duration;
    c.fromX = c.fromY = .5f;
    c.fromVal = c.toVal = 255.f;
    return c;
}
TransitionCommand trigger(CommandAction action, float start, float duration) {
    TransitionCommand c;
    c.action = action; c.targetObj = 0;
    c.startTime = start; c.duration = duration;
    return c;
}
}

int main() {
    auto image = object();
    auto move = trigger(CommandAction::Move, .5f, 1.f);
    move.fromX = .2f; move.toX = .8f; move.fromY = move.toY = .5f;
    auto alpha = trigger(CommandAction::FadeOut, 1.f, .5f);
    alpha.fromVal = 255.f; alpha.toVal = 0.f;
    auto rotate = trigger(CommandAction::Rotate, 0.f, 1.f);
    rotate.fromVal = 0.f; rotate.toVal = 360.f;
    TransitionEvaluator player;
    player.configure({image, move, alpha, rotate});
    assert(close(player.timeline().duration, 2.f));
    auto frame = player.sample(.25f, {1000.f, 500.f}, {0.f, 0.f}, .5f);
    assert(close(frame.objects[0].position.x, 500.f));
    assert(close(frame.objects[0].rotation, 90.f));
    frame = player.sample(1.f, {1000.f, 500.f}, {0.f, 0.f}, .5f);
    assert(close(frame.objects[0].position.x, 500.f));
    assert(frame.sceneB.opacity == 255.f && frame.sceneA.opacity == 0.f);
    frame = player.sample(1.25f, {1000.f, 500.f}, {0.f, 0.f}, .5f);
    assert(close(frame.objects[0].opacity, 127.5f));
    frame = player.sample(1.75f, {1000.f, 500.f}, {0.f, 0.f}, .5f);
    assert(frame.objects[0].opacity == 0.f); // the image clip must not overwrite Alpha
    assert(close(frame.objects[0].position.x, 800.f));
    frame = player.sample(.25f, {1000.f, 500.f}, {0.f, 0.f}, .5f);
    assert(frame.objects[0].opacity == 255.f); // scrubbing backwards is deterministic
    frame = player.sample(2.f, {1000.f, 500.f}, {0.f, 0.f}, .5f);
    assert(!frame.objects[0].visible); // the final frame must reveal Scene B
    frame = player.sample(2.1f, {1000.f, 500.f}, {0.f, 0.f}, .5f);
    assert(!frame.objects[0].visible); // object lifetime belongs to its own track

    auto follow = trigger(CommandAction::FollowCursor, .5f, .5f);
    player.configure({image, follow});
    frame = player.sample(.75f, {1000.f, 500.f}, {700.f, 150.f});
    assert(close(frame.objects[0].position.x, 700.f));
    frame = player.sample(1.2f, {1000.f, 500.f}, {900.f, 200.f});
    assert(close(frame.objects[0].position.x, 700.f)); // cursor stops at the trigger's end
    frame = player.sample(.2f, {1000.f, 500.f}, {900.f, 200.f});
    assert(close(frame.objects[0].position.x, 500.f));

    image.enabled = false;
    player.configure({image, move});
    frame = player.sample(1.f, {1000.f, 500.f}, {});
    assert(!frame.objects[0].visible); // disabling cannot turn an object trigger into a scene trigger
    assert(close(frame.sceneA.position.x, 500.f));
    image.enabled = true;
    TransitionCommand disabled;
    disabled.enabled = false;
    move.targetObj = 1;
    player.configure({disabled, image, move});
    frame = player.sample(1.25f, {1000.f, 500.f}, {});
    assert(close(frame.objects[1].position.x, 650.f)); // stable references across disabled clips

    std::vector<TransitionCommand> commands{disabled, image, move};
    insertTransitionCommand(commands, 0, object());
    assert(commands[3].targetObj == 2);
    moveTransitionCommand(commands, 2, 1);
    assert(commands[3].targetObj == 1);
    eraseTransitionCommand(commands, 0);
    assert(commands[2].targetObj == 0);
    eraseTransitionCommand(commands, 0);
    assert(commands.size() == 1 && !commands[0].enabled); // deleting an object removes its triggers

    TransitionCommand spawn;
    spawn.action = CommandAction::Spawn; spawn.spawnCount = 2;
    image.startTime = -1.f; move.startTime = -1.f; move.targetObj = 1;
    commands = {spawn, image, move};
    makeTransitionTimelineAbsolute(commands);
    assert(commands.size() == 2 && commands[1].targetObj == 0);
    assert(commands[0].startTime == 0.f && commands[1].startTime == 0.f);
    // Moving an absolute trigger doesn't move the object or the other triggers.
    commands[1].startTime = .75f;
    player.configure(commands);
    assert(player.timeline().clips[0].start == 0.f);
    assert(close(player.timeline().clips[1].start, .75f));

    image.startTime = -1.f; image.duration = .5f;
    image.fromVal = image.toVal = 0.f;
    move.startTime = -1.f; move.targetObj = 0; move.duration = .5f;
    commands = {image, move};
    makeTransitionTimelineAbsolute(commands);
    assert(commands[0].duration == 1.f && commands[0].toVal == 255.f);

    image = object(.5f, 1.f);
    image.fromVal = image.toVal = 0.f;
    player.configure({image});
    frame = player.sample(1.f, {1000.f, 500.f}, {});
    assert(frame.objects[0].opacity == 0.f); // explicit zero alpha stays invisible
    player.configure({object()}, 5.f);
    assert(player.timeline().duration == 5.f);
    frame = player.sample(std::numeric_limits<float>::quiet_NaN(), {1000.f, 500.f}, {});
    assert(std::isfinite(frame.sceneA.position.x));
}
