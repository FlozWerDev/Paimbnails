"""Locate the dependency sources prepared by CMake/CPM or the Geode action."""
import os
from pathlib import Path


def package_source(build, project_root, package, required_file):
    build, project_root = Path(build), Path(project_root)
    caches = []
    configured_sources = []
    if external := os.environ.get("CPM_SOURCE_CACHE"):
        caches.append(Path(external).expanduser())
    # CMake may have a cache path even when this process did not inherit its environment.
    cache_file = build / "CMakeCache.txt"
    if cache_file.is_file():
        for line in cache_file.read_text(encoding="utf-8-sig").splitlines():
            key, separator, value = line.partition("=")
            if not separator:
                continue
            key = key.split(":", 1)[0].casefold()
            if key == "cpm_source_cache":
                caches.append(Path(value).expanduser())
            elif key in (f"cpm_package_{package}_source_dir", f"{package}_source_dir"):
                configured_sources.append(Path(value))
    caches += [project_root / "cpm-cache", build / "cpm-cache", project_root / "build/cpm-cache"]
    candidates = [*configured_sources, build / "_deps" / (package + "-src"),
                  project_root / "build/_deps" / (package + "-src")]
    for cache in dict.fromkeys(caches):
        candidates += sorted((cache / package).glob("*"))
    return next((source for source in candidates if (source / required_file).is_file()), None)


def json_compile_inputs(build, project_root):
    """Use the configured matjson implementation, including its private dependencies."""
    source = package_source(build, project_root, "json", "include/matjson.hpp")
    if not source:
        raise RuntimeError("Configure the matjson dependency before running native fixtures")
    includes = [source / "include"]
    sources = sorted((source / "src").glob("*.cpp"))
    for relative in ("src/external/dragonbox.cpp", "src/external/zmij.cc"):
        if (source / relative).is_file():
            sources.append(source / relative)
    if (source / "src/external/zmij.cc").is_file():
        ordered_map = package_source(build, project_root, "ordered-map", "include/tsl/ordered_map.h")
        if not ordered_map:
            raise RuntimeError("matjson 4 requires the configured ordered-map headers")
        includes.append(ordered_map / "include")
    return includes, sources
