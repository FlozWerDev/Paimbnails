"""Audit SDK headers and dependencies without configuring or compiling the mod."""

import argparse
import json
from pathlib import Path
import re
from build_paths import bindings_directory, build_directory


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return path.read_text(encoding="utf-8-sig")


def version(value):
    numeric = re.match(r"v?(\d+(?:\.\d+){0,2})", value.strip())[1]
    return tuple(int(part) for part in numeric.split("."))


def check_priority_targets(source, generated, label):
    checked = 0
    for match in re.finditer(r"(?:class|struct) \$modify\(([^)]+)\)\s*\{", source):
        base = match[1].split(",")[-1].strip()
        depth, end = 1, match.end()
        while depth and end < len(source):
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        body = source[match.end():end - 1]
        requests = re.findall(
            r'(?:setHookPriority\w*|afterNodeIdsOrLate|veryLatePost|afterModOrElseNodeIdsLate)'
            r'\(\s*(?:self\s*,\s*)?"([^"]+)"', body)
        if not requests and base != "Slider":
            continue
        native = read(generated / f"Geode/modify/{base}.hpp")
        # GD 2.2091: 0x84200 allocates/returns SliderTouchLogic. Treating it
        # as bool Slider::init corrupts the pointer consumed by Slider::create.
        if base == "Slider" and re.search(
            r"GEODE_APPLY_MODIFY_FOR_FUNCTION\([^\n]*0x84200[^\n]*, Slider, init,", native
        ):
            assert not re.search(r"\bbool\s+init\s*\(", body), \
                f"{label}: Slider::init has the wrong native ABI at 0x84200; hook Slider::create instead"
        for request in requests:
            method = request.split("::")[-1]
            assert re.search(rf"GEODE_APPLY_MODIFY_FOR_FUNCTION\([^\n]*, {method},", native), \
                f"{label}: {base} has no native hook for {request}"
            checked += 1
    return checked


def check_manifest(manifest):
    version = manifest["version"].lstrip("v")
    project = read(ROOT / "CMakeLists.txt")
    assert re.search(rf"project\(\w+ VERSION {re.escape(version)}\)", project), \
        f"CMake project version must match mod.json ({version})"

    # Geode extracts the mod icon as a lowercase root file on every platform.
    logos = sorted(p.name for p in ROOT.iterdir() if p.is_file() and p.name.lower() == "logo.png")
    assert logos == ["logo.png"], f"Mod icon must be exactly 'logo.png', found {logos}"
    sprites = manifest.get("resources", {}).get("sprites", [])
    assert "logo.png" in sprites, "mod.json must declare the lowercase logo.png sprite"

    for key, setting in manifest.get("settings", {}).items():
        if setting.get("type") != "keybind":
            continue
        assert setting.get("category") in ("universal", "gameplay", "editor"), \
            f"keybind setting '{key}' needs a universal, gameplay or editor category"


def check_source_rules():
    banned = re.compile(r"\b(?:std::(?:stod|stof|stoi|stol|stoll)|atof|atoi|atol)\s*\(")
    throwing = re.compile(r"\bthrow\s+[^;]+;")
    vendored = {"stb_image.h", "stb_image_write.h", "miniz.c", "miniz.h"}
    for path in (ROOT / "src").rglob("*"):
        if path.suffix not in (".cpp", ".hpp", ".mm", ".h"):
            continue
        if path.name in vendored:
            continue
        source = read(path)
        for pattern, message in (
            (banned, "use geode::utils::numFromString instead of"),
            (throwing, "Geode avoids throwing exceptions; return a Result instead of"),
        ):
            for match in pattern.finditer(source):
                line_start = source.rfind("\n", 0, match.start()) + 1
                if "//" in source[line_start:match.start()]:
                    continue
                line = source.count("\n", 0, match.start()) + 1
                raise AssertionError(
                    f"{path.relative_to(ROOT)}:{line}: {message} "
                    f"'{match.group(0).strip().rstrip('(').strip()}'"
                )


