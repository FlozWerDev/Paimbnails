"""gles2 tint pixel regression; requires mesa egl/gles2 shared libraries."""
import ctypes as ct
import math
import os
from pathlib import Path

os.environ.setdefault("EGL_PLATFORM", "surfaceless")
egl = ct.CDLL("libEGL.so.1")
gl = ct.CDLL("libGLESv2.so.2")
Int, UInt, Float, Pointer = ct.c_int, ct.c_uint, ct.c_float, ct.c_void_p


def api(lib, name, result, *args):
    function = getattr(lib, name)
    function.restype = result
    function.argtypes = args
    return function


def shader(kind, source):
    handle = api(gl, "glCreateShader", UInt, UInt)(kind)
    text = ct.c_char_p(source.encode())
    api(gl, "glShaderSource", None, UInt, Int, ct.POINTER(ct.c_char_p), Pointer)(
        handle, 1, ct.byref(text), None
    )
    api(gl, "glCompileShader", None, UInt)(handle)
    success = Int()
    api(gl, "glGetShaderiv", None, UInt, UInt, ct.POINTER(Int))(
        handle, 0x8B81, ct.byref(success)
    )
    log = ct.create_string_buffer(8192)
    api(gl, "glGetShaderInfoLog", None, UInt, Int, Pointer, Pointer)(
        handle, len(log), None, log
    )
    assert success.value, log.value.decode()
    return handle


def lum(r, g, b):
    return 0.30 * r + 0.59 * g + 0.11 * b


def rnd(x):
    return min(max(math.floor(x + 0.5), 0), 255)


def tint(src, color, bright, sat, con):
    v = [min(max(c * (lum(*src) / bright), 0.0), 255.0) for c in color]
    if sat != 1.0:
        l = lum(*v)
        v = [l + (x - l) * sat for x in v]
    if con != 0.0:
        v = [(x - 127.5) * (1.0 + con) + 127.5 for x in v]
    return [rnd(x) for x in v]


def blend(base, base_a, tinted, w, replace):
    if w == 0:
        return base, base_a
    if replace or w == 255:
        return tinted, max(base_a, w)
    a = w / 255.0
    return [rnd(t * a + b * (1.0 - a)) for t, b in zip(tinted, base)], max(base_a, w)


def reference(src, m, colors, bright, sat, con, thresh, replace, apply_detail):
    r, g, b, a = src
    if a == 0:
        return src
    if thresh > 0 and lum(r, g, b) < thresh:
        return src
    base, out_a = [r, g, b], a
    roles = [(m[0], colors[0], False), (m[1], colors[1], False),
             (m[2] if apply_detail else 0, colors[2], False),
             (m[3], colors[3], replace)]
    for w, color, rep in roles:
        if w > 0:
            base, out_a = blend(base, out_a, tint([r, g, b], color, bright, sat, con), w, rep)
    return base + [out_a]


W = 8
# bottom-up rows: readback pixel (x, y) samples texel row y from this array.
BASE = [
    [(200, 100, 50, 255), (100, 150, 200, 255), (180, 180, 180, 255), (90, 90, 200, 255),
     (10, 10, 10, 255), (123, 45, 67, 0), (150, 150, 150, 255), (77, 88, 99, 255)],
] + [[(0, 0, 0, 0)] * W for _ in range(W - 1)]
MASK = [
    [(255, 0, 0, 0), (0, 128, 0, 0), (0, 0, 255, 0), (0, 0, 0, 200),
     (255, 0, 0, 0), (255, 255, 255, 255), (255, 128, 0, 0), (0, 0, 0, 0)],
] + [[(0, 0, 0, 0)] * W for _ in range(W - 1)]

C1 = (149, 226, 3)
C2 = (28, 233, 255)
WHITE = (255, 255, 255)
GLOW = (0, 255, 255)

