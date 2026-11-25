"""checks live-slot wiring; --static-only avoids shader compilation.
default mode also checks gles2 pixels with mesa."""
import argparse
import ast
import json
import math
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
FEATURE = ROOT / "src/features/texture-studio"

# LiveTone order (LiveMaskAnalyzer.hpp) == toneForCode() order in the shader.
TONES = ("Primary", "Secondary", "Blue", "Pink", "Red", "Surface", "Neutral", "Background")
NEUTRAL, BACKGROUND = TONES.index("Neutral"), TONES.index("Background")


def contracts():
    runtime = (FEATURE / "services/LiveSlotRuntime.cpp").read_text()
    header = (FEATURE / "services/LiveSlotRuntime.hpp").read_text()
    analyzer = (FEATURE / "engine/LiveMaskAnalyzer.cpp").read_text()
    analyzer_header = (FEATURE / "engine/LiveMaskAnalyzer.hpp").read_text()
    editor = (FEATURE / "ui/LiveSlotEditor.cpp").read_text()
    shader = (ROOT / "resources/shaders/live_slot.glsl").read_text()
    hooks = (FEATURE / "hooks/LiveSlotHooks.cpp").read_text()
    manager = (FEATURE / "ui/TextureStudioLayer.cpp").read_text()
    popup = (FEATURE / "ui/NewProjectPopup.cpp").read_text()
    serialization = (FEATURE / "persist/TextureProjectSerialize.hpp").read_text()
    settings = json.loads((ROOT / "mod.json").read_text())
    uniforms = set(re.findall(r"uniform\s+\w+\s+(u_\w+)\s*;", shader))
    pushed = set(re.findall(r'"(u_\w+)"', runtime))
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
    assert 'LiveSlotRuntime::get().onTextureLoaded(key, texture, true)' in hooks
    # every cell/list fill is a CCLayerColor: one generic hook, colors restored after draw.
    assert 'CCLayerColor::draw()' in hooks and 'remapLayerColors(m_pSquareColors, 4)' in hooks
    assert 'std::begin(saved), std::end(saved), std::begin(m_pSquareColors)' in hooks
    for gone in ('LevelCell', 'CommentCell', 'CustomSFXCell', 'SmartTemplateCell'):
        assert gone not in hooks, gone
    assert 'LiveSlotRuntime::get().onGLContextReload()' in (ROOT / 'src/core/GLContextReload.cpp').read_text()
    assert 'LiveSlotRuntime::get().start()' in (FEATURE / 'hooks/MenuLayerEntry.cpp').read_text()
    live_branch = serialization.split('if (p.liveRendering) {', 1)[1].split('}', 1)[0]
    assert 'return obj;' in live_branch
    assert '"sheets"' not in live_branch and '"spriteSettings"' not in live_branch
    for field in ('"toneSpread"', '"autoSurface"', '"autoBackground"', '"colorSurface"',
                  '"colorBackground"', '"livePaintBackgrounds"', '"livePaintLists"'):
        assert field in live_branch and field in serialization.split('fromJson', 1)[1], field
    # palette-faithful engine: tones derive from the palette, never fixed per-button colors.
    for forbidden in ('kButtonPalettes', 'buttonPalette', 'isOffState'):
        assert forbidden not in runtime, forbidden
    assert 'p.tintScope = TintScope::ButtonsAndMenuUi;' in runtime
    assert 'gameTint' not in shader and 'vChroma' not in shader
    assert 'vec3 originalRgb = tex.rgb * v_fragmentColor.rgb;' in shader
    assert 'u_strength * clamp(paintWeight, 0.0, 1.0)' in shader
    assert 'tintedSum / max(paintWeight' in shader
    assert 'vDim' not in shader and 'blendRole(' not in shader
    # masks hold coverage fractions (RGBA4444), so the shader never divides by alpha.
    assert 'm / max(tex.a' not in shader
    assert 'kCCTexture2DPixelFormat_RGBA4444' in runtime
    assert 'floor(m.a * 15.0 + 0.5)' in shader
    shader_order = re.findall(r'return u_tone(\w+);', shader)
    assert tuple(shader_order) == TONES, shader_order
    analyzer_order = re.findall(r'^\s+(\w+)(?: = 0)?,', analyzer_header.split('enum class LiveTone', 1)[1]
                                .split('};', 1)[0], re.M)
    assert tuple(analyzer_order) == TONES, analyzer_order
    assert 'color(Uniform::TonePrimary, tone(LiveTone::Primary))' in runtime
    assert 'color(Uniform::ToneBackground, tone(LiveTone::Background))' in runtime
    assert 'TintScope tintScope = TintScope::ButtonsAndMenuUi;' in (
        FEATURE / 'persist/TextureProject.hpp').read_text()
    # sheet textures loaded at boot must map by any key spelling, as masks land
    assert '"-uhd.png"' in runtime and '"-hd.png"' in runtime
    assert 'cache->m_pTextures' in runtime
    assert 'textureCache->m_pTextures->count() != m_cachedTextureCount' in runtime
    assert 'source.textures.emplace_back(texture)' in runtime
    assert 'resolved != sourcePath && preferred != sourcePath && key != sourcePath' in runtime
    upload = runtime.split('LiveSlotRuntime::startNextMask()', 1)[1].split(
        'LiveSlotRuntime::trimUnusedMasks(', 1)[0]
    assert 'refreshTextures();' not in upload
    # background art: the gradient always, level art only outside gameplay.
    assert 'add("GJ_gradientBG", false, SourceKind::Background)' in runtime
    assert 'SourceKind::MenuBackground && m_inGameplay' in runtime
    assert 'gm->m_playLayer || gm->m_levelEditorLayer' in runtime
    assert 'ClusterClassifier::classifyUiWithRatios(' in analyzer
    renderer = (FEATURE / 'engine/SpritePreviewRenderer.cpp').read_text()
    assert 'ClusterClassifier::classifyUi(clusters, framePixels)' in renderer
    assert 'LiveSlotRuntime::get().reanalyze(m_project)' in editor
    reanalyze = runtime.split('LiveSlotRuntime::reanalyze(', 1)[1].split('\n}', 1)[0]
    for required in ('releaseSources()', 'live_mask_cache::clear(', 'return preview('):
        assert required in reanalyze, required
    release = runtime.split('LiveSlotRuntime::releaseSources()', 1)[1].split('\n}', 1)[0]
    for required in ('++*m_generation', 'm_sources.clear()', 'm_textures.clear()', 'm_maskBytes = 0'):
        assert required in release, required
    assert 'm_workerRunning = false' not in release
    # cocos only forgets deleted textures on unit 0: masks reset unit 1 first.
    assert 'ccGLBindTexture2DN(1, 0)' in runtime and 'unbindMasks();' in release
    assert 'm_workerRunning' in header
    assert 'ScrollLayer::create(' in editor and 'contentHeight' in editor
    assert 'Scrollbar::create(m_controlsScroll)' in editor
    assert 'Show Original' in editor and 'Show Painted' in editor
    assert 'keepScroll' in editor
    # swatches must not use textures the live engine repaints.
    assert 'CCSprite::create("square.png")' not in editor and 'createWithTexture(m_solid)' in editor
    settings_ui = (ROOT / 'src/features/settings-panel/ui/SettingsCategoryBuilder.cpp').read_text()
    assert 'Open Texture Studio' in settings_ui and 'TextureStudioLayer::open()' in settings_ui
    assert '[LiveSlot] masks done' in runtime and '[LiveSlot] tint applied' in runtime
    assert 'texture-studio-auto-apply' not in settings['settings']
    assert 'texture-studio-medium-port' not in settings['settings']
    for test in ('tint_preview_shader_regression.py', 'live_slot_shader_regression.py'):
        ast.parse((ROOT / 'tests' / test).read_text())
    print('PASS: live-slot integration contracts (no compilation).')


