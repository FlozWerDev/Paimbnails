import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

const read = (path) => readFileSync(new URL(`../${path}`, import.meta.url), "utf8");

const idBadge = read("src/features/info-suite/services/IdBadge.cpp");
const quickHub = read("src/features/quick-hub/services/QuickHubKeybind.cpp");
const volumeScroll = read("src/features/volume-scroll/hooks/VolumeScrollHook.cpp");
const searchPopup = read("src/features/info-suite/ui/AdvancedSearchPopup.hpp");

for (const [name, source] of [
    ["IdBadge.cpp", idBadge],
    ["QuickHubKeybind.cpp", quickHub],
    ["VolumeScrollHook.cpp", volumeScroll],
]) {
    assert.ok(
        !source.includes("Geode/modify/CCKeyboardDispatcher.hpp"),
        `${name} must not include the non-portable CCKeyboardDispatcher modify binding`,
    );
}

assert.match(idBadge, /KeyboardInputEvent\(\)\.listen/);
assert.match(quickHub, /KeyboardInputEvent\(\)\.listen/);
assert.match(quickHub, /KeyboardModifier::Super/);
assert.match(searchPopup, /bool init\(\) override;/);

console.log("PASS: iOS uses portable keyboard events and valid popup overrides");
