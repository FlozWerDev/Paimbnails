"""Audit SDK headers and dependencies without configuring or compiling the mod."""

import argparse
import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return path.read_text(encoding="utf-8-sig")


def version(value):
    return tuple(int(part) for part in value.strip().split(".")[:3])


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
        if not requests:
            continue
        native = read(generated / f"Geode/modify/{base}.hpp")
        for request in requests:
            method = request.split("::")[-1]
            assert re.search(rf"GEODE_APPLY_MODIFY_FOR_FUNCTION\([^\n]*, {method},", native), \
                f"{label}: {base} has no native hook for {request}"
            checked += 1
    return checked


def run(sdk):
    manifest = json.loads(read(ROOT / "mod.json"))
    required = manifest["geode"]
    installed = read(sdk / "VERSION").strip()
    assert version(installed) >= version(required), f"SDK {installed} is older than {required}"
    assert version(installed)[:2] == version(required)[:2], "SDK minor version differs from mod.json"

    project_cmake = read(ROOT / "CMakeLists.txt")
    sdk_cmake = read(sdk / "CMakeLists.txt")
    for dependency in ("GEODE_ARC_VERSION", "GEODE_ASP_VERSION"):
        upstream = re.search(rf'set\({dependency}\s+"([^"]+)"', sdk_cmake)[1]
        pinned = re.search(rf'set\({dependency}\s+"([^"]+)"[^\n]*FORCE\)', project_cmake)[1]
        assert pinned == upstream, f"{dependency}: {pinned} differs from SDK default {upstream}"
    assert f'VERSION_LESS "{required}"' in project_cmake
    assert "sdk: given" in read(ROOT / ".github/workflows/build.yml")

    sdk_headers = sdk / "loader/include"
    generated = ROOT / "build-win/bindings/bindings"
    header_roots = (sdk_headers, generated, ROOT / "build-win/_deps/bindings-src/bindings/include")
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