def live_pixel(src, mask, tones, detail_color, glow_color, bright, sat, con, thresh, replace,
               detail, *, premultiplied=False, strength=1.0, glow=1.0, vertex=(1, 1, 1, 1),
               full_paint=False):
    """CPU oracle for live_slot.glsl. mask = (body, detail, glow, code) nibbles 0..15."""
    def rounded(value):
        return min(max(math.floor(value + 0.5), 0), 255)

    luminance = lambda c: sum(x * w for x, w in zip(c, (.30, .59, .11)))
    original = [s * v for s, v in zip(src[:3], vertex[:3])]
    out_alpha = src[3] * vertex[3]
    keep = [rounded(c) for c in original] + [rounded(out_alpha)]
    if strength <= 0 or src[3] < .5:
        return keep
    body, det, gl, code = mask
    body, det, gl = body / 15, det / 15, gl / 15

    alpha = src[3] / 255
    tex = [c / 255 for c in src[:3]]
    if premultiplied:
        tex = [c / alpha for c in tex]
    vert = list(vertex[:3])
    if premultiplied and vertex[3] > 0:
        vert = [c / vertex[3] for c in vert]
    v_max = max(vert)
    if full_paint:
        # background art: the art's shading times the tint brightness, in the tone.
        shade = luminance(tex) * v_max
        backdrop = [c * shade for c in tones[BACKGROUND]]
        if premultiplied:
            backdrop = [c * alpha * vertex[3] for c in backdrop]
        return [rounded(o * (1 - strength) + b * strength) for o, b in zip(original, backdrop)] + \
            [rounded(out_alpha)]
    if code == NEUTRAL:
        v_min = min(vert)
        v_sat = (v_max - v_min) / v_max if v_max > 0 else 0
        t = min(max((v_sat - .15) / .20, 0), 1)
        body *= 1 - t * t * (3 - 2 * t)
    det *= float(detail)
    glow_weight = gl * glow
    if replace and gl > .5 / 15:
        glow_weight = glow
    total = body + det + glow_weight
    if total <= 0:
        return keep
    if thresh > .5 and luminance([c * 255 for c in tex]) < thresh:
        return keep
    rgb = [min(max(t * v, 0), 1) * 255 for t, v in zip(tex, vert)]
    lum = luminance(rgb)

    def tinted(color):
        if lum <= bright:
            result = [c * lum / bright for c in color]
        else:
            t = min(max((lum - bright) / max(255 - bright, 1), 0), 1)
            t = t * t * (3 - 2 * t)
            result = [c + (255 - c) * t for c in color]
        luma = luminance(result)
        result = [luma + (c - luma) * sat for c in result]
        return [rounded((c - 127.5) * (1 + con) + 127.5) for c in result]

    parts = [(tones[int(code)], body), (detail_color, det), (glow_color, glow_weight)]
    painted = [sum(tinted(color)[ch] * w for color, w in parts if w > 0) / max(total, 1 / 255) / 255
               for ch in range(3)]
    if premultiplied:
        painted = [c * alpha * vertex[3] for c in painted]
    coverage = strength * min(total, 1)
    # original is in 0..255 space; painted in 0..1 space.
    output = [o * (1 - coverage) + p * 255 * coverage for o, p in zip(original, painted)]
    return [rounded(c) for c in output] + [rounded(out_alpha)]


