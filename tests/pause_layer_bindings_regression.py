"""Check pause lifecycle hooks against Geode's generated bindings without building."""

import argparse
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
GENERATED = None
LIFECYCLE = {
    "init", "customSetup", "onEnter", "onExit", "pauseGame", "resume",
    "resumeAndRestart", "startGame", "onResume", "onRestart", "onRestartFull",
    "onNormalMode", "onPracticeMode", "onQuit", "destroyPlayer", "visit",
    "tryQuit", "onTryEdit", "onEdit", "goEdit", "keyBackClicked", "keyDown",
    "onExitEditor", "onExitNoSave", "saveLevel", "onPlaytest", "onStopPlaytest",
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
    global GENERATED
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    args = parser.parse_args()
    candidates = [args.build_dir] if args.build_dir else [
        ROOT / "build-clang", ROOT / "build-win", ROOT / "build"
    ]
    GENERATED = next((path / "bindings/bindings/Geode/modify" for path in candidates
                      if (path / "bindings/bindings/Geode/modify/PauseLayer.hpp").is_file()), None)
    if GENERATED is None:
        raise FileNotFoundError("Configure the mod first, or provide --build-dir")
    sources = []
    for path in sorted((ROOT / "src").rglob("*.cpp")):
        source = path.read_text(encoding="utf-8-sig")
        if any(base in {"PauseLayer", "PlayLayer", "EditorPauseLayer", "LevelEditorLayer"}
               for base, _ in modify_blocks(source)):
            sources.append((path, source))
    # Lifecycle is inherited from CCLayer, whose hooks live alongside PauseLayer's.
    checked = sum(audit_source(source, path.relative_to(ROOT)) for path, source in sources)
    print(f"Pause lifecycle binding checks passed ({checked} native hooks, {len(sources)} source files).")


if __name__ == "__main__":
    run()
