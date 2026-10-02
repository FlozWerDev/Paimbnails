"""RTX source, numerical, and framebuffer checks without compiling shaders or the mod."""

import argparse
import ctypes as ct
import math
import os
from pathlib import Path
import random
import re
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
RENDERER = (ROOT / "src/features/rtx/services/RTXRenderer.cpp").read_text()
SHADERS = {
    name: (ROOT / f"resources/shaders/rtx_{name}.glsl").read_text()
    for name in ("common", "trace", "temporal", "atrous", "bloom", "composite")
}


def contracts():
    for name, program in (("trace", "trace"), ("temporal", "temporal"),
                          ("atrous", "atrous"), ("bloom", "bloom"),
                          ("composite", "composite")):
        uniforms = set(re.findall(r"uniform\s+\w+\s+(u_\w+)\s*;", SHADERS[name]))
        wired = set(re.findall(rf'glGetUniformLocation\({program}, "(u_\w+)"\)', RENDERER))
        wired.update(re.findall(rf'bindSampler\({program}, "(u_\w+)", \d+\)', RENDERER))
        assert uniforms == wired, (name, uniforms - wired, wired - uniforms)

    frame = RENDERER.split("void RTXRenderer::renderFrame() {", 1)[1]
    capture = frame.index("glCopyTexSubImage2D(")
    restore = frame.index("glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(state.fbo))")
    allocate = frame.index("ensureTraceTargets(")
    assert allocate < restore < capture
    assert frame.index("FrameState const state;") < frame.index("ensurePrograms()")
    assert "!m_hasHistory || m_frameCounter % cadence == 0" in frame
    assert "else {\n            invalidateHistory();" in frame
    assert "ccGLBindTexture2DN(" not in RENDERER
    assert "ccGLUseProgram(" not in RENDERER
    assert "ccGLInvalidateStateCache(" not in RENDERER

    temporal = SHADERS["temporal"]
    assert temporal.index("u_historyValid < 0.5") < temporal.index("texture2D(u_history,")
    assert "u_clampSigma >= 0.0" in temporal
    assert "cfg.ghostClamp ? cfg.clampSigma : -1.f" in RENDERER
    assert "bindTexture(4, m_guideHistory.tex)" in RENDERER
    assert "precision highp sampler2D;" in SHADERS["common"]
    for target in ("m_traceRT", "m_history[0]", "m_history[1]", "m_atrous[0]", "m_atrous[1]"):
        assert f"makeTarget({target}, w, h, true)" in RENDERER
    assert "GL_HALF_FLOAT_OES" in RENDERER and "GL_HALF_FLOAT" in RENDERER
    assert "GL_RGB, srcW, srcH, 0, GL_RGB, GL_UNSIGNED_BYTE" in RENDERER
    print("PASS: framebuffer capture, history lifecycle, HDR, and shader bindings")


def evaluate(expression, variables):
    expression = " ".join(expression.split()).replace("vec3(", "(")
    expression = re.sub(r"(\d+(?:\.\d+)?(?:e[+-]?\d+)?)f\b", r"\1", expression)
    return eval(expression, {"__builtins__": {}}, variables)


def apply_transform(transform, point):
    return (transform.a * point[0] + transform.c * point[1] + transform.tx,
            transform.b * point[0] + transform.d * point[1] + transform.ty)


def reprojection_checks():
    body = RENDERER.split("void RTXRenderer::updateReprojection() {", 1)[1]
    body = body.split("void RTXRenderer::syncGovernorEffectives", 1)[0]
    expressions = dict(re.findall(r"float const (det|a|b|c|d) = ([^;]+);", body))
    rows = [re.search(rf"m_reprojRow{i} = \{{([^}}]+)\}};", body)[1].split(",") for i in (0, 1)]
    randomizer = random.Random(7281)
    for _ in range(500):
        transforms = []
        for _ in range(2):
            angle = randomizer.uniform(-math.pi, math.pi)
            sx, sy = [randomizer.uniform(0.0005, 0.004) for _ in range(2)]
            transforms.append(SimpleNamespace(
                a=math.cos(angle) * sx, b=math.sin(angle) * sx,
                c=-math.sin(angle) * sy, d=math.cos(angle) * sy,
                tx=randomizer.uniform(-0.2, 0.2), ty=randomizer.uniform(-0.2, 0.2)))
        now, previous = transforms
        variables = {"n": now, "p": previous}
        for name in ("det", "a", "b", "c", "d"):
            variables[name] = evaluate(expressions[name], variables)
        evaluated = [[evaluate(expr, variables) for expr in row] for row in rows]
        world = (randomizer.uniform(-200, 200), randomizer.uniform(-200, 200))
        current_uv = apply_transform(now, world)
        actual = tuple(sum(row[i] * (*current_uv, 1)[i] for i in range(3)) for row in evaluated)
        expected = apply_transform(previous, world)
        assert max(abs(a - b) for a, b in zip(actual, expected)) < 1e-10
    print("PASS: 500 camera transforms with rotation, translation, and nonuniform scale")


