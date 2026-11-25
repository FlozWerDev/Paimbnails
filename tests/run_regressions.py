"""Run portable regressions, generated-binding audits, or optional integration tests."""

import argparse
import ctypes.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dependency_paths import json_compile_inputs, package_source

ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / "tests"
CPP_TESTS = {
    "request_queue_regression": ["src/features/twitch-requests/TwitchRequestParser.cpp"],
    "request_verification_regression": [],
    "visible_rows_regression": [],
    "capture_operation_regression": [],
    "project_storage_regression": [],
    "lazy_texture_regression": [],
    "native_scene_navigation_regression": [],
    "scene_safety_regression": [],
    "dynamic_transition_motion_regression": [],
    "custom_transition_regression": [],
    "custom_transition_preview_regression": [],
    "menu_physics_regression": [],
    "disk_manifest_regression": ["src/features/thumbnails/services/DiskManifestIndex.cpp"],
    "gif_decoder_regression": ["src/utils/GIFDecoder.cpp"],
    "profile_playback_regression": [],
    "cache_budget_regression": [],
    "cache_image_policy_regression": [],
    "progressbar_math_regression": [],
    "custom_hover_regression": [],
    "scroll_input_filter_regression": [],
    "death_sfx_format_regression": [],
    "dynamic_ambient_regression": [],
    "glass_quality_regression": [],
    "glass_geometry_regression": [],
    "filesystem_path_regression": [],
    "wallpaper_playback_regression": [],
    "wallpaper_layout_regression": [],
    "scene_layout_regression": [],
    "web_frame_convert_regression": [],
    "atomic_file_write_regression": ["src/utils/AtomicFileWrite.cpp"],
    "hmac_sha256_regression": ["src/utils/HmacSha256.cpp"],
    "audio_wav_regression": ["src/video/AudioWavCommon.cpp"],
    "editor_animate_regression": [],
    "editor_optimizer_regression": [
        "src/features/editor-optimizer/StructureOptimizerCore.cpp",
    ],
    "editor_physics_regression": [],
    "editor_physics_native_regression": [],
    "editor_physics_preview_regression": [
        "src/features/editor-physics/services/NativePreview.cpp",
        "src/features/editor-physics/services/PhysicsSolver.cpp",
        "src/features/editor-physics/PhysicsNative.cpp",
    ],
    "module_registry_regression": [
        "src/core/modules/ModuleCatalog.cpp",
        "src/core/modules/ModuleRegistry.cpp",
    ],
    "menu_loop_scan_regression": [],
    "gif_import_regression": [],
    "image_to_editor_regression": [],
    "packgen_image_regression": ["src/utils/AtomicFileWrite.cpp"],
    "packgen_paint_regression": [],
    "paint_circle_regression": [],
    "ui_sprite_catalog": [],
    "guide_matcher_regression": [],
    "guide_conversation_regression": [],
    "packcache_roundtrip_regression": ["src/utils/AtomicFileWrite.cpp"],
    "blur_disk_init_regression": [],
    "menumusic_save_guard_regression": ["src/utils/AtomicFileWrite.cpp"],
    "autopreview_guard_regression": [],
    "translator_text_regression": ["src/features/translator/TranslatorText.cpp"],
    "chat_image_token_regression": [],
    "chat_image_crypto_regression": [
        "src/utils/HmacSha256.cpp",
        "src/features/thread-images/ChatImageCrypto.cpp",
    ],
    "core_lifecycle_regression": [
        "src/core/modules/ModuleCatalog.cpp",
        "src/core/modules/ModuleRegistry.cpp",
        "src/utils/AtomicFileWrite.cpp",
        "src/features/menu-music/services/MenuMusicLibrarySave.cpp",
    ],
    "editor_mcp_regression": [
        "src/features/editor-mcp/EditorMcpCatalog.cpp",
        "src/features/editor-mcp/EditorMcpSchemas.cpp",
        "src/features/editor-mcp/EditorMcpProtocol.cpp",
    ],
}
CPP_TEST_FILES = {
    "guide_matcher_regression": "guide/harness.cpp",
    "guide_conversation_regression": "guide/conversation_harness.cpp",
}
CPP_TEST_STUBS = {
    "module_registry_regression": "module_registry_stubs",
    "scene_safety_regression": "scene_safety_stubs",
    "core_lifecycle_regression": "core_lifecycle_stubs",
}


