"""Compile and exercise the real local Shop protocol and storage with sanitizers."""
import os
from pathlib import Path
import subprocess
import tempfile

from dependency_paths import package_source, json_compile_inputs
from run_regressions import add_sanitizer_runtime

ROOT = Path(__file__).resolve().parents[1]

def main():
    build = Path(os.environ.get("PAIMBNAILS_BUILD_DIR", ROOT / "build-clang"))
    json = package_source(build, ROOT, "json", "include/matjson.hpp")
    result = package_source(build, ROOT, "result", "include/Geode/Result.hpp")
    fmt = package_source(build, ROOT, "fmt", "include/fmt/format.h")
    assert json and result and fmt, "Configure the CMake dependencies first"
    compiler = os.environ.get("PAIMBNAILS_SHOP_CXX", "clang++")
    output = ROOT / "build-regressions/paimon-shop"
    output.mkdir(parents=True, exist_ok=True)
    production = (ROOT / "src/features/paimon-shop/services/ShopConfiguration.cpp").read_text()
    production = "\n".join(line for line in production.splitlines() if not line.startswith("#include"))
    fixture = (ROOT / "tests/paimon_shop_configuration_fixture.cpp").read_text()
    generated = output / "configuration.cpp"
    generated.write_text(fixture.replace("@SHOP_CONFIGURATION_SOURCE@", production))
    binary = output / ("shop.exe" if os.name == "nt" else "shop")
    sources = [ROOT / "tests/paimon_shop_regression.cpp",
               ROOT / "src/features/paimon-shop/data/ShopPreset.cpp",
               ROOT / "src/features/paimon-shop/services/ShopLibrary.cpp",
               ROOT / "src/utils/AtomicFileWrite.cpp"]
    json_includes, json_sources = json_compile_inputs(build, ROOT)
    sources += [generated, *json_sources]
    command = [compiler, "-std=c++23", "-g", "-Wall", "-Wextra", "-Wpedantic",
               "-fsanitize=address,undefined", "-I" + str(ROOT / "tests/stubs"),
               "-I" + str(ROOT), "-isystem", str(fmt / "include"),
               *[flag for include in json_includes for flag in ("-isystem", str(include))],
               "-isystem", str(result / "include"),
               *map(str, sources), "-o", str(binary)]
    subprocess.run(command, check=True)
    environment = os.environ.copy()
    add_sanitizer_runtime(environment, compiler)
    with tempfile.TemporaryDirectory(prefix="paimon-shop-") as temporary:
        subprocess.run([str(binary), temporary], env=environment, check=True)

if __name__ == "__main__":
    main()