PALETTE = [(255, 0, 0), (0, 0, 255), (0, 255, 0), (255, 0, 255),
           (128, 0, 0), (120, 80, 40), (128, 128, 128), (10, 20, 30)]
WHITE = (255, 255, 255)


def reference_cases():
    def pixel(src, mask, **options):
        return live_pixel(src, mask, PALETTE, WHITE, WHITE, 255, 1, 0, 0, False, False, **options)

    # Unpainted texels, strength=0 and zero-detail keep the vertex-tinted original.
    assert pixel((80, 160, 240, 255), (0, 0, 0, 0), vertex=(1, .5, .25, 1)) == [80, 80, 60, 255]
    assert pixel((80, 160, 240, 255), (15, 0, 0, 0), strength=0) == [80, 160, 240, 255]
    assert pixel((80, 160, 240, 255), (0, 15, 0, 0), vertex=(1, .5, .25, 1)) == [80, 80, 60, 255]
    assert pixel((80, 160, 240, 255), (0, 0, 15, 0), glow=0) == [80, 160, 240, 255]
    # The tone code picks the palette shade: a green texel leaves no green behind.
    primary = pixel((0, 255, 0, 255), (15, 0, 0, 0))
    secondary = pixel((0, 255, 0, 255), (15, 0, 0, 1))
    assert primary[1] == 0 and primary[0] > 0 and primary[2] == 0, primary
    assert secondary[1] == 0 and secondary[2] > 0 and secondary[0] == 0, secondary
    # Brightness follows what is on screen: dimmed sprites stay dimmed.
    bright = pixel((200, 200, 200, 255), (15, 0, 0, 0))
    dimmed = pixel((200, 200, 200, 255), (15, 0, 0, 0), vertex=(.5, .5, .5, 1))
    assert dimmed[0] < bright[0], (dimmed, bright)
    # Gray art colored by code (color previews) keeps its color.
    assert pixel((255, 255, 255, 255), (15, 0, 0, NEUTRAL), vertex=(0, 1, 0, 1)) == [0, 255, 0, 255]
    # Backgrounds ignore the mask and take the background tone; the tint's hue
    # (GD cycles it on the main menu) cannot change their brightness.
    background = pixel((200, 200, 200, 255), (0, 0, 0, 0), full_paint=True, vertex=(0, .4, 1, 1))
    green_tint = pixel((200, 200, 200, 255), (0, 0, 0, 0), full_paint=True, vertex=(0, 1, 0, 1))
    assert background == green_tint and background[2] > background[0], (background, green_tint)
    dimmed_tint = pixel((200, 200, 200, 255), (0, 0, 0, 0), full_paint=True, vertex=(0, .5, 0, 1))
    assert dimmed_tint[2] < background[2], dimmed_tint
    # Dark protect judges the art, not the dimmed on-screen color.
    assert live_pixel((200, 200, 200, 255), (15, 0, 0, 0), PALETTE, WHITE, WHITE, 255, 1, 0, 100,
                      False, False, vertex=(.3, .3, .3, 1)) != [60, 60, 60, 255]
    # Premultiplied texels keep their coverage.
    assert pixel((25, 25, 25, 64), (15, 0, 0, 0), premultiplied=True)[3] == 64
    print('PASS: 12 live tint reference cases (CPU oracle; no GPU compilation).')


