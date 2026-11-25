"""Build real CMake package targets and verify local deployment into a staging directory."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def run(*arguments):
    result = subprocess.run(arguments, capture_output=True, text=True, errors="replace")
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)


def verify_library_target_deployment(cmake):
    compiler = shutil.which("clang++") or shutil.which("g++")
    assert compiler, "A C++ compiler is required to verify deployment after linking"
    with tempfile.TemporaryDirectory(prefix="paimbnails-link-deploy-") as temporary:
        root = Path(temporary)
        source, build, mods = root / "source", root / "build", root / "mods with spaces"
        source.mkdir()
        mods.mkdir()
        (source / "mod.json").write_text('{"id":"test.quickhub"}', encoding="utf-8")
        (source / "package.geode").write_bytes(b"package target")
        cpp = source / "mod.cpp"

        def write_library(value):
            cpp.write_text('#ifdef _WIN32\n__declspec(dllexport)\n#endif\n'
                           f'int mod_value() {{ return {value}; }}\n', encoding="utf-8")

        write_library(7)
        fake_cli = source / "geode.py"
        fake_cli.write_text(
            'from pathlib import Path\nimport sys\n'
            'binary = Path(sys.argv[sys.argv.index("--binary") + 1])\n'
            'output = Path(sys.argv[sys.argv.index("--output") + 1])\n'
            'output.write_bytes(b"package after link:" + binary.read_bytes())\n', encoding="utf-8")
        launcher = source / ("geode.cmd" if os.name == "nt" else "geode")
        launcher.write_text(
            f'@"{sys.executable}" "{fake_cli}" %*\n' if os.name == "nt" else
            f'#!/bin/sh\nexec "{sys.executable}" "{fake_cli}" "$@"\n', encoding="utf-8")
        launcher.chmod(0o755)
        (source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.21)\nproject(LinkDeployTest CXX)\n'
            f'include("{(ROOT / "cmake/LocalModDeploy.cmake").as_posix()}")\n'
            'add_library(LiveDeploy SHARED mod.cpp)\n'
            'add_custom_target(LiveDeploy_PACKAGE ALL DEPENDS LiveDeploy\n'
            ' COMMAND "${CMAKE_COMMAND}" -E copy "${PROJECT_SOURCE_DIR}/package.geode"\n'
            ' "${CMAKE_CURRENT_BINARY_DIR}/test.quickhub.geode" VERBATIM)\n'
            'paimbnails_deploy_mod(LiveDeploy)\n', encoding="utf-8")
        run(cmake, "-S", str(source), "-B", str(build), "-G", "Ninja",
            f"-DCMAKE_CXX_COMPILER={compiler}", "-DCMAKE_BUILD_TYPE=Release",
            f"-DGEODE_CLI={launcher}", "-DGEODE_PACKAGE_ZSTD=ON",
            "-DPAIMBNAILS_DEPLOY_LOCAL_MOD=ON", f"-DPAIMBNAILS_MODS_DIRECTORY={mods}")
        installed = mods / "test.quickhub.geode"
        run(cmake, "--build", str(build), "--target", "LiveDeploy")
        assert installed.is_file(), "Building just the library must install the current mod"
        first_package = installed.read_bytes()
        assert first_package.startswith(b"package after link:")
        write_library(8)
        run(cmake, "--build", str(build), "--target", "LiveDeploy")
        assert installed.read_bytes() != first_package
        assert installed.read_bytes() == (build / "test.quickhub.geode").read_bytes()
        last_package = installed.read_bytes()
        run(cmake, "-S", str(source), "-B", str(build), "-DPAIMBNAILS_DEPLOY_LOCAL_MOD=OFF")
        write_library(9)
        run(cmake, "--build", str(build), "--target", "LiveDeploy")
        assert installed.read_bytes() == last_package


def main():
    cmake = shutil.which("cmake")
    assert cmake, "CMake is required to verify package deployment"
    verify_library_target_deployment(cmake)
    with tempfile.TemporaryDirectory(prefix="paimbnails-deploy-") as temporary:
        root = Path(temporary)
        source, build, mods = root / "source", root / "build", root / "mods with spaces"
        source.mkdir()
        (source / "mod.json").write_text('{"id":"test.quickhub"}', encoding="utf-8")
        seed = source / "package.geode"
        seed.write_bytes(b"first build")
        (source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.21)\nproject(DeployTest NONE)\n'
            'set(WIN32 TRUE)\n'  # Exercise Windows defaults on every CI host; no native code is built.
            f'include("{(ROOT / "cmake/LocalModDeploy.cmake").as_posix()}")\n'
            'add_custom_target(DeployTest_PACKAGE ALL\n'
            ' COMMAND "${CMAKE_COMMAND}" -E copy "${PROJECT_SOURCE_DIR}/package.geode"\n'
            ' "${CMAKE_CURRENT_BINARY_DIR}/test.quickhub.geode" VERBATIM)\n'
            'paimbnails_deploy_mod(DeployTest)\n', encoding="utf-8")
        run(cmake, "-S", str(source), "-B", str(build), "-G", "Ninja",
            f"-DPAIMBNAILS_MODS_DIRECTORY={mods}")
        cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
        assert "PAIMBNAILS_DEPLOY_LOCAL_MOD:BOOL=ON" in cache
        assert not mods.exists(), "The first build must create the mods directory"
        run(cmake, "--build", str(build))
        installed = mods / "test.quickhub.geode"
        assert installed.read_bytes() == b"first build"
        unrelated = mods / "another.mod.geode"
        unrelated.write_bytes(b"preserve this mod")
        seed.write_bytes(b"second build")
        run(cmake, "--build", str(build), "--target", "DeployTest_PACKAGE")
        assert installed.read_bytes() == b"second build"
        assert unrelated.read_bytes() == b"preserve this mod"
        run(cmake, "-S", str(source), "-B", str(build), "-DPAIMBNAILS_DEPLOY_LOCAL_MOD=OFF")
        seed.write_bytes(b"deployment disabled")
        run(cmake, "--build", str(build))
        assert installed.read_bytes() == b"second build"
        # Configure only: never copy a test mod into the real game's directory.
        run(cmake, "-S", str(source), "-B", str(build),
            "-DPAIMBNAILS_MODS_DIRECTORY:STRING=F:/SteamLibrary/steamapps/common/Geometry Dash/geode/mods")
        cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
        assert ("PAIMBNAILS_MODS_DIRECTORY:PATH=C:/Program Files (x86)/Steam/steamapps/common/"
                "Geometry Dash/geode/mods") in cache
        assert "PAIMBNAILS_DEPLOY_LOCAL_MOD:BOOL=OFF" in cache
    print("PASS: deployment after linking and packaging, directory creation, repeated builds, custom paths, migration and opt-out")


if __name__ == "__main__":
    main()
