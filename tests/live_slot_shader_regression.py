"""checks live-slot wiring; --static-only avoids shader compilation.
default mode also checks gles2 pixels with mesa."""
import argparse
import ast
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
FEATURE = ROOT / "src/features/texture-studio"


def contracts():
    runtime = (FEATURE / "services/LiveSlotRuntime.cpp").read_text()
    editor = (FEATURE / "ui/LiveSlotEditor.cpp").read_text()
    shader = (ROOT / "resources/shaders/live_slot.glsl").read_text()
    hooks = (FEATURE / "hooks/LiveSlotHooks.cpp").read_text()
    manager = (FEATURE / "ui/TextureStudioLayer.cpp").read_text()
    popup = (FEATURE / "ui/NewProjectPopup.cpp").read_text()
    serialization = (FEATURE / "persist/TextureProjectSerialize.hpp").read_text()
    settings = json.loads((ROOT / "mod.json").read_text())
    uniforms = set(re.findall(r"uniform\s+\w+\s+(u_\w+)\s*;", shader))
    pushed = set(re.findall(r'(?:scalar|color)\("(u_\w+)"', runtime))
    pushed.update(re.findall(r'getUniformLocationForName\("(u_\w+)"', runtime))
    assert uniforms == pushed, (uniforms - pushed, pushed - uniforms)
    for text in (runtime, editor, manager, popup):
        for forbidden in ("PackExporter::", "TextureLoaderInstaller::", "saveToPng(",
                          "encodeAsPng(", "snapshotTexture(", "onGenerate("):
            assert forbidden not in text, forbidden
    assert 'CCSprite::draw()' in hooks and 'CCSpriteBatchNode::draw()' in hooks
    assert 'setShaderProgram(previous)' in hooks
    assert 'CCTextureCache::addImage(path, skipSuffix)' in hooks
    assert 'LiveSlotRuntime::get().onTextureLoaded(path, texture, skipSuffix)' in hooks
    assert 'CCTextureCache::addUIImage(image, key)' in hooks
    assert 'LiveSlotRuntime::get().refreshTextures()' in hooks
    assert 'LiveSlotRuntime::get().onGLContextReload()' in (ROOT / 'src/core/GLContextReload.cpp').read_text()
    assert 'LiveSlotRuntime::get().start()' in (FEATURE / 'hooks/MenuLayerEntry.cpp').read_text()
    live_branch = serialization.split('if (p.liveRendering) {', 1)[1].split('}', 1)[0]
    assert 'return obj;' in live_branch
    assert '"sheets"' not in live_branch and '"spriteSettings"' not in live_branch
    assert 'texture-studio-auto-apply' not in settings['settings']
    assert 'texture-studio-medium-port' not in settings['settings']
    for test in ('tint_preview_shader_regression.py', 'live_slot_shader_regression.py'):
        ast.parse((ROOT / 'tests' / test).read_text())
    print('PASS: live-slot integration contracts (no compilation).')


def gpu_pixels():
    import tint_preview_shader_regression as gpu

    straight = [row[:] for row in gpu.BASE]
    straight[0][0] = (200, 100, 50, 64)
    straight[0][1] = (100, 150, 200, 128)
    straight[0][2] = (180, 180, 180, 192)
    straight[0][3] = (90, 90, 200, 96)
    for premultiplied in (False, True):
        gpu.BASE = [[tuple([gpu.rnd(c * p[3] / 255) for c in p[:3]] + [p[3]])
                     if premultiplied else p for p in row] for row in straight]
        for strength in (0.0, 0.5, 1.0):
            for glow in (0.0, 0.5, 1.0):
                def reference(src, masks, colors, bright, sat, con, thresh, replace, detail):
                    rgb = list(src[:3])
                    if premultiplied and src[3]:
                        rgb = [c * 255 / src[3] for c in rgb]
                    base = rgb[:]
                    if src[3] and not (thresh > 0 and gpu.lum(*rgb) < thresh):
                        for i in range(4):
                            if i == 2 and not detail:
                                continue
                            before = base[:]
                            base, _ = gpu.blend(base, src[3], gpu.tint(rgb, colors[i], bright, sat, con),
                                                masks[i], replace and i == 3)
                            if i == 3:
                                base = [b + (a - b) * glow for b, a in zip(before, base)]
                    output = [b + (a - b) * strength for b, a in zip(rgb, base)]
                    if premultiplied:
                        output = [c * src[3] / 255 for c in output]
                    return [gpu.rnd(c) for c in output] + [src[3]]

                gpu.run('live_slot.glsl', {
                    'u_strength': strength, 'u_glowStrength': glow,
                    'u_premultiplied': float(premultiplied),
                    'u_maskScaleX': 1.0, 'u_maskScaleY': 1.0,
                }, reference)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--static-only', action='store_true')
    args = parser.parse_args()
    contracts()
    if not args.static_only:
        gpu_pixels()
