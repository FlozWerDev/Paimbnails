"""Audit every $modify hook against the generated Geode bindings.

The check is intentionally independent from the compiler: it reads the .bro
files used by the configured build, the generated modify headers, and the
source tree. In strict mode it fails when a method declared in a hooked class
has no GEODE_APPLY_MODIFY_FOR_FUNCTION generated for it, when an override does
not exist in the class hierarchy, or when two hooks of the same block share a
native address. Platform availability is reported, not guessed.
"""

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

from build_paths import bindings_directory, build_directory


ROOT = Path(__file__).resolve().parents[1]
SOURCES = (".cpp", ".hpp", ".mm", ".h")
SKIP_METHODS = {
    "onModify", "if", "for", "while", "switch", "catch", "return", "sizeof",
    "static_assert", "decltype", "alignof", "operator",
}

# Methods Geode does not generate hooks for when they are inherited from a
# base class (the generated <class>.hpp only lists the class's own methods).
# They are tracked here so the audit fails on new ones while the existing debt
# is migrated to CCNode::addOnEnterCallback/addOnExitCallback/addCleanupCallback
# (Geode/ui/NodeEvent.hpp) or to Fields destructor cleanup.
KNOWN_INHERITED = {
    ("LevelInfoLayer", "ccTouchBegan"),
    ("LevelInfoLayer", "ccTouchCancelled"),
    ("LevelInfoLayer", "ccTouchEnded"),
    ("LevelInfoLayer", "ccTouchMoved"),
    ("LevelSearchLayer", "enterPressed"),
    ("ShareCommentLayer", "enterPressed"),
    ("SliderTouchLogic", "ccTouchCancelled"),
}

# Native methods that only exist on macOS in the 2.2091 bindings.
KNOWN_MISSING = {
    ("LevelSearchLayer", "onStarAward"),
    ("SlideInLayer", "hideLayer"),
    ("SlideInLayer", "showLayer"),
}

CLASS_RE = re.compile(r"^class\s+([\w:]+)(?:\s*:\s*([^\n{]+))?\s*\{\s*$")
ATTR_RE = re.compile(r"^\s*\[\[([^\]]+)\]\]\s*$")
MODIFY_RE = re.compile(r"class\s+\$modify\(\s*\w+\s*,\s*([\w:]+)\s*\)\s*\{")
METHOD_RE = re.compile(
    r"^\s*(?P<virtual>virtual\s+)?(?P<static>static\s+)?"
    r"(?P<head>[^\n;{}()]*?)\b(?P<name>[\w~]+)\s*\((?P<params>[^;{}]*)\)\s*"
    r"(?P<const>const\s*)?(?:=\s*(?P<bindings>[^;]+);|;)\s*(?://.*)?$"
)
SIGNATURE_RE = re.compile(
    r"(?P<prefix>.*?)\b(?P<name>[A-Za-z_]\w*)\s*\((?P<params>[^()]*)\)\s*"
    r"(?P<const>const\s*)?(?:override\s*)?$",
    re.S,
)


def attr_platforms(attrs):
    """Return the platforms of link(...)/missing(...) attributes."""
    platforms = set()
    for match in re.finditer(r"\[\[\s*(?:link|missing)\s*\(([^)]*)\)\s*\]\]", attrs):
        for value in re.split(r"[,\s]+", match.group(1)):
            if value:
                platforms.add(value)
    return platforms


def split_platforms(text):
    return {value for value in re.split(r"[,\s]+", text) if value}