def glsl_scalar(name, value):
    common = SHADERS["common"]
    body = re.search(rf"vec3 {name}\(vec3 c\)\s*\{{([^}}]+)\}}", common)[1]
    body = re.sub(r"//[^\n]*", "", body)
    variables = {
        "c": value, "min": min, "max": max, "sqrt": math.sqrt, "abs": abs,
        "clamp": lambda x, lo, hi: min(max(x, lo), hi),
        "kHdrCeil": 0.9995, "kU2White": 0.72519,
        "toDisplay": lambda x: math.sqrt(max(x, 0)),
        "tmU2Curve": lambda x: ((x * (0.15 * x + 0.05) + 0.004)
                               / (x * (0.15 * x + 0.50) + 0.06)) - 0.066667,
    }
    for statement in body.split(";"):
        statement = statement.strip()
        if not statement:
            continue
        if statement.startswith("return "):
            return evaluate(statement.removeprefix("return "), variables)
        variable, expression = statement.split("=", 1)
        variable = variable.removeprefix("vec3 ").strip().split(".")[0]
        if "?" in expression:
            condition, alternatives = expression.split("?", 1)
            yes, no = alternatives.split(":", 1)
            expression = f"({yes}) if ({condition}) else ({no})"
        expression = re.sub(r"\b(\w+)\.[xyz]\b", r"\1", expression)
        variables[variable] = evaluate(expression, variables)
    raise AssertionError(name)


def image_math_checks():
    composite = SHADERS["composite"]
    pairs = (("tmReinhard", "tmReinhardInv"), ("tmAces", "tmAcesInv"),
             ("tmFilmic", "tmFilmicInv"), ("tmUncharted", "tmUnchartedInv"))
    assert "col += shown - toDisplay(clamp(tonemapApply(baseHdr, u_tonemap), 0.0, 1.0));" in composite
    assert composite.index("if (u_mix <= 0.0)") < composite.index("vec3 baseHdr")
    for forward, inverse in pairs:
        for byte in range(256):
            original = byte / 255
            hdr = min(max(glsl_scalar(inverse, original * original), 0), 64)
            mapped = math.sqrt(min(max(glsl_scalar(forward, hdr), 0), 1))
            output = mapped + original - mapped
            assert math.isfinite(hdr) and math.isfinite(mapped)
            assert abs(output - original) < 1e-12, (forward, byte)

    assert SHADERS["trace"].count("u_texel / u_texel.y") == 3
    for width, height in ((1920, 1080), (2560, 1080), (1080, 1920), (1280, 64)):
        for angle in range(0, 360, 7):
            radians = math.radians(angle)
            dx = math.cos(radians) * height / width
            dy = math.sin(radians)
            distance_px = math.hypot(dx * width, dy * height)
            assert abs(distance_px - height) < 1e-9
    assert "bool linearSteps = g < 1.001;" in SHADERS["trace"]
    for steps in (4, 10, 14, 24, 32):
        for growth in (1.0, 1.0001, 1.001, 1.28, 1.5):
            positions = [(i + 0.5) / steps if growth < 1.001 else
                         (growth ** (i + 0.5) - 1) / (growth ** steps - 1)
                         for i in range(steps)]
            assert 0 < positions[0] < positions[-1] < 1
            assert all(a < b for a, b in zip(positions, positions[1:]))

    assert "* u_ca * 2.0 * u_texel" in composite
    assert "gi * u_giStrength + refl + trans" in SHADERS["trace"]
    assert "u_giStrength" not in composite
    assert "max(phi / (1.0 + blur * kVarGain), 8.0)" in SHADERS["atrous"]
    assert "atan(" not in SHADERS["bloom"] and "u_frame" not in SHADERS["bloom"]
    print("PASS: neutral colors, aspect ratios, ray endpoints, independent reflections, and stable rays")


