"""Validate native hook availability and the complete UI inventory without building."""

import json
import re
from collections import defaultdict

from audit_dynamic_transition_bindings import ROOT, bindings_directory, collect, inventory, markdown


def blocks(source):
    for match in re.finditer(r'class \$modify\(\w+,\s*(\w+)\)\s*\{', source):
        depth, end = 1, match.end()
        while depth and end < len(source):
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        yield match[1], source[match.end():end - 1]


def run():
    directory = bindings_directory()
    classes, _ = collect(directory)
    generated = ROOT / "build-win/bindings/bindings/Geode/modify"
    hooks = defaultdict(list)
    count = 0
    for path in (
        ROOT / "src/features/transitions/hooks/DynamicTransitionPanelHook.cpp",
        ROOT / "src/features/transitions/hooks/DynamicTransitionKeyboardHook.cpp",
        ROOT / "src/hooks/DynamicPopupHook.cpp",
    ):
        source = path.read_text(encoding="utf-8-sig")
        source = re.sub(r'#ifdef GEODE_IS_MACOS\n.*?#endif', "", source, flags=re.S)
        for name, body in blocks(source):
            qualified = name if name in classes else f"cocos2d::{name}"
            assert qualified in classes, f"Unknown native class: {qualified}"
            methods = {method["name"]: method for method in classes[qualified]["methods"]}
            for method in re.findall(r'^    (?:bool|void) (\w+)\(', body, re.M):
                if method not in ("show", "showLayer", "hideLayer", "animateInRandomSide",
                                  "dispatchKeyboardMSG", "dispatchKeypadMSG", "addChild",
                                  "removeChild", "removeAllChildrenWithCleanup"):
                    continue
                assert method in methods, f"Missing binding: {qualified}::{method}"
                header = (generated / f"{name}.hpp").read_text()
                assert re.search(rf'GEODE_APPLY_MODIFY_FOR_FUNCTION\([^\n]*, {method},', header), \
                    f"Unhookable Windows method: {qualified}::{method}"
                match = re.search(r'\bwin (0x[\da-f]+)\b', methods[method]["bindings"])
                if match:
                    hooks[match[1]].append(f"{qualified}::{method}")
                count += 1
    duplicates = {address: names for address, names in hooks.items() if len(names) > 1}
    assert not duplicates, f"Duplicate hooks on shared native addresses: {duplicates}"

    popup_shows = {
        re.search(r'\bwin (0x[\da-f]+)\b', method["bindings"])[1]
        for item in inventory(directory)["classes"] if item["family"] == "popup"
        for method in item["methods"] if method["name"] == "show" and
        re.search(r'\bwin (0x[\da-f]+)\b', method["bindings"])
    }
    assert popup_shows <= hooks.keys(), f"Uncovered native popup show implementations: {popup_shows - hooks.keys()}"

    data = inventory(directory)
    saved = json.loads((ROOT / "docs/DYNAMIC_TRANSITION_BINDINGS.json").read_text())
    assert data == saved, "The UI inventory is out of date"
    assert markdown(data) == (ROOT / "docs/DYNAMIC_TRANSITION_BINDINGS.md").read_text()
    families = {item["name"]: item["family"] for item in data["classes"]}
    for name, expected in {
        "MoreOptionsLayer": "popup", "SetupTriggerPopup": "popup", "ProfilePage": "popup",
        "OptionsLayer": "dropdown", "EndLevelLayer": "dropdown", "RetryLevelLayer": "dropdown",
        "PauseLayer": "blocking", "DialogLayer": "dialog", "SlideInLayer": "dropdown",
        "LevelBrowserLayer": "browser_overlay", "GJGarageLayer": "scene", "LevelEditorLayer": "game_editor",
    }.items():
        assert families[name] == expected, f"Incorrect UI family for {name}"
    assert data["candidate_count"] == len(families)
    print(f'Dynamic Transition bindings checks passed ({count} hooks, {len(popup_shows)} native popup show implementations, '
          f'{data["candidate_count"]} inventoried candidates).')


if __name__ == "__main__":
    run()