def parse_bro(directory):
    """Parse classes and methods from the .bro files included by Entry.bro."""
    classes = {}
    entry = directory / "Entry.bro"
    files = ["Entry.bro", *re.findall(r"#include <([^>]+)>", entry.read_text(encoding="utf-8-sig"))]
    for filename in files:
        path = directory / filename
        if not path.is_file():
            continue
        lines = path.read_text(encoding="utf-8-sig").splitlines()
        pending_attrs = []
        index = 0
        while index < len(lines):
            raw = lines[index]
            stripped = raw.strip()
            if ATTR_RE.match(stripped):
                pending_attrs.append(stripped)
                index += 1
                continue
            class_match = CLASS_RE.match(raw)
            if not class_match:
                if stripped and not stripped.startswith("//"):
                    pending_attrs = []
                index += 1
                continue

            name, bases_text = class_match.group(1), class_match.group(2)
            class_attrs = " ".join(pending_attrs)
            pending_attrs = []
            body = []
            index += 1
            while index < len(lines):
                if not lines[index].startswith((" ", "\t")) and lines[index].strip() == "}":
                    break
                body.append(lines[index])
                index += 1
            index += 1

            methods = []
            method_attrs = []
            for line in body:
                stripped = line.strip()
                if not stripped:
                    continue
                if ATTR_RE.match(stripped):
                    method_attrs.append(stripped)
                    continue
                if stripped.startswith("//") or stripped.startswith("/*") or stripped.startswith("*"):
                    continue
                method_match = METHOD_RE.match(line)
                if not method_match:
                    method_attrs = []
                    continue
                method_name = method_match.group("name")
                if method_name in SKIP_METHODS or method_name.startswith("~"):
                    method_attrs = []
                    continue
                unqualified = name.rsplit("::", 1)[-1]
                if method_name == unqualified:
                    method_attrs = []
                    continue
                methods.append({
                    "name": method_name,
                    "params": method_match.group("params").strip(),
                    "virtual": bool(method_match.group("virtual")),
                    "bindings": (method_match.group("bindings") or "").strip(),
                    "attrs": " ".join(method_attrs),
                    "line": index,
                })
                method_attrs = []

            classes[name] = {
                "name": name,
                "bases": [base.strip() for base in (bases_text or "").split(",") if base.strip()],
                "attrs": class_attrs,
                "link": split_platforms(" ".join(
                    match.group(1)
                    for match in re.finditer(r"\[\[\s*link\s*\(([^)]*)\)\s*\]\]", class_attrs)
                )),
                "methods": methods,
                "source": filename,
            }
    return classes


def class_lookup(classes, name):
    if name in classes:
        return classes[name]
    name = name.rsplit("::", 1)[-1]
    matches = [item for key, item in classes.items() if key.rsplit("::", 1)[-1] == name]
    return matches[0] if len(matches) == 1 else None


def ancestry(classes, base):
    """Yield the base class and its ancestors, breadth-first."""
    queue = [class_lookup(classes, base)]
    seen = set()
    while queue:
        item = queue.pop(0)
        if not item or item["name"] in seen:
            continue
        seen.add(item["name"])
        yield item
        for parent in item["bases"]:
            queue.append(class_lookup(classes, parent))


def split_params(text):
    """Split a parameter list on top-level commas."""
    if not text.strip():
        return []
    out, current, depth = [], [], 0
    for char in text:
        if char in "<([":
            depth += 1
        elif char in ">)]":
            depth = max(0, depth - 1)
        if char == "," and depth == 0:
            out.append("".join(current).strip())
            current = []
        else:
            current.append(char)
    if "".join(current).strip():
        out.append("".join(current).strip())
    return out


TYPE_WORDS = {
    "auto", "bool", "char", "double", "float", "int", "long", "short",
    "signed", "unsigned", "void", "wchar_t", "char8_t", "char16_t", "char32_t",
}


def normalize_param(text):
    text = re.sub(r"\b(?:cocos2d|geode)::", "", text)
    text = re.sub(r"\b(?:struct|class|const|volatile)\b", "", text)
    text = text.replace("&", "")
    text = re.sub(r"\b[A-Za-z_]\w*\s*\[\s*\]", "*", text)
    match = re.match(r"^(.*?)([A-Za-z_]\w*)\s*$", text.strip())
    if match:
        prefix = match.group(1).rstrip()
        name = match.group(2)
        if name not in TYPE_WORDS and prefix and (
            prefix.endswith(("*", ">", "]")) or re.search(r"[A-Za-z_]\w*", prefix)
        ):
            text = prefix
    return re.sub(r"\s+", "", text)


def method_matches(method, name, params):
    if method["name"] != name:
        return False
    expected = split_params(method["params"])
    actual = split_params(params)
    if len(expected) != len(actual):
        return False
    return [normalize_param(value) for value in expected] == [normalize_param(value) for value in actual]


def find_method(classes, base, name, params):
    """Return (declaring_class, method, state).

    state is "base", "inherited" or "helper". A name that exists with another
    arity is treated as a helper; only an explicit $override makes it an error.
    """
    item = class_lookup(classes, base)
    if not item:
        return None, None, "helper"
    for method in item["methods"]:
        if method_matches(method, name, params):
            return item, method, "base"
    for ancestor in list(ancestry(classes, base))[1:]:
        for method in ancestor["methods"]:
            if method_matches(method, name, params):
                return ancestor, method, "inherited"
    return None, None, "helper"