def python_command(name, *arguments):
    # Embedded Python omits the script directory from sys.path.
    launch = "import runpy,sys;sys.path.insert(0,sys.argv[1]);sys.argv=sys.argv[2:];runpy.run_path(sys.argv[0],run_name='__main__')"
    return [sys.executable, "-c", launch, str(TESTS), str(TESTS / name), *arguments]


def compiler_path(explicit):
    if explicit:
        return str(Path(explicit).resolve()) if Path(explicit).is_file() else explicit
    candidates = ("clang-cl", "cl", "clang++", "g++") if os.name == "nt" else ("g++", "clang++")
    for candidate in candidates:
        if found := shutil.which(candidate):
            return found
    raise RuntimeError("No C++ compiler found; pass --cxx or set CXX")


def add_sanitizer_runtime(environment, compiler):
    found = shutil.which(compiler) or compiler
    library = Path(found).resolve().parent.parent / "lib/clang"
    for runtime in sorted(library.glob("*/lib/windows"), reverse=True):
        if list(runtime.glob("clang_rt.asan_dynamic-*.dll")):
            environment["PATH"] = str(runtime) + os.pathsep + environment.get("PATH", "")
            break


def fmt_include_path(build):
    source = package_source(build, ROOT, "fmt", "include/fmt/format.h")
    return source / "include" if source else None


