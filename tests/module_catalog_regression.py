"""Check that every catalog entry is wired to valid settings and parents."""

import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "src/core/modules/ModuleCatalog.cpp"
ACCESSORS = ROOT / "src/core/modules/ModuleAccessors.cpp"
SETTINGS = ROOT / "mod.json"
HELPERS = {"setting", "saved", "custom", "infoMod"}
ALIASES = {
    "kThumbs": "paimbnails.thumbnails.browser",
    "kInfoSuite": "paimbnails.infosuite.info",
    "kPerformance": "paimbnails.performance.gameplay",
    "kIconGradients": "paimbnails.icongradients.global",
    "kNone": "",
}


def skip_string(source, start):
    index = start + 1
    while index < len(source):
        if source[index] == "\\":
            index += 2
        elif source[index] == '"':
            return index + 1
        else:
            index += 1
    raise AssertionError("unterminated C++ string in module catalog")


def split_arguments(source):
    arguments = []
    start = 0
    depth = 0
    index = 0
    while index < len(source):
        char = source[index]
        if char == '"':
            index = skip_string(source, index)
            continue
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
        elif char == "," and depth == 0:
            arguments.append(source[start:index].strip())
            start = index + 1
        index += 1
    arguments.append(source[start:].strip())
    return arguments


def catalog_calls(source):
    marker = "std::vector<Module> buildCatalog()"
    start = source.index(marker)
    body_start = source.index("return {", start) + len("return {")
    body_end = source.index("\n    };", body_start)
    body = source[body_start:body_end]
    calls = []
    index = 0
    while index < len(body):
        if body.startswith("//", index):
            newline = body.find("\n", index)
            index = len(body) if newline < 0 else newline + 1
            continue
        if body.startswith("/*", index):
            end = body.find("*/", index + 2)
            if end < 0:
                raise AssertionError("unterminated C++ comment in module catalog")
            index = end + 2
            continue
        if body[index] == '"':
            index = skip_string(body, index)
            continue

        match = re.match(r"[A-Za-z_]\w*", body[index:])
        if not match:
            index += 1
            continue
        name = match.group(0)
        after_name = index + len(name)
        paren = after_name
        while paren < len(body) and body[paren].isspace():
            paren += 1
        if name not in HELPERS or paren == len(body) or body[paren] != "(":
            index = after_name
            continue

        depth = 1
        end = paren + 1
        while end < len(body) and depth:
            if body[end] == '"':
                end = skip_string(body, end)
                continue
            if body[end] == "(":
                depth += 1
            elif body[end] == ")":
                depth -= 1
            end += 1
        if depth:
            raise AssertionError(f"unterminated {name} call in module catalog")
        calls.append((name, split_arguments(body[paren + 1:end - 1])))
        index = end
    return calls


def literal(argument):
    match = re.fullmatch(r'"((?:[^"\\]|\\.)*)"', argument.strip())
    if not match:
        raise AssertionError(f"expected a string literal, got {argument!r}")
    return bytes(match.group(1), "utf-8").decode("unicode_escape")


def resolve(argument):
    value = argument.strip()
    if value.startswith('"'):
        value = literal(value)
    return ALIASES.get(value, value)


def parent_argument(kind, arguments):
    default_index = {"setting": 7, "saved": 7, "custom": 6, "infoMod": 6}[kind]
    if len(arguments) > default_index:
        return resolve(arguments[default_index])
    return "paimbnails.infosuite.info" if kind == "infoMod" else ""


def main():
    source = CATALOG.read_text(encoding="utf-8")
    settings = json.loads(SETTINGS.read_text(encoding="utf-8"))["settings"]
    calls = catalog_calls(source)
    modules = []
    for kind, arguments in calls:
        if len(arguments) < 2:
            raise AssertionError(f"incomplete {kind} catalog entry")
        module_id = resolve(arguments[0])
        name = literal(arguments[1])
        parent = parent_argument(kind, arguments)
        key = literal(arguments[3]) if kind in ("setting", "saved", "infoMod") else ""
        modules.append((kind, module_id, name, key, parent))

    ids = [module[1] for module in modules]
    assert len(ids) == len(set(ids)), "module IDs must be unique"
    id_set = set(ids)
    custom_ids = {module_id for kind, module_id, *_ in modules if kind == "custom"}
    accessor_ids = set(re.findall(r'registerAccessor\("([^"]+)"', ACCESSORS.read_text(encoding="utf-8")))
    assert custom_ids == accessor_ids, (
        f"custom accessor mismatch; missing={sorted(custom_ids - accessor_ids)}, "
        f"orphaned={sorted(accessor_ids - custom_ids)}"
    )

    for kind, module_id, name, key, parent in modules:
        assert module_id.startswith("paimbnails."), f"invalid ID: {module_id}"
        assert name.strip(), f"empty name: {module_id}"
        assert not parent or parent in id_set, f"unknown parent {parent}: {module_id}"
        if kind in ("setting", "infoMod"):
            assert key in settings, f"missing mod.json setting {key}: {module_id}"
            definition = settings[key]
            assert definition.get("type") == "bool", f"non-boolean module setting {key}: {module_id}"
            assert isinstance(definition.get("default"), bool), f"missing boolean default {key}: {module_id}"
        elif kind == "saved":
            assert key, f"empty saved-value key: {module_id}"

        visited = {module_id}
        current = parent
        while current:
            assert current not in visited, f"parent cycle at {current}"
            visited.add(current)
            owner = next(module for module in modules if module[1] == current)
            current = owner[4]

    print(
        f"PASS: {len(modules)} module entries; unique IDs, valid parents, "
        f"{len(custom_ids)} custom accessors, and all Setting keys mapped to booleans"
    )


if __name__ == "__main__":
    main()