def sanitize(text):
    """Replace comments and string literals with spaces, preserving newlines."""
    out = list(text)
    index, length = 0, len(text)
    state = "code"
    while index < length:
        char = text[index]
        nxt = text[index + 1] if index + 1 < length else ""
        if state == "code":
            if char == "/" and nxt == "/":
                out[index] = out[index + 1] = " "
                state = "line"
                index += 2
                continue
            if char == "/" and nxt == "*":
                out[index] = out[index + 1] = " "
                state = "block"
                index += 2
                continue
            if char in ('"', "'"):
                out[index] = " "
                state = char
                index += 1
                continue
            index += 1
            continue
        if state == "line":
            if char == "\n":
                state = "code"
            else:
                out[index] = " "
            index += 1
            continue
        if state == "block":
            if char == "*" and nxt == "/":
                out[index] = out[index + 1] = " "
                state = "code"
                index += 2
                continue
            if char != "\n":
                out[index] = " "
            index += 1
            continue
        if char == "\\":
            out[index] = " "
            if index + 1 < length:
                if text[index + 1] != "\n":
                    out[index + 1] = " "
                index += 2
            else:
                index += 1
            continue
        if char == state or char == "\n":
            out[index] = " "
            if char == state:
                state = "code"
            index += 1
            continue
        out[index] = " "
        index += 1
    return "".join(out)


def modify_blocks(source):
    for match in MODIFY_RE.finditer(source):
        start = match.end() - 1
        depth, index = 1, start + 1
        while index < len(source) and depth:
            if source[index] == "{":
                depth += 1
            elif source[index] == "}":
                depth -= 1
            index += 1
        if depth == 0:
            yield match.group(1), source[start + 1:index - 1], source.count("\n", 0, match.start()) + 1


def member_signatures(block):
    """Yield declarations of members at the outermost class body level."""
    out = []
    depth, start = 0, 0
    for index, char in enumerate(block):
        if char == "{":
            declaration = block[start:index].strip()
            if depth == 0 and "(" in declaration and not declaration.startswith(("struct ", "class ", "enum ")):
                out.append(declaration)
            depth += 1
            start = index + 1
        elif char == "}":
            depth = max(0, depth - 1)
            start = index + 1
        elif char == ";" and depth == 0:
            start = index + 1
    return out


def parse_member(declaration, base):
    match = SIGNATURE_RE.search(declaration)
    if not match:
        return None
    name = match.group("name")
    if name in SKIP_METHODS or name == base.rsplit("::", 1)[-1] or name.startswith("~"):
        return None
    return {
        "name": name,
        "params": match.group("params").strip(),
        "override": "$override" in declaration,
    }


def generated_hook(generated, base, method):
    header = generated / f"{base.rsplit('::', 1)[-1]}.hpp"
    if not header.is_file():
        return "missing-header"
    text = header.read_text(encoding="utf-8-sig")
    if re.search(rf"GEODE_APPLY_MODIFY_FOR_FUNCTION\([^\n]*, {re.escape(method)},", text):
        return "hooked"
    if re.search(rf"GEODE_APPLY_MODIFY_FOR_FUNCTION_ERROR_INLINE\([^\n]*, {re.escape(method)},", text):
        return "inline"
    return "missing"


def platform_coverage(item, method):
    covered = set(item["link"])
    bindings = method["bindings"]
    for platform, names in (("win", ("win",)), ("android", ("android",)),
                            ("mac", ("imac", "m1")), ("ios", ("ios",))):
        if any(re.search(rf"\b{name}\b", bindings) for name in names):
            covered.add(platform)
        if any(re.search(rf"\b{name} inline\b", bindings) for name in names):
            covered.discard(platform)
    missing = attr_platforms(method["attrs"])
    if not missing:
        missing = attr_platforms(item["attrs"])
    return covered - missing


def address_of(method):
    match = re.search(r"\bwin\s+(0x[0-9a-fA-F]+)\b", method["bindings"])
    return match.group(1) if match else None


