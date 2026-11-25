"""Run outstanding physics audit reproductions, independently of passing regressions.

Exit 1 means a reproduced defect remains. Exit 2 means compilation/execution failed.
This does not build, package, or install the mod.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cxx", default=os.environ.get("CXX"))
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--measure-budget", action="store_true")
    args = parser.parse_args()
    compiler = args.cxx or shutil.which("clang-cl") or shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        parser.error("No C++ compiler found; pass --cxx.")
    cl_style = Path(compiler).stem.lower() in ("cl", "clang-cl")
    environment = os.environ.copy()
    if args.sanitize:
        environment["ASAN_OPTIONS"] = environment.get("ASAN_OPTIONS", "") + ":exitcode=86"
        environment["UBSAN_OPTIONS"] = environment.get("UBSAN_OPTIONS", "") + ":halt_on_error=1:exitcode=87"
    if args.sanitize and os.name == "nt":
        compiler_file = Path(shutil.which(compiler) or compiler).resolve()
        runtimes = compiler_file.parent.parent / "lib/clang"
        for runtime in sorted(runtimes.glob("*/lib/windows"), reverse=True):
            if list(runtime.glob("clang_rt.asan_dynamic-*.dll")):
                environment["PATH"] = str(runtime) + os.pathsep + environment.get("PATH", "")
                break
    sources = {
        "physics_audit": [
            ROOT / "src/features/editor-physics/services/PhysicsSolver.cpp",
            ROOT / "src/features/editor-physics/services/NativePreview.cpp",
            ROOT / "src/features/editor-physics/PhysicsNative.cpp",
        ],
        "menu_physics_audit": [],
    }
    defects = False
    with tempfile.TemporaryDirectory(prefix="paimbnails-physics-audit-") as temporary:
        output = Path(temporary)
        for name, units in sources.items():
            executable = output / (name + (".exe" if os.name == "nt" else ""))
            inputs = [str(HERE / (name + ".cpp")), *map(str, units)]
            if cl_style:
                command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/W4", "/O2",
                           "/I" + str(ROOT / "tests/stubs")]
                if args.sanitize:
                    command += ["/Zi", "/fsanitize=address", "/clang:-fsanitize=undefined"]
                command += inputs + ["/Fo:" + str(output) + os.sep, "/Fe:" + str(executable)]
            else:
                command = [compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Wpedantic",
                           "-I" + str(ROOT / "tests/stubs")]
                if args.sanitize:
                    command += ["-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
                command += inputs + ["-o", str(executable)]
            print("Compile:", name, flush=True)
            try:
                compiled = subprocess.run(command, cwd=output, env=environment, timeout=120)
                if compiled.returncode != 0:
                    return 2
                arguments = [str(executable)]
                if name == "physics_audit" and args.measure_budget:
                    arguments.append("--measure-budget")
                result = subprocess.run(arguments, cwd=ROOT, env=environment, timeout=60)
            except (OSError, subprocess.TimeoutExpired) as error:
                print(error, flush=True)
                return 2
            if result.returncode not in (0, 1):
                return 2
            defects |= result.returncode == 1
    return 1 if defects else 0


if __name__ == "__main__":
    raise SystemExit(main())
