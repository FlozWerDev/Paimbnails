"""Static and structural checks for Android compatibility, PC-only gating, gestures, and Manager Cut."""

import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(rel: str) -> str:
    return (ROOT / rel).read_text(encoding="utf-8", errors="replace")


def main() -> None:
    manifest = json.loads(read("mod.json"))
    settings = manifest["settings"]

    # 1. PC-exclusive settings in mod.json must declare desktop-only platforms.
    desktop_only_settings = [
        "custom-cursor-enable",
        "smooth-scroll",
        "capture-menu-rightclick",
        "input-scroll-title",
        "input-scroll-enable",
        "input-scroll-step",
        "input-scroll-decimal-modifier",
        "input-scroll-decimal-step",
        "input-scroll-decimal-places",
        "input-scroll-focus-only",
        "input-scroll-wrap",
        "twitch-requests-obs-enabled",
        "info-ids-shift-reveal",
    ]
    for key in desktop_only_settings:
        plats = settings[key].get("platforms", [])
        assert plats == ["win", "mac"], f"{key} must have platforms=['win','mac'], got {plats}"

    # 2. All 28 mod.json keybinds must appear in ManagerCutPopup.cpp (+ quick-hub-hold-button).
    keybind_keys = [k for k, v in settings.items() if v.get("type") == "keybind"]
    manager_cut = read("src/features/keybinds/ui/ManagerCutPopup.cpp")
    for k in keybind_keys:
        assert f'"{k}"' in manager_cut, f"ManagerCutPopup is missing mod.json keybind '{k}'"
    assert '"quick-hub-hold-button"' in manager_cut, "ManagerCutPopup missing quick-hub-hold-button"
    assert '"Cambiar a Gestos"' in manager_cut and '"Cambiar a Teclas"' in manager_cut, \
        "ManagerCutPopup must provide explicit switch button between Keys and Gestures"

    # 3. Hub General category must include Manager Cut.
    hub = read("src/layers/PaimonHubLayer.cpp")
    case0 = hub.split("case 0:")[1].split("case 1:")[0]
    assert '"Manager Cut"' in case0, "PaimonHubLayer General category must include Manager Cut"
    assert "#if defined(GEODE_IS_DESKTOP)" in hub, "PaimonHubLayer must gate desktop Extras on mobile"

    # 4. Android critical fixes checks.
    egl = read("src/hooks/CCEGLView.cpp")
    assert "#ifdef GEODE_IS_WINDOWS\n    void setFrameSize" in egl, \
        "CCEGLView::setFrameSize must be Windows-only (missing on Android)"
    assert "#if defined(GEODE_IS_ANDROID)\n    void setFrameSize" in egl, \
        "Android resize handling should live on CCEGLViewProtocol"

    # Also ensure VolumeScrollHook mobile block includes <algorithm> cleanly.
    assert "#include <algorithm>" in read("src/features/volume-scroll/hooks/VolumeScrollHook.cpp")

    eclipse_blur = read("resources/shaders/eclipse_blur.glsl")
    assert "#version" not in eclipse_blur, "eclipse_blur.glsl must not hardcode desktop #version 120"

    gl_reload = read("src/core/GLContextReload.cpp")
    assert "eglGetCurrentContext" in gl_reload and "onContextLost" in gl_reload, \
        "GLContextReload must handle Android EGL context loss"

    cp = read("src/features/editor-colorpicker/ui/ColorPickerOverlay.cpp")
    assert "activePointerPos()" in cp and "m_hasTouchPos" in cp, \
        "ColorPickerOverlay must track touch coordinates on mobile"

    locator = read("src/features/texture-studio/data/GdResourcesLocator.cpp")
    assert "getFileData(" in locator and "GEODE_IS_MOBILE" in locator, \
        "Texture Studio locator must support Android APK assets via CCFileUtils"

    # 5. Cursor hooks must stay strictly desktop-only.
    cursor_hook = read("src/features/cursor/hooks/CursorHook.cpp")
    assert "PaimonClickTouchDispatcher" not in cursor_hook, \
        "Custom cursor must not run mobile touch hooks"
    assert cursor_hook.lstrip().startswith('#include "CursorHook.hpp"\n#if defined(GEODE_IS_DESKTOP)'), \
        "CursorHook must be desktop-only"

    # 6. Gestures engine and quick-hub/volume-scroll point-space touch tracking.
    gm_cpp = read("src/features/gestures/GestureManager.cpp")
    qh_touch = read("src/features/quick-hub/services/QuickHubTouchHold.cpp")
    vs_hook = read("src/features/volume-scroll/hooks/VolumeScrollHook.cpp")
    assert "viewPixelsToPoint" in gm_cpp and "viewPixelsToPoint" in qh_touch and "viewPixelsToPoint" in vs_hook, \
        "Mobile gestures must convert view pixels to DPI-independent UI points"
    assert "GestureEditPopup" in manager_cut and "setBinding" in gm_cpp, \
        "Gestures must be editable from Manager Cut"
    assert "->setAutoScale(false)" in manager_cut, \
        "ManagerCutPopup RowLayouts must disable autoScale to preserve button sizing"

    # 7. Full src/ Android audit fixes invariants.
    settings_hpp = read("src/core/Settings.hpp")
    assert 'hasSetting("discord-rpc-enabled")' in settings_hpp, \
        "discord_rpc::enabled() must check hasSetting before getSettingValue"

    welcome = read("src/features/onboarding/WelcomeFlow.cpp")
    assert "#if defined(GEODE_IS_DESKTOP)" in welcome, \
        "WelcomeFlow must gate desktop Cursor step and CursorManager calls"

    smooth_ui_cfg = read("src/ui/SmoothUIConfigPopup.cpp")
    assert "#if defined(GEODE_IS_DESKTOP)" in smooth_ui_cfg, \
        "SmoothUIConfigPopup must gate smooth-scroll card and presets"

    feat_cfg = read("src/ui/FeatureConfigPopup.cpp")
    assert "#if defined(GEODE_IS_DESKTOP)" in feat_cfg, \
        "FeatureConfigPopup must gate Scroll & Editor Zoom section"

    cap_overlay = read("src/features/capture/ui/CaptureOverlay.cpp")
    assert "#if defined(GEODE_IS_WINDOWS)" in cap_overlay, \
        "CaptureOverlay must not fail with false clipboard warning toast on mobile"

    cap_menu = read("src/features/capture/ui/CaptureMenuPopup.cpp")
    assert "#if defined(GEODE_IS_DESKTOP)" in cap_menu, \
        "CaptureMenuPopup must gate desktop right-click invert-inputs option"

    death_fx = read("src/features/death-effects/ui/DeathEffectPopup.cpp")
    icon_ed = read("src/features/icon-maker/ui/IconEditorLayer.cpp")
    icon_gal = read("src/features/icon-maker/ui/IconGalleryLayer.cpp")
    for rel, txt in [("DeathEffectPopup", death_fx), ("IconEditorLayer", icon_ed), ("IconGalleryLayer", icon_gal)]:
        assert "GEODE_IS_MOBILE" in txt and "clipboard::write" in txt, \
            f"{rel} must provide mobile path-copy fallback instead of bare openFolder"

    score_cell = read("src/hooks/GJLevelScoreCell.cpp")
    assert "#if !defined(GEODE_IS_DESKTOP)" in score_cell, \
        "GJLevelScoreCell::isPointerOver must return false off desktop"

    emote_picker = read("src/features/emotes/ui/EmotePickerPopup.cpp")
    assert "GEODE_IS_MOBILE" in emote_picker, \
        "EmotePickerPopup must guard mobile hover and update prompt text"

    collab = read("src/features/collab-editor/CollabManager.cpp")
    assert "CursorManager::get().pointerPos()" in collab, \
        "CollabManager::sendCameraPresence must read touch pointerPos on mobile"

    shaders = read("src/utils/Shaders.cpp")
    assert "GEODE_IS_MOBILE" in shaders and "pointerPos()" in shaders, \
        "ShaderBgSprite must track touch pointer on mobile"

    mm_add = read("src/features/menu-music/ui/MenuMusicAddPopup.cpp")
    assert "GEODE_IS_ANDROID" in mm_add, \
        "MenuMusicAddPopup must guard yt-dlp install prompt on mobile"

    print(
        f"PASS: verified {len(desktop_only_settings)} desktop-only settings, "
        f"{len(keybind_keys)} + 1 keybinds in Manager Cut, Android fixes, and full-src audit invariants."
    )


if __name__ == "__main__":
    main()