def framebuffer_pixels():
    os.environ.setdefault("EGL_PLATFORM", "surfaceless")
    egl, gl = ct.CDLL("libEGL.so.1"), ct.CDLL("libGLESv2.so.2")
    integer, uint, floating, pointer = ct.c_int, ct.c_uint, ct.c_float, ct.c_void_p

    def api(lib, name, result, *arguments):
        function = getattr(lib, name)
        function.restype, function.argtypes = result, arguments
        return function

    display = api(egl, "eglGetDisplay", pointer, pointer)(None)
    assert api(egl, "eglInitialize", uint, pointer, pointer, pointer)(display, None, None)
    attributes = (integer * 13)(0x3033, 1, 0x3040, 4, 0x3024, 8, 0x3023, 8,
                                0x3022, 8, 0x3021, 8, 0x3038)
    config, count = pointer(), integer()
    assert api(egl, "eglChooseConfig", uint, pointer, pointer, pointer, integer, pointer)(
        display, attributes, ct.byref(config), 1, ct.byref(count)) and count.value
    context = api(egl, "eglCreateContext", pointer, pointer, pointer, pointer, pointer)(
        display, config, None, (integer * 3)(0x3098, 3, 0x3038))
    surface = api(egl, "eglCreatePbufferSurface", pointer, pointer, pointer, pointer)(
        display, config, (integer * 5)(0x3057, 80, 0x3056, 48, 0x3038))
    assert api(egl, "eglMakeCurrent", uint, pointer, pointer, pointer, pointer)(
        display, surface, surface, context)
    gen_texture = api(gl, "glGenTextures", None, integer, pointer)
    active = api(gl, "glActiveTexture", None, uint)
    bind_texture = api(gl, "glBindTexture", None, uint, uint)
    image = api(gl, "glTexImage2D", None, uint, integer, integer, integer, integer,
                integer, uint, uint, pointer)
    parameter = api(gl, "glTexParameteri", None, uint, uint, integer)
    gen_fbo = api(gl, "glGenFramebuffers", None, integer, pointer)
    bind_fbo = api(gl, "glBindFramebuffer", None, uint, uint)
    attach = api(gl, "glFramebufferTexture2D", None, uint, uint, uint, uint, integer)
    status = api(gl, "glCheckFramebufferStatus", uint, uint)
    clear_color = api(gl, "glClearColor", None, floating, floating, floating, floating)
    clear = api(gl, "glClear", None, uint)
    viewport = api(gl, "glViewport", None, integer, integer, integer, integer)
    copy = api(gl, "glCopyTexSubImage2D", None, uint, integer, integer, integer,
               integer, integer, integer, integer)
    read = api(gl, "glReadPixels", None, integer, integer, integer, integer, uint, uint, pointer)
    error = api(gl, "glGetError", uint)
    targets = []

    def target(width, height, hdr=False, rgb=False):
        texture, fbo = uint(), uint()
        gen_texture(1, ct.byref(texture))
        active(0x84C0)
        bind_texture(0x0DE1, texture.value)
        format_value = 0x1907 if rgb else 0x1908
        image(0x0DE1, 0, 0x881A if hdr else format_value, width, height, 0,
              format_value, 0x140B if hdr else 0x1401, None)
        for name in (0x2801, 0x2800):
            parameter(0x0DE1, name, 0x2601)
        for name in (0x2802, 0x2803):
            parameter(0x0DE1, name, 0x812F)
        gen_fbo(1, ct.byref(fbo))
        bind_fbo(0x8D40, fbo.value)
        attach(0x8D40, 0x8CE0, 0x0DE1, texture.value, 0)
        assert status(0x8D40) == 0x8CD5 and error() == 0
        targets.append((texture, fbo))
        return texture.value, fbo.value

    try:
        _, source_fbo = target(80, 48, rgb=True)
        clear_color(0.2, 0.4, 0.8, 1)
        clear(0x4000)
        api(gl, "glEnable", None, uint)(0x0C11)
        api(gl, "glScissor", None, integer, integer, integer, integer)(19, 7, 37, 23)
        clear_color(0.8, 0.2, 0.4, 1)
        clear(0x4000)
        api(gl, "glDisable", None, uint)(0x0C11)
        captured_tex, captured_fbo = target(37, 23, rgb=True)
        decoy_tex, _ = target(37, 23)
        clear_color(0, 1, 0, 1)
        clear(0x4000)
        viewport(0, 0, 37, 23)
        active(0x84C0 + 4)
        bind_texture(0x0DE1, decoy_tex)
        bind_fbo(0x8D40, source_fbo)
        active(0x84C0)
        bind_texture(0x0DE1, captured_tex)
        copy(0x0DE1, 0, 0, 0, 19, 7, 37, 23)
        assert error() == 0
        bind_fbo(0x8D40, captured_fbo)
        pixels = (ct.c_ubyte * (37 * 23 * 4))()
        read(0, 0, 37, 23, 0x1908, 0x1401, pixels)
        assert error() == 0
        expected = (204, 51, 102, 255)
        assert all(abs(pixels[i] - expected[i % 4]) <= 1 for i in range(len(pixels)))

        target(13, 9, hdr=True)
        clear_color(3, 1.5, 0.5, 1)
        clear(0x4000)
        result = (floating * 4)()
        read(0, 0, 1, 1, 0x1908, 0x1406, result)
        assert error() == 0
        assert all(abs(a - b) < 0.001 for a, b in zip(result, (3, 1.5, 0.5, 1)))
        print("PASS: GLES framebuffer pixels at nonzero viewport origin and HDR values above one")
    finally:
        for texture, fbo in targets:
            api(gl, "glDeleteFramebuffers", None, integer, pointer)(1, ct.byref(fbo))
            api(gl, "glDeleteTextures", None, integer, pointer)(1, ct.byref(texture))
        api(egl, "eglMakeCurrent", uint, pointer, pointer, pointer, pointer)(display, None, None, None)
        api(egl, "eglDestroySurface", uint, pointer, pointer)(display, surface)
        api(egl, "eglDestroyContext", uint, pointer, pointer)(display, context)
        api(egl, "eglTerminate", uint, pointer)(display)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--static-only", action="store_true", help="Skip EGL framebuffer checks")
    args = parser.parse_args()
    contracts()
    reprojection_checks()
    image_math_checks()
    if not args.static_only:
        framebuffer_pixels()
