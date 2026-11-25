# Pause layer lifecycle audit

The audit uses the Geometry Dash 2.2091 `.bro` declarations and the generated
Geode 6 modify bindings from the configured build. CMake rejects SDKs targeting
a different GD version. `PauseLayer` inherits its
entry/exit lifecycle from `CCLayer`; those hooks must target the bound base
implementation. `onEdit` schedules `goEdit` as an action, so editor navigation
must not be treated as an immediately completed resume.

The changes cover:

- Resume, restart, full restart, normal/practice mode, quit confirmations, and
  the deferred transition into the editor. A rejected quit no longer leaves an
  invisible pause menu. Pending scene changes do not reopen it.
- Capture cleanup cancels only the mod's own selectors. Native callbacks and
  other modules' scheduled work remain available.
- Screenshot and feedback capture share cleanup for success, errors, timeouts,
  and leaving the scene. Visibility and touch state are restored together.
- The framebuffer service controls hiding the loading overlay during rendering;
  a separate timer no longer races it or leaves it hidden during processing.
- File picker callbacks are invalidated when the pause menu closes. Capture
  previews do not open over a newer confirmation/modal window.
- Recapture uses an operation token. Late callbacks after timeout, close, or
  retry cannot reopen the popup or complete a newer capture. An idle preview
  does not cancel another caller's framebuffer request.
- Preview removal releases its caller exactly once, including external removal.
  Deferred gameplay resume is tied to the original `PlayLayer` and uses native
  `resume()` to preserve audio and other hooks.
- Pause zoom stops responding during capture or modal dialogs, and when its
  module is disabled. Reset does not modify a running, unpaused level.
- Closing cancels framebuffer work before resetting zoom, so a capture's saved
  transform cannot put the zoom back after resume. Zoom cleanup belongs to the
  original gameplay instance.
- Repeated setup repairs missing capture buttons without duplicating them;
  camera buttons belonging to other mods keep their callbacks. Deferred layout
  updates ignore a closing or detached pause menu.

## Automated checks

```powershell
python tests/pause_layer_bindings_regression.py --build-dir build-clang
clang-cl /nologo /std:c++20 /EHsc tests/capture_operation_regression.cpp /Fo:build-clang/tests/capture_operation_regression.obj /Fe:build-clang/tests/capture_operation_regression.exe
& ./build-clang/tests/capture_operation_regression.exe
cmake --build build-clang --parallel 4
```

The binding audit discovers hooks in all source files that modify `PauseLayer`,
`EditorPauseLayer`, `PlayLayer`, or `LevelEditorLayer`. The operation regression
checks duplicate input, repeated completion, cancellation, and 10,000
timeout/retry sequences. These tests do not execute Geometry Dash.

## In-game verification

Use a test level and check:

1. Pause/resume repeatedly with buttons, Escape, and Space; restart normally and
   from the beginning; change normal/practice mode.
2. Cancel and confirm quit and editor dialogs, including while a capture is
   starting. The menu must remain usable after cancellation.
3. Capture, cancel the preview, recapture repeatedly, and close during recapture.
   The pause menu and its inputs must recover, with no returning popup.
4. Open a file picker, close the pause menu, then complete/cancel the picker.
   Its old result must not open a new preview.
5. Zoom/pan and hide/show the pause menu, then capture or open a modal. Zoom must
   stop until the operation finishes. Disable pause zoom while paused as well.
6. Enter the editor from pause, return to gameplay, and exit to the browser.
   No loading screen or pause overlay should remain attached.
7. Repeat with a custom pause layout and other installed pause/camera mods.

Runtime verification is still required; compilation and binding checks do not
establish that every combination of third-party hooks behaves correctly.