def performance_contracts():
    runtime = (FEATURE / 'services/LiveSlotRuntime.cpp').read_text()
    header = (FEATURE / 'services/LiveSlotRuntime.hpp').read_text()
    analyzer = (FEATURE / 'engine/LiveMaskAnalyzer.cpp').read_text()
    draw = runtime.split('LiveSlotRuntime::prepareDraw(', 1)[1].split(
        'LiveSlotRuntime::remapLayerColors(', 1)[0]
    frame = runtime.split('void LiveSlotRuntime::beginFrame(', 1)[1].split('\n}', 1)[0]
    layers = runtime.split('bool LiveSlotRuntime::remapLayerColors(', 1)[1].split('\n}', 1)[0]
    # The draw hooks may request deferred work, but never scan paths or query uniforms.
    for section in (draw, frame, layers):
        for forbidden in ('refreshTextures();', 'getUniformLocationForName(', 'programForKey(',
                          'fullPathForFilename('):
            assert forbidden not in section, forbidden
    assert 'beginFrame(frame)' in draw and 'm_lastCacheCheckFrame == frame' in frame
    assert 'requestMaskTrim(frame)' in frame
    assert 'm_project.tintStrength <= 0.f' in draw and 'm_project.tintStrength <= 0.f' in layers
    assert 'm_sourcesByName.find(' in runtime and 'm_sourcesByPath.find(' in runtime
    # One worker/result at a time, and only requested sources get analyzed.
    assert 'm_workerRunning' in header and 'requestMaskBuild()' in runtime
    assert 'source.requested && !source.analyzed' in runtime
    assert 'trimUnusedMasks(' in runtime
    assert 'm_maskBytes' in header and 'kMaskCacheBudget' in runtime
    # Analysis: disk cache before decoding, distinct colors, frames on a worker pool.
    build = runtime.split('MaskResult buildLiveMask(', 1)[1].split('\n}', 1)[0]
    assert build.index('live_mask_cache::load(') < build.index('ImageBuffer::loadFromFile(')
    assert 'live_mask_cache::store(' in build and 'LiveMaskAnalyzer::analyzeAll(' in build
    assert 'ColorClustering::computeWeighted(' in analyzer
    assert 'std::thread' in analyzer and 'overlaps(' in analyzer
    shader = (ROOT / 'resources/shaders/live_slot.glsl').read_text()
    assert shader.count('rec601(src)') == 1
    assert 'paintWeight <= 0.0' in shader
    assert 'if (body > 0.0)' in shader
    print('PASS: live-slot performance contracts (no native/GPU compilation).')