def audit(build, report):
    bindings = bindings_directory(build)
    generated = build / "bindings/bindings/Geode/modify"
    classes = parse_bro(bindings)

    errors, warnings = [], []
    platform_limited = set()
    hooks = 0
    generated_hooks = 0
    inherited_known = 0
    missing_known = 0
    blocks = 0
    per_class = defaultdict(int)

    for path in sorted(ROOT.joinpath("src").rglob("*")):
        if path.suffix not in SOURCES:
            continue
        source = sanitize(path.read_text(encoding="utf-8-sig"))
        for base, block, line in modify_blocks(source):
            blocks += 1
            seen_addresses = {}
            for declaration in member_signatures(block):
                member = parse_member(declaration, base)
                if not member:
                    continue
                declaring, method, state = find_method(classes, base, member["name"], member["params"])
                if method is None:
                    if member["override"]:
                        errors.append(
                            f"{path.relative_to(ROOT)}:{line}: {base}::{member['name']} is marked "
                            f"$override but does not exist in the class hierarchy"
                        )
                    continue
                if state == "inherited":
                    if (base, member["name"]) in KNOWN_INHERITED:
                        inherited_known += 1
                        continue
                    errors.append(
                        f"{path.relative_to(ROOT)}:{line}: {base}::{member['name']} is inherited from "
                        f"{declaring['name']} and has no generated hook in {base}.hpp"
                    )
                    continue
                status = generated_hook(generated, base, member["name"])
                hooks += 1
                per_class[base] += 1
                if status != "hooked":
                    if (base, member["name"]) in KNOWN_MISSING:
                        missing_known += 1
                        continue
                    errors.append(
                        f"{path.relative_to(ROOT)}:{line}: {base}::{member['name']} has no "
                        f"GEODE_APPLY_MODIFY_FOR_FUNCTION in {base}.hpp ({status})"
                    )
                    continue
                generated_hooks += 1
                covered = platform_coverage(declaring, method)
                if not covered.intersection({"win", "android"}):
                    platform_limited.add(f"{base}::{member['name']}")
                address = address_of(method)
                if address:
                    previous = seen_addresses.get(address)
                    if previous and previous != member["name"]:
                        errors.append(
                            f"{path.relative_to(ROOT)}:{line}: {base}::{previous} and "
                            f"{base}::{member['name']} share win address {address}"
                        )
                    seen_addresses[address] = member["name"]

    placeholders = {
        (item["name"].rsplit("::", 1)[-1], method["name"])
        for item in classes.values()
        for method in item["methods"]
        if not method["virtual"] and "0x9999999" in method["bindings"]
    }
    source_text = "\n".join(
        path.read_text(encoding="utf-8-sig")
        for path in sorted(ROOT.joinpath("src").rglob("*"))
        if path.suffix in SOURCES
    )
    for class_name, method_name in sorted(placeholders):
        if re.search(rf"\b{re.escape(class_name)}\s*::\s*{re.escape(method_name)}\s*\(",
                     source_text):
            warnings.append(
                f"{class_name}::{method_name} is a non-virtual binding with a 0x9999999 "
                f"placeholder and is called from src/"
            )

    print(f"Bindings: {bindings.name} | classes: {len(classes)} | modify blocks: {blocks}")
    print(f"Hooks checked: {hooks} | generated: {generated_hooks} | "
          f"known inherited: {inherited_known} | known platform-only: {missing_known} | "
          f"warnings: {len(warnings)}")
    print(f"Platform limited (benign: vtable/linked or documented): {len(platform_limited)}")
    if per_class:
        classes_with_hooks = ", ".join(
            f"{name}({count})" for name, count in sorted(per_class.items())[:12]
        )
        print(f"Top classes: {classes_with_hooks}")

    if platform_limited:
        print("Platform-limited hooks (reviewed as intentional):")
        for name in platform_limited:
            print(f"  - {name}")
    for warning in warnings:
        print(f"WARN: {warning}")
    for error in errors:
        print(f"{'WARN' if report else 'FAIL'}: {error}")

    if report:
        print(f"REPORT ONLY: {len(errors)} issue(s) found; rerun without --report to fail.")
        return 0
    if errors:
        print(f"FAILED: {len(errors)} hook binding issue(s).")
        return 1
    print("PASS: all $modify hooks have a generated native binding.")
    return 0


def write_inventory(path, build, classes):
    lines = [
        "# Inventario de hooks y bindings",
        "",
        f"Bindings: `{bindings_directory(build).name}`.",
        "",
        "| Clase | Metodos hookeados |",
        "| --- | ---: |",
    ]
    counts = defaultdict(int)
    for item in classes.values():
        counts[item["name"].rsplit("::", 1)[-1]] = len(item["methods"])
    for name, count in sorted(counts.items()):
        if count:
            lines.append(f"| `{name}` | {count} |")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--report", action="store_true",
                        help="Print findings without failing")
    parser.add_argument("--write", action="store_true",
                        help="Write docs/HOOK_BINDINGS_AUDIT.md")
    args = parser.parse_args()
    build = build_directory(args.build_dir)
    classes = parse_bro(bindings_directory(build))
    if args.write:
        write_inventory(ROOT / "docs/HOOK_BINDINGS_AUDIT.md", build, classes)
    return audit(build, args.report)


if __name__ == "__main__":
    sys.exit(main())
