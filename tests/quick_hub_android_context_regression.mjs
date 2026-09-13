import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const read = (path) => readFileSync(new URL(`../${path}`, import.meta.url), "utf8");

const manager = read("src/features/quick-hub/services/QuickHubManager.cpp");
const radial = read("src/features/quick-hub/ui/QuickHubRadial.cpp");
const touch = read("src/features/quick-hub/services/QuickHubTouchHold.cpp");

assert.match(manager, /if \(PlayLayer::get\(\)\) return false;/);
assert.match(manager, /m_playbackMode == PlaybackMode::Not/);
assert.match(radial, /if \(!QuickHubManager::canOpenInCurrentContext\(\)\) return;/);

assert.match(touch, /menu->m_eState == kCCMenuStateTrackingTouch/);
assert.match(touch, /menu->m_pSelectedItem/);
assert.match(touch, /scene == s_touch\.startScene/);
assert.match(touch, /if \(!gestureContextIsStillValid\(\)\)/);

const dispatch = touch.indexOf("CCEGLViewProtocol::handleTouchesBegin");
const uiCheck = touch.indexOf("hasTrackingMenu(scene)");
assert.ok(dispatch >= 0 && uiCheck > dispatch,
    "button detection must run after Cocos dispatches the touch");

console.log("PASS: Quick Hub ignores Android gameplay and claimed UI touches");
