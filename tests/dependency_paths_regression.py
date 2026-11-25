"""Exercise external caches and the exact sources selected by CMake."""
import os
from pathlib import Path
import tempfile
from unittest.mock import patch

import run_regressions as runner
from dependency_paths import package_source


def touch(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("// fixture", encoding="utf-8")


with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    build = root / "build"
    cache = root / "external-cache"
    touch(cache / "fmt/old/include/fmt/format.h")
    touch(cache / "rapidfuzz/hash/rapidfuzz/fuzz.hpp")
    with patch.object(runner, "ROOT", root), patch.dict(os.environ, {"CPM_SOURCE_CACHE": str(cache)}):
        assert runner.fmt_include_path(build) == cache / "fmt/old/include"
        assert runner.rapidfuzz_include_path(build) == cache / "rapidfuzz/hash"

        # A restored cache may contain multiple SDK revisions: use the configured one.
        chosen = cache / "fmt/selected"
        touch(chosen / "include/fmt/format.h")
        build.mkdir(parents=True)
        (build / "CMakeCache.txt").write_text(
            "CPM_PACKAGE_fmt_SOURCE_DIR:INTERNAL=" + str(chosen) + "\n",
            encoding="utf-8")
        assert runner.fmt_include_path(build) == chosen / "include"

    # CMake's persisted cache remains usable without an inherited environment.
    with patch.dict(os.environ, {"CPM_SOURCE_CACHE": ""}):
        (build / "CMakeCache.txt").write_text(
            "CPM_SOURCE_CACHE:PATH=" + str(cache) + "\n", encoding="utf-8")
        assert package_source(build, root, "rapidfuzz", "rapidfuzz/fuzz.hpp") == cache / "rapidfuzz/hash"
        touch(build / "_deps/result-src/include/Geode/Result.hpp")
        assert package_source(build, root, "result", "include/Geode/Result.hpp") == build / "_deps/result-src"
        assert package_source(build, root, "missing", "include/missing.h") is None

print("PASS: external, configured and restored dependency sources")