CASES = [
    # name, colors(c1,c2,detail,glow), bright, sat, con, thresh, replace, applydetail
    ("base", (C1, C2, WHITE, GLOW), 160, 1.0, 0.0, 0, False, False),
    ("detail", (C1, C2, (255, 0, 0), GLOW), 160, 1.0, 0.0, 0, False, True),
    ("replace", (C1, C2, WHITE, GLOW), 160, 1.0, 0.0, 0, True, False),
    ("darkcut", (C1, C2, WHITE, GLOW), 160, 1.0, 0.0, 20, False, False),
    ("grade", (C1, C2, WHITE, GLOW), 100, 0.0, 0.5, 0, False, False),
]


def run(fragment_name="tint_preview.glsl", extra_uniforms=None, reference_pixel=reference):
    display = api(egl, "eglGetDisplay", Pointer, Pointer)(None)
    assert api(egl, "eglInitialize", UInt, Pointer, Pointer, Pointer)(display, None, None)
    attributes = (Int * 13)(0x3033, 1, 0x3040, 4, 0x3024, 8, 0x3023, 8,
                            0x3022, 8, 0x3021, 8, 0x3038)
    config, count = Pointer(), Int()
    assert api(egl, "eglChooseConfig", UInt, Pointer, Pointer, Pointer, Int, Pointer)(
        display, attributes, ct.byref(config), 1, ct.byref(count)
    ) and count.value
    context = api(egl, "eglCreateContext", Pointer, Pointer, Pointer, Pointer, Pointer)(
        display, config, None, (Int * 3)(0x3098, 2, 0x3038)
    )
    surface = api(egl, "eglCreatePbufferSurface", Pointer, Pointer, Pointer, Pointer)(
        display, config, (Int * 5)(0x3057, 64, 0x3056, 64, 0x3038)
    )
    assert api(egl, "eglMakeCurrent", UInt, Pointer, Pointer, Pointer, Pointer)(
        display, surface, surface, context
    )
    directory = Path(__file__).resolve().parents[1] / "resources" / "shaders"
    vertex = shader(0x8B31, "uniform mat4 CC_MVPMatrix;\n" +
                    (directory / "cell_vertex.glsl").read_text())
    fragment = shader(0x8B30, (directory / fragment_name).read_text())
    program = api(gl, "glCreateProgram", UInt)()
    attach = api(gl, "glAttachShader", None, UInt, UInt)
    attach(program, vertex)
    attach(program, fragment)
    api(gl, "glLinkProgram", None, UInt)(program)
    success = Int()
    api(gl, "glGetProgramiv", None, UInt, UInt, ct.POINTER(Int))(
        program, 0x8B82, ct.byref(success)
    )
    log = ct.create_string_buffer(8192)
    api(gl, "glGetProgramInfoLog", None, UInt, Int, Pointer, Pointer)(
        program, len(log), None, log
    )
    assert success.value, log.value.decode()
    api(gl, "glUseProgram", None, UInt)(program)
    location = api(gl, "glGetUniformLocation", Int, UInt, ct.c_char_p)
    uniform1i = api(gl, "glUniform1i", None, Int, Int)
    uniform1f = api(gl, "glUniform1f", None, Int, Float)
    uniform3f = api(gl, "glUniform3f", None, Int, Float, Float, Float)
    for name in [b"u_texture", b"u_roleMask", b"u_color1", b"u_color2",
                 b"u_detailColor", b"u_glowColor", b"u_brightness",
                 b"u_saturation", b"u_contrast", b"u_darkThreshold",
                 b"u_glowReplace", b"u_applyDetail", b"CC_MVPMatrix"]:
        assert location(program, name) != -1, name
    for name, value in (extra_uniforms or {}).items():
        loc = location(program, name.encode())
        assert loc != -1, name
        uniform1f(loc, value)
    identity = (Float * 16)(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
    api(gl, "glUniformMatrix4fv", None, Int, Int, UInt, Pointer)(
        location(program, b"CC_MVPMatrix"), 1, 0, identity
    )

    gen_tex = api(gl, "glGenTextures", None, Int, Pointer)
    bind_tex = api(gl, "glBindTexture", None, UInt, UInt)
    tex_param = api(gl, "glTexParameteri", None, UInt, UInt, Int)
    tex_image = api(gl, "glTexImage2D", None, UInt, Int, Int, Int, Int, Int, UInt, UInt, Pointer)
    active_tex = api(gl, "glActiveTexture", None, UInt)

    def upload(grid, unit):
        flat = [c for row in grid for px in row for c in px]
        data = (ct.c_ubyte * len(flat))(*flat)
        tex = UInt()
        gen_tex(1, ct.byref(tex))
        active_tex(0x84C0 + unit)
        bind_tex(0x0DE1, tex.value)
        for param, value in [(0x2801, 0x2600), (0x2800, 0x2600),
                             (0x2802, 0x812F), (0x2803, 0x812F)]:
            tex_param(0x0DE1, param, value)
        tex_image(0x0DE1, 0, 0x1908, W, W, 0, 0x1908, 0x1401, data)

    upload(BASE, 0)
    upload(MASK, 1)
    uniform1i(location(program, b"u_texture"), 0)
    uniform1i(location(program, b"u_roleMask"), 1)

    arrays = []
    for name, size, data in [
        (b"a_position", 2, [-1, -1, 1, -1, -1, 1, 1, 1]),
        (b"a_texCoord", 2, [0, 0, 1, 0, 0, 1, 1, 1]),
        (b"a_color", 4, [1] * 16),
    ]:
        index = api(gl, "glGetAttribLocation", Int, UInt, ct.c_char_p)(program, name)
        array = (Float * len(data))(*data)
        arrays.append(array)
        api(gl, "glEnableVertexAttribArray", None, UInt)(index)
        api(gl, "glVertexAttribPointer", None, UInt, Int, UInt, UInt, Int, Pointer)(
            index, size, 0x1406, 0, 0, array
        )
    api(gl, "glViewport", None, Int, Int, Int, Int)(0, 0, W, W)
    draw = api(gl, "glDrawArrays", None, UInt, Int, Int)
    read = api(gl, "glReadPixels", None, Int, Int, Int, Int, UInt, UInt, Pointer)
    pixels = (ct.c_ubyte * (W * W * 4))()

    checked = 0
    for name, colors, bright, sat, con, thresh, replace, apply_detail in CASES:
        uniform3f(location(program, b"u_color1"), *colors[0])
        uniform3f(location(program, b"u_color2"), *colors[1])
        uniform3f(location(program, b"u_detailColor"), *colors[2])
        uniform3f(location(program, b"u_glowColor"), *colors[3])
        uniform1f(location(program, b"u_brightness"), bright)
        uniform1f(location(program, b"u_saturation"), sat)
        uniform1f(location(program, b"u_contrast"), con)
        uniform1f(location(program, b"u_darkThreshold"), thresh)
        uniform1f(location(program, b"u_glowReplace"), 1.0 if replace else 0.0)
        uniform1f(location(program, b"u_applyDetail"), 1.0 if apply_detail else 0.0)
        draw(5, 0, 4)
        read(0, 0, W, W, 0x1908, 0x1401, pixels)
        raw = list(pixels)
        for y in range(W):
            for x in range(W):
                got = raw[(y * W + x) * 4:(y * W + x) * 4 + 4]
                want = list(reference_pixel(BASE[y][x], MASK[y][x], colors, bright,
                                      sat, con, thresh, replace, apply_detail))
                if BASE[y][x] == want:
                    assert got == want, (name, x, y, got, want)
                else:
                    diff = max(abs(g - w) for g, w in zip(got, want))
                    assert diff <= 2, (name, x, y, got, want)
                checked += 1
    assert api(gl, "glGetError", UInt)() == 0
    api(egl, "eglMakeCurrent", UInt, Pointer, Pointer, Pointer, Pointer)(
        display, None, None, None
    )
    api(egl, "eglDestroySurface", UInt, Pointer, Pointer)(display, surface)
    api(egl, "eglDestroyContext", UInt, Pointer, Pointer)(display, context)
    api(egl, "eglTerminate", UInt, Pointer)(display)
    print(f"PASS: {fragment_name} compiles/links and passes {checked} pixel checks "
          f"across {len(CASES)} uniform cases.")


if __name__ == "__main__":
    run()
