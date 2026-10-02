"""Check Dynamic Transition settings and UI integration without building the mod."""

import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        assert key not in result, f"Duplicate JSON key: {key}"
        result[key] = value
    return result


def read(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")


def run():
    manifest = json.loads(read("mod.json"), object_pairs_hook=unique_object)
    setting = manifest["settings"]["dynamic-transition-enabled"]
    assert setting["type"] == "bool" and setting["default"] is True

    manager = read("src/features/transitions/services/DynamicTransitionManager.cpp")
    motion = read("src/features/transitions/services/DynamicTransitionMotion.hpp")
    defaults = read("src/core/SettingsMigration.hpp")
    reads = set(re.findall(
        r'(?:getSavedValue<[^>]+>|readEnum)\("(dynamic-transition-[^"]+)"', manager
    ))
    writes = set(re.findall(r'setSavedValue\("(dynamic-transition-[^"]+)"', manager))
    assert reads == writes, f"Settings read/write mismatch: {reads ^ writes}"
    registered = dict(re.findall(r'set[BIDF]\("(dynamic-transition-[^"]+)",\s*([^)]+)\)', defaults))
    assert reads <= registered.keys(), f"Missing reset defaults: {reads - registered.keys()}"
    assert "dynamic-transition-preset" not in registered
    assert '"dynamic-transition-preset"' not in manager

    config = re.search(r'struct Config \{(.*?)\n\};', motion, re.S)[1]
    fields = set(re.findall(r'^    \w+ (\w+) = ', config, re.M))
    loaded = set(re.findall(r'config\.(\w+) = ', manager.split('Config animationConfig')[0]))
    assert fields == loaded, f"Config fields not loaded: {fields ^ loaded}"
    popup = read("src/features/transitions/ui/DynamicTransitionConfigPopup.cpp")
    editable = set(re.findall(r'm_config\.(\w+) = ', popup))
    assert fields == editable, f"Config fields missing from the popup: {fields ^ editable}"

    for field, key in re.findall(
        r'config\.(\w+) = mod->getSavedValue<[^>]+>\("(dynamic-transition-[^"]+)"', manager
    ):
        match = re.search(rf'(?:bool|float) {field} = ([^;]+);', motion)
        assert match, f"Missing config default: {field}"
        declared = match[1].removesuffix("f")
        saved = registered[key].strip()
        if declared in ("true", "false"):
            assert saved == declared, f"Different reset default for {field}"
        else:
            assert float(saved) == float(declared), f"Different reset default for {field}"

    counts = dict(re.findall(r'inline constexpr int (\w+) = (\d+);', motion))
    for field, key, count in re.findall(
        r'config\.(\w+) = readEnum\("([^"]+)", config\.\w+, (\w+)\)', manager
    ):
        enum = re.search(rf'(\w+) {field} = \w+::\w+;', motion)[1]
        members = [member.strip() for member in re.search(
            rf'enum class {enum} \{{([^}}]+)\}}', motion
        )[1].split(",")]
        assert int(counts.get(count, count)) == len(members), f"Enum range mismatch for {field}"
        default = re.search(rf'{enum} {field} = {enum}::(\w+);', config)[1]
        assert int(registered[key]) == members.index(default), f"Invalid enum reset default for {field}"

    assert read("src/core/modules/ModuleCatalog.cpp").count('"paimbnails.dynamictransition.global"') == 1
    assert read("src/core/modules/ModuleLocalized.cpp").count('"paimbnails.dynamictransition.global"') == 2
    for entry in (
        "src/layers/PaimonHubLayer.cpp",
        "src/layers/PaiConfigLayer.cpp",
        "src/ui/SmoothUIConfigPopup.cpp",
    ):
        assert "DynamicTransitionConfigPopup::create()" in read(entry), f"Missing settings entry: {entry}"

    for path in (ROOT / "src/features/transitions").rglob("Dynamic*.*"):
        for include in re.findall(r'^#include "([^"]+)"', path.read_text(), re.MULTILINE):
            assert (path.parent / include).resolve().is_file(), f"Missing include: {path}: {include}"

    print(f"Dynamic Transition configuration checks passed ({len(reads)} saved settings).")


if __name__ == "__main__":
    run()