def gpu_pixels():
    import ctypes as ct
    import tint_preview_shader_regression as gpu

    api, egl, gl = gpu.api, gpu.egl, gpu.gl
    Int, UInt, Float, Pointer = gpu.Int, gpu.UInt, gpu.Float, gpu.Pointer
    W = 8
    display = api(egl, "eglGetDisplay", Pointer, Pointer)(None)
    assert api(egl, "eglInitialize", UInt, Pointer, Pointer, Pointer)(display, None, None)
    attributes = (Int * 13)(0x3033, 1, 0x3040, 4, 0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3021, 8, 0x3038)
    config, count = Pointer(), Int()
    assert api(egl, "eglChooseConfig", UInt, Pointer, Pointer, Pointer, Int, Pointer)(
        display, attributes, ct.byref(config), 1, ct.byref(count)) and count.value
    context = api(egl, "eglCreateContext", Pointer, Pointer, Pointer, Pointer, Pointer)(
        display, config, None, (Int * 3)(0x3098, 2, 0x3038))
    surface = api(egl, "eglCreatePbufferSurface", Pointer, Pointer, Pointer, Pointer)(
        display, config, (Int * 5)(0x3057, W, 0x3056, W, 0x3038))
    assert api(egl, "eglMakeCurrent", UInt, Pointer, Pointer, Pointer, Pointer)(
        display, surface, surface, context)
    directory = ROOT / "resources" / "shaders"
    vertex = gpu.shader(0x8B31, "uniform mat4 CC_MVPMatrix;\n" + (directory / "cell_vertex.glsl").read_text())
    fragment = gpu.shader(0x8B30, (directory / "live_slot.glsl").read_text())
    program = api(gl, "glCreateProgram", UInt)()
    api(gl, "glAttachShader", None, UInt, UInt)(program, vertex)
    api(gl, "glAttachShader", None, UInt, UInt)(program, fragment)
    api(gl, "glLinkProgram", None, UInt)(program)
    success = Int()
    api(gl, "glGetProgramiv", None, UInt, UInt, ct.POINTER(Int))(program, 0x8B82, ct.byref(success))
    assert success.value
    api(gl, "glUseProgram", None, UInt)(program)
    location = api(gl, "glGetUniformLocation", Int, UInt, ct.c_char_p)
    uniform1i = api(gl, "glUniform1i", None, Int, Int)
    uniform1f = api(gl, "glUniform1f", None, Int, Float)
    uniform3f = api(gl, "glUniform3f", None, Int, Float, Float, Float)
    identity = (Float * 16)(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
    api(gl, "glUniformMatrix4fv", None, Int, Int, UInt, Pointer)(
        location(program, b"CC_MVPMatrix"), 1, 0, identity)

    texels = [(200, 100, 50, 255), (100, 150, 200, 255), (180, 180, 180, 255), (90, 90, 200, 255),
              (10, 10, 10, 255), (123, 45, 67, 0), (255, 255, 255, 255), (77, 88, 99, 128)]
    masks = [(15, 0, 0, 0), (15, 0, 0, 1), (8, 0, 7, 5), (0, 15, 0, 2),
             (15, 0, 0, 3), (15, 15, 15, 4), (15, 0, 0, NEUTRAL), (10, 0, 0, 6)]
    gen_tex = api(gl, "glGenTextures", None, Int, Pointer)
    bind_tex = api(gl, "glBindTexture", None, UInt, UInt)
    tex_param = api(gl, "glTexParameteri", None, UInt, UInt, Int)
    tex_image = api(gl, "glTexImage2D", None, UInt, Int, Int, Int, Int, Int, UInt, UInt, Pointer)
    active_tex = api(gl, "glActiveTexture", None, UInt)

    def upload(data, unit, kind):
        tex = UInt()
        gen_tex(1, ct.byref(tex))
        active_tex(0x84C0 + unit)
        bind_tex(0x0DE1, tex.value)
        for param, value in [(0x2801, 0x2600), (0x2800, 0x2600), (0x2802, 0x812F), (0x2803, 0x812F)]:
            tex_param(0x0DE1, param, value)
        tex_image(0x0DE1, 0, 0x1908, W, W, 0, 0x1908, kind, data)

    rgba = [c for y in range(W) for x in range(W) for c in (texels[x] if y == 0 else (0, 0, 0, 0))]
    upload((ct.c_ubyte * len(rgba))(*rgba), 0, 0x1401)
    packed = [((m[0] << 12) | (m[1] << 8) | (m[2] << 4) | m[3]) if y == 0 else 0
              for y in range(W) for m in masks]
    upload((ct.c_ushort * len(packed))(*packed), 1, 0x8033)
    uniform1i(location(program, b"u_texture"), 0)
    uniform1i(location(program, b"u_roleMask"), 1)
    for name, color in zip(TONES, PALETTE):
        uniform3f(location(program, ("u_tone" + name).encode()), *color)
    uniform3f(location(program, b"u_detailColor"), 255, 0, 0)
    uniform3f(location(program, b"u_glowColor"), 0, 255, 255)
    for name, value in (("u_brightness", 160.0), ("u_saturation", 1.0), ("u_contrast", 0.0),
                        ("u_darkThreshold", 0.0), ("u_glowReplace", 0.0), ("u_applyDetail", 1.0),
                        ("u_strength", 1.0), ("u_glowStrength", 1.0), ("u_premultiplied", 0.0),
                        ("u_maskScaleX", 1.0), ("u_maskScaleY", 1.0), ("u_fullPaint", 0.0)):
        uniform1f(location(program, name.encode()), value)

    arrays = []
    for name, size, data in [(b"a_position", 2, [-1, -1, 1, -1, -1, 1, 1, 1]),
                             (b"a_texCoord", 2, [0, 0, 1, 0, 0, 1, 1, 1]),
                             (b"a_color", 4, [1] * 16)]:
        index = api(gl, "glGetAttribLocation", Int, UInt, ct.c_char_p)(program, name)
        array = (Float * len(data))(*data)
        arrays.append(array)
        api(gl, "glEnableVertexAttribArray", None, UInt)(index)
        api(gl, "glVertexAttribPointer", None, UInt, Int, UInt, UInt, Int, Pointer)(index, size, 0x1406, 0, 0, array)
    api(gl, "glViewport", None, Int, Int, Int, Int)(0, 0, W, W)
    api(gl, "glDrawArrays", None, UInt, Int, Int)(5, 0, 4)
    pixels = (ct.c_ubyte * (W * W * 4))()
    api(gl, "glReadPixels", None, Int, Int, Int, Int, UInt, UInt, Pointer)(0, 0, W, W, 0x1908, 0x1401, pixels)
    raw = list(pixels)
    for x in range(W):
        got = raw[x * 4:x * 4 + 4]
        want = live_pixel(texels[x], masks[x], PALETTE, (255, 0, 0), (0, 255, 255), 160, 1, 0, 0, False, True)
        assert max(abs(g - w) for g, w in zip(got, want)) <= 2, (x, got, want)
    assert api(gl, "glGetError", UInt)() == 0
    api(egl, "eglMakeCurrent", UInt, Pointer, Pointer, Pointer, Pointer)(display, None, None, None)
    api(egl, "eglTerminate", UInt, Pointer)(display)
    print(f'PASS: live_slot.glsl compiles/links and passes {W} RGBA4444 pixel checks.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--static-only', action='store_true')
    args = parser.parse_args()
    contracts()
    reference_cases()
    performance_contracts()
    if not args.static_only:
        gpu_pixels()
