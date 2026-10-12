"""Check pause lifecycle hooks against Geode's generated bindings without building."""

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GENERATED = ROOT / "build-win/bindings/bindings/Geode/modify"
SOURCES = (
    "src/hooks/PauseLayer.cpp",
    "src/hooks/PlayLayer.cpp",
    "src/features/main-menu-layout/hooks/PauseLayerLayoutHook.cpp",
    "src/features/versus/hooks/VersusPauseLayer.cpp",
)
LIFECYCLE = {
    "init", "customSetup", "onEnter", "onExit", "pauseGame", "resume",
    "resumeAndRestart", "startGame", "onResume", "onRestart", "onRestartFull",
    "onNormalMode", "onPracticeMode", "onQuit", "destroyPlayer", "visit",
}


def modify_blocks(source):
    for match in re.finditer(r"class \$modify\(\w+,\s*(\w+)\)\s*\{", source):
        depth, end = 1, match.end()
        while depth and end < len(source):
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        yield match[1], source[match.end():end - 1]


def audit_source(source, label):
    checked = 0
    for base, body in modify_blocks(source):
        methods = set(re.findall(r"^    (?:bool|void) (\w+)\(", body, re.M)) & LIFECYCLE
        if not methods:
            continue
        header = (GENERATED / f"{base}.hpp").read_text()
        for method in sorted(methods):
            assert re.search(
                rf"GEODE_APPLY_MODIFY_FOR_FUNCTION\([^\n]*, {method},", header
            ), f"{label}: {base}::{method} has no generated hook; inherited overrides are ignored"
            checked += 1
    return checked


def run():
    checked = sum(audit_source((ROOT / path).read_text(encoding="utf-8-sig"), path)
                  for path in SOURCES)
    print(f"Pause lifecycle binding checks passed ({checked} native hooks).")


if __name__ == "__main__":
    run()