def rapidfuzz_include_path(build):
    return package_source(build, ROOT, "rapidfuzz", "rapidfuzz/fuzz.hpp")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--suite", choices=("portable", "bindings", "optional"), default="portable")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build-regressions")
    parser.add_argument("--cxx", default=os.environ.get("CXX"))
    parser.add_argument("--sdk", default=os.environ.get("GEODE_SDK"))
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    output = build / "regression-results"
    output.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    environment["PAIMBNAILS_BUILD_DIR"] = str(build)
    records = []

    def run(name, command, timeout=120, env=None):
        try:
            result = subprocess.run(command, cwd=ROOT, env=env or environment,
                                    capture_output=True, text=True, errors="replace", timeout=timeout)
            log = result.stdout + result.stderr
            status = "PASS" if result.returncode == 0 else "FAIL"
        except (OSError, subprocess.TimeoutExpired) as error:
            status, log = "FAIL", str(error)
        (output / (name + ".log")).write_text(log, encoding="utf-8")
        records.append({"test": name, "status": status})
        print(f"{status}: {name}", flush=True)
        if status == "FAIL":
            print(log[-5000:], flush=True)
        return status == "PASS"

    def skip(name, reason):
        records.append({"test": name, "status": "SKIP", "reason": reason})
        print(f"SKIP: {name}: {reason}", flush=True)

    if args.suite == "portable":
        compiler = compiler_path(args.cxx)
        cl_style = Path(compiler).stem.lower() in ("cl", "clang-cl")
        add_sanitizer_runtime(environment, compiler)
        for name, sources in CPP_TESTS.items():
            executable = output / (name + (".exe" if os.name == "nt" else ""))
            test_source = TESTS / CPP_TEST_FILES.get(name, name + ".cpp")
            inputs = [str(test_source), *[str(ROOT / path) for path in sources]]
            stub_dir = CPP_TEST_STUBS.get(name, "stubs")
            if cl_style:
                command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/W4",
                           "/I" + str(TESTS / stub_dir)]
                if name == "lazy_texture_regression":
                    command.append("/I" + str(TESTS / "lazy_texture_stubs"))
                if name == "packgen_paint_regression":
                    command.append("/I" + str(TESTS / "packgen_stubs"))
                if name.startswith("guide_"):
                    command.append("/I" + str(TESTS / "guide" / "stubs"))
                    rapidfuzz_include = rapidfuzz_include_path(build)
                    if rapidfuzz_include:
                        command.append("/I" + str(rapidfuzz_include))
                if name in ("editor_animate_regression", "core_lifecycle_regression", "editor_mcp_regression"):
                    fmt_include = fmt_include_path(build)
                    if fmt_include:
                        command.append("/I" + str(fmt_include))
                if name in ("core_lifecycle_regression", "editor_mcp_regression"):
                    result_root = package_source(build, ROOT, "result", "include/Geode/Result.hpp")
                    if result_root:
                        command.append("/I" + str(result_root / "include"))
                    json_includes, json_sources = json_compile_inputs(build, ROOT)
                    command += ["/I" + str(include) for include in json_includes]
                    inputs += [str(path) for path in json_sources]
                if name == "editor_mcp_regression":
                    command += ["/DFMT_HEADER_ONLY", "/DPAIM_EDITOR_MCP_HEADLESS"]
                if args.sanitize:
                    command += ["/Zi", "/fsanitize=address"]
                    if "clang" in Path(compiler).name:
                        command += ["/clang:-fsanitize=undefined"]
                command += inputs + ["/Fo:" + str(output) + os.sep, "/Fe:" + str(executable)]
            else:
                command = [compiler, "-std=c++20", "-Wall", "-Wextra", "-Wpedantic", "-pthread",
                           "-I" + str(TESTS / stub_dir)]
                if name == "lazy_texture_regression":
                    command.append("-I" + str(TESTS / "lazy_texture_stubs"))
                if name == "packgen_paint_regression":
                    command.append("-I" + str(TESTS / "packgen_stubs"))
                if name.startswith("guide_"):
                    command.append("-I" + str(TESTS / "guide" / "stubs"))
                    rapidfuzz_include = rapidfuzz_include_path(build)
                    if rapidfuzz_include:
                        command.append("-I" + str(rapidfuzz_include))
                if name in ("editor_animate_regression", "core_lifecycle_regression", "editor_mcp_regression"):
                    fmt_include = fmt_include_path(build)
                    if fmt_include:
                        command += ["-isystem", str(fmt_include)]
                if name in ("core_lifecycle_regression", "editor_mcp_regression"):
                    result_root = package_source(build, ROOT, "result", "include/Geode/Result.hpp")
                    if result_root:
                        command += ["-isystem", str(result_root / "include")]
                    json_includes, json_sources = json_compile_inputs(build, ROOT)
                    for include in json_includes:
                        command += ["-isystem", str(include)]
                    inputs += [str(path) for path in json_sources]
                if name == "editor_mcp_regression":
                    command += ["-DFMT_HEADER_ONLY", "-DPAIM_EDITOR_MCP_HEADLESS"]
                if args.sanitize:
                    command += ["-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
                command += inputs + ["-o", str(executable)]
            if run(name + "-compile", command, timeout=240 if name.startswith("guide_") else 120):
                if name in ("menu_loop_scan_regression", "project_storage_regression", "wallpaper_playback_regression"):
                    with tempfile.TemporaryDirectory(prefix="paimon-menu-loop-") as temporary:
                        run(name, [str(executable), temporary], timeout=60)
                else:
                    run(name, [str(executable)], timeout=180 if name.startswith("guide_") else 60)

        for name, arguments in (
            ("dynamic_transition_config_regression.py", []),
            ("live_slot_shader_regression.py", ["--static-only"]),
            ("ui_layout_regression.py", []),
            ("discord_presence_regression.py", []),
            ("rtx_render_regression.py", ["--static-only"]),
            ("module_catalog_regression.py", []),
            ("dependency_paths_regression.py", []),
            ("local_mod_deploy_regression.py", []),
            ("paimon_shop_regression.py", []),
        ):
            run(Path(name).stem, python_command(name, *arguments))
        if os.name == "nt":
            run("glass_shader_regression", python_command("glass_shader_regression.py"))
        run("review_behavior_regression", python_command("review_behavior_regression.py",
            "--build-dir", str(build)), timeout=240)
        run("review_guards_regression", python_command("review_guards_regression.py"))
        node = shutil.which("node")
        native_environment = environment.copy()
        native_compiler = compiler if not cl_style else shutil.which("clang++")
        if native_compiler:
            native_environment["CXX"] = native_compiler
            add_sanitizer_runtime(native_environment, native_compiler)
        for name in ("crash_compat_regression.mjs", "ios_keyboard_dispatcher_regression.mjs",
                     "quick_hub_android_context_regression.mjs", "main_level_prefetch_regression.mjs",
                     "emote_preload_regression.mjs"):
            if not node:
                skip(Path(name).stem, "Node.js unavailable")
                continue
            if name in ("main_level_prefetch_regression.mjs", "emote_preload_regression.mjs") and not native_compiler:
                skip(Path(name).stem, "Clang++ is required for native Node fixtures")
                continue
            run(Path(name).stem, [node, str(TESTS / name)], env=native_environment)

        try:
            import numpy
            import PIL
            imaging_available = True
        except ImportError:
            imaging_available = False
        if imaging_available:
            run("gd_renderer_regression", python_command("gd_renderer_regression.py"), timeout=240)
        else:
            skip("gd_renderer_regression", "Pillow and NumPy are required")

        if not imaging_available:
            skip("image_to_editor_visual_regression", "Pillow and NumPy are required")
        elif not native_compiler:
            skip("image_to_editor_visual_regression", "Clang++ is required to build the conversion harness")
        else:
            # deterministic synthetic source: an opaque photo-like composition
            # exercises conversion, export and native rendering end to end.
            from PIL import Image, ImageDraw
            with tempfile.TemporaryDirectory(prefix="paimon-visual-") as temporary:
                synthetic = Path(temporary) / "synthetic.png"
                image = Image.new("RGBA", (96, 96), (30, 30, 120, 255))
                draw = ImageDraw.Draw(image)
                draw.ellipse([10, 10, 60, 60], fill=(220, 60, 40, 255))
                draw.rectangle([50, 50, 90, 90], fill=(40, 200, 90, 255))
                image.save(synthetic)
                run("image_to_editor_visual_regression",
                    python_command("image_to_editor_visual_regression.py",
                                   "--image", str(synthetic),
                                   "--out", str(output / "image-to-editor-visual")),
                    timeout=300, env=native_environment)

    elif args.suite == "bindings":
        if not args.sdk:
            raise RuntimeError("Pass --sdk or set GEODE_SDK for binding audits")
        for name, arguments in (
            ("hook_bindings_regression.py", ["--build-dir", str(build)]),
            ("pause_layer_bindings_regression.py", ["--build-dir", str(build)]),
            ("geode_sdk_compat_regression.py", ["--sdk", args.sdk]),
            ("dynamic_transition_bindings_regression.py", []),
        ):
            run(Path(name).stem, python_command(name, *arguments))
    else:
        node = shutil.which("node")
        webview_sdk = package_source(build, ROOT, "webview2", "build/native/include/WebView2.h")
        if os.name == "nt" and webview_sdk:
            compiler = compiler_path(args.cxx)
            cl_style = Path(compiler).stem.lower() in ("cl", "clang-cl")
            live_test = output / "web_wallpaper_integration.exe"
            sources = [str(TESTS / "web_wallpaper_integration.cpp"),
                       str(ROOT / "src/features/backgrounds/services/WebWallpaperRenderer.cpp")]
            loader = str(webview_sdk / "build/native/x64/WebView2LoaderStatic.lib")
            if cl_style:
                command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/W4", *sources,
                           "/I" + str(webview_sdk / "build/native/include"),
                           "/Fo:" + str(output) + os.sep, "/Fe:" + str(live_test)]
                if args.sanitize:
                    command += ["/Zi", "/fsanitize=address"]
                    if "clang" in Path(compiler).name:
                        command += ["/clang:-fsanitize=undefined"]
                command += ["/link", loader, "d3d11.lib", "dxgi.lib", "windowsapp.lib", "ole32.lib", "user32.lib", "version.lib"]
            else:
                command = [compiler, "-std=c++20", "-Wall", "-Wextra", "-Wpedantic", *sources,
                           "-isystem", str(webview_sdk / "build/native/include"), loader,
                           "-o", str(live_test), "-ld3d11", "-ldxgi", "-lwindowsapp", "-lole32", "-luser32", "-lversion"]
                if args.sanitize:
                    command += ["-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
            add_sanitizer_runtime(environment, compiler)
            if run("web_wallpaper-compile", command):
                run("web_wallpaper", [str(live_test), str(output), str(ROOT / "resources/wallpaper/bridge.js")], timeout=150)
        else:
            skip("web_wallpaper", "Configure the Windows build to fetch the WebView2 SDK")
        resources = Path(environment.get("PAIMBNAILS_GD_RESOURCES", ""))
        required_sheets = ("GJ_GameSheetIcons", "GJ_GameSheetGlow", "GJ_GameSheet")
        if resources.is_dir() and all(
            (resources / (sheet + suffix)).is_file()
            for sheet in required_sheets for suffix in (".png", ".plist")
        ):
            compiler = compiler_path(args.cxx)
            cl_style = Path(compiler).stem.lower() in ("cl", "clang-cl")
            executable = output / ("packgen_official_assets.exe" if os.name == "nt"
                                   else "packgen_official_assets")
            command = ([compiler, "/nologo", "/std:c++20", "/EHsc",
                        "/I" + str(TESTS / "packgen_stubs")]
                       if cl_style else
                       [compiler, "-std=c++20", "-pthread",
                        "-I" + str(TESTS / "packgen_stubs")])
            if cl_style:
                command += [str(TESTS / "packgen_official_assets.cpp"),
                            "/Fe:" + str(executable)]
            else:
                command += [str(TESTS / "packgen_official_assets.cpp"),
                            "-o", str(executable)]
            if run("packgen_official_assets-compile", command):
                asset_output = output / "official-assets"
                asset_output.mkdir(parents=True, exist_ok=True)
                run("packgen_official_assets", [str(executable), str(resources),
                                                 str(asset_output)], timeout=300)
            audit_executable = output / ("ui_sprite_audit.exe" if os.name == "nt"
                                         else "ui_sprite_audit")
            audit_command = ([compiler, "/nologo", "/std:c++20", "/EHsc"]
                             if cl_style else [compiler, "-std=c++20"])
            audit_command += [str(TESTS / "ui_sprite_audit.cpp"),
                              "/Fe:" + str(audit_executable) if cl_style else "-o"]
            if not cl_style:
                audit_command.append(str(audit_executable))
            if run("ui_sprite_audit-compile", audit_command):
                audit_path = output / "ui-sprite-audit.txt"
                run("ui_sprite_audit", [str(audit_executable), str(resources),
                                         str(audit_path)], timeout=120)
        else:
            skip("packgen_official_assets",
                 "Set PAIMBNAILS_GD_RESOURCES to a Geometry Dash Resources folder with its PNG and plist sheets")
            skip("ui_sprite_audit",
                 "Set PAIMBNAILS_GD_RESOURCES to audit the official sprite plist files")
        server = ROOT / "cloudflare-server/paimon-thumbnails-server.js"
        if node and server.is_file():
            run("thumbnail_upload_routing", [node, str(TESTS / "thumbnail_upload_routing.mjs")])
        else:
            skip("thumbnail_upload_routing", "Private server sources or Node.js are unavailable")
        if node:
            dependency = subprocess.run([node, "-e", "require(process.env.PLAYWRIGHT_MODULE || 'playwright')"],
                                        cwd=ROOT, env=environment, capture_output=True)
            if dependency.returncode == 0:
                run("stream_overlay", [node, str(TESTS / "stream_overlay_regression.mjs")], timeout=300)
            else:
                skip("stream_overlay", "Install Playwright and its Chromium browser to run visual integration")
        else:
            skip("stream_overlay", "Node.js is unavailable")
        if sys.platform.startswith("linux") and ctypes.util.find_library("EGL") and ctypes.util.find_library("GLESv2"):
            for name in ("death_animation_shader_regression.py", "tint_preview_shader_regression.py",
                         "live_slot_shader_regression.py", "rtx_render_regression.py"):
                run(Path(name).stem + "-gpu", python_command(name))
        else:
            skip("gpu", "Headless GPU fixtures require Linux EGL/GLES")

    (output / (args.suite + ".json")).write_text(json.dumps(records, indent=2), encoding="utf-8")
    failed = sum(record["status"] == "FAIL" for record in records)
    print(f"{len(records)} checks; {failed} failures; logs: {output}", flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