def run(sdk):
    manifest = json.loads(read(ROOT / "mod.json"))
    required = manifest["geode"]
    installed = read(sdk / "VERSION").strip()
    assert version(installed) >= version(required), f"SDK {installed} is older than {required}"
    assert version(installed)[:2] == version(required)[:2], "SDK minor version differs from mod.json"

    project_cmake = read(ROOT / "CMakeLists.txt")
    check_manifest(manifest)
    check_source_rules()
    sdk_cmake = read(sdk / "CMakeLists.txt")
    for dependency in ("GEODE_ARC_VERSION", "GEODE_ASP_VERSION"):
        upstream = re.search(rf'set\({dependency}\s+"([^"]+)"', sdk_cmake)[1]
        pinned = re.search(rf'set\({dependency}\s+"([^"]+)"[^\n]*FORCE\)', project_cmake)[1]
        assert pinned == upstream, f"{dependency}: {pinned} differs from SDK default {upstream}"
    required_numeric = ".".join(str(part) for part in version(required))
    assert f'VERSION_LESS "{required_numeric}"' in project_cmake
    workflow = read(ROOT / ".github/workflows/build.yml")
    assert re.search(r"(?m)^\s+sdk:\s*given\s*$", workflow), "CI must install the SDK named by mod.json"
    assert re.search(r"option\(GEODE_LINK_NIGHTLY [^\n]*\bOFF\)", project_cmake), \
        "Release import libraries must be linked unless nightly is requested explicitly"
    assert "--suite bindings --build-dir build" in workflow, "CI must audit generated bindings"

    sdk_headers = sdk / "loader/include"
    generated = build_directory() / "bindings/bindings"
    configured_bindings = bindings_directory()
    assert configured_bindings.name == "2.2091", \
        f"Expected GD 2.2091 bindings, got {configured_bindings.name}"
    header_roots = (sdk_headers, generated, configured_bindings.parent / "include")
    includes = set()
    bases = set()
    priority_targets = 0
    for path in (ROOT / "src").rglob("*"):
        if path.suffix not in (".cpp", ".hpp", ".mm", ".h"):
            continue
        source = read(path)
        priority_targets += check_priority_targets(source, generated, path.relative_to(ROOT))
        includes.update(re.findall(r"^\s*#include\s*[<\"](Geode/[^>\"]+)[>\"]", source, re.M))
        for args in re.findall(r"\$modify\(([^)]+)\)", source):
            bases.add(args.split(",")[-1].strip())
        assert not re.search(r"\bupdateDesignResolutionSize\s*\(", source), \
            f"{path}: updateDesignResolutionSize is protected in Geode 5.11"
        assert not re.search(r"formatter\s*<\s*matjson::Value\s*>", source), \
            f"{path}: Geode provides the matjson::Value formatter"
    for include in sorted(includes):
        assert any((directory / include).is_file() for directory in header_roots), \
            f"Missing SDK header: {include}"
    for base in sorted(bases):
        relative = f"Geode/modify/{base}.hpp"
        assert any((directory / relative).is_file() for directory in header_roots), \
            f"Missing modify binding: {base}"

    for header, signature in (
        ("Geode/modify/Modify.hpp", r"std::vector<Hook\*> getAllHooks\(\)"),
        ("Geode/loader/Hook.hpp", r"std::optional<std::string_view> getModifyClassName\(\) const"),
        ("Geode/cocos/platform/CCFileUtils.h", r"size_t GEODE_DLL getTexturePackCount\(\)"),
    ):
        assert re.search(signature, read(sdk_headers / header)), f"Missing Geode 5.11 API in {header}"

    print(f"PASS: Geode {installed}, {len(includes)} headers, {len(bases)} modify bases, "
          f"{priority_targets} priority targets, SDK dependencies and new APIs")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", type=Path, required=True)
    run(parser.parse_args().sdk)
