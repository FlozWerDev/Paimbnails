"""Render Liquid Glass pixels with a hidden Windows OpenGL context (no game required)."""

import ctypes as ct
from ctypes import wintypes as wt
from contextlib import contextmanager
from pathlib import Path
import sys


def api(library, name, result, *arguments):
    function = getattr(library, name)
    function.restype = result
    function.argtypes = arguments
    return function


@contextmanager
def hidden_gl():
    user = ct.WinDLL("user32", use_last_error=True)
    gdi = ct.WinDLL("gdi32", use_last_error=True)
    gl = ct.WinDLL("opengl32", use_last_error=True)

    class PixelFormat(ct.Structure):
        _fields_ = [("size", wt.WORD), ("version", wt.WORD), ("flags", wt.DWORD)]
        _fields_ += [(name, ct.c_ubyte) for name in (
            "type", "color", "red", "redShift", "green", "greenShift", "blue", "blueShift",
            "alpha", "alphaShift", "accum", "accumRed", "accumGreen", "accumBlue", "accumAlpha",
            "depth", "stencil", "aux", "layer", "reserved")]
        _fields_ += [(name, wt.DWORD) for name in ("layerMask", "visibleMask", "damageMask")]

    create = api(user, "CreateWindowExW", wt.HWND, wt.DWORD, wt.LPCWSTR, wt.LPCWSTR,
                 wt.DWORD, ct.c_int, ct.c_int, ct.c_int, ct.c_int, wt.HWND,
                 wt.HMENU, wt.HINSTANCE, ct.c_void_p)
    window = create(0, "STATIC", "Liquid Glass regression", 0x80000000,
                    0, 0, 128, 64, None, None, None, None)
    assert window, ct.WinError(ct.get_last_error())
    dc = context = None
    make_current = api(gl, "wglMakeCurrent", wt.BOOL, wt.HDC, ct.c_void_p)
    try:
        dc = api(user, "GetDC", wt.HDC, wt.HWND)(window)
        descriptor = PixelFormat()
        descriptor.size, descriptor.version = ct.sizeof(PixelFormat), 1
        descriptor.flags, descriptor.color, descriptor.alpha = 0x24, 32, 8
        choose = api(gdi, "ChoosePixelFormat", ct.c_int, wt.HDC, ct.POINTER(PixelFormat))
        index = choose(dc, ct.byref(descriptor))
        assert index and api(gdi, "SetPixelFormat", wt.BOOL, wt.HDC, ct.c_int,
                             ct.POINTER(PixelFormat))(dc, index, ct.byref(descriptor))
        context = api(gl, "wglCreateContext", ct.c_void_p, wt.HDC)(dc)
        assert context and make_current(dc, context)
        proc = api(gl, "wglGetProcAddress", ct.c_void_p, ct.c_char_p)

        def load(name, result, *arguments):
            pointer = proc(name.encode())
            if pointer and pointer not in (1, 2, 3, ct.c_void_p(-1).value):
                return ct.WINFUNCTYPE(result, *arguments)(pointer)
            return api(gl, name, result, *arguments)

        yield load
    finally:
        make_current(None, None)
        if context:
            api(gl, "wglDeleteContext", wt.BOOL, ct.c_void_p)(context)
        if dc:
            api(user, "ReleaseDC", ct.c_int, wt.HWND, wt.HDC)(window, dc)
        api(user, "DestroyWindow", wt.BOOL, wt.HWND)(window)


# Cocos prepends these builtins to every program it compiles.
COCOS_PREAMBLE = ("uniform mat4 CC_PMatrix;\nuniform mat4 CC_MVMatrix;\nuniform mat4 CC_MVPMatrix;\n"
                  "uniform vec4 CC_Time;\nuniform vec4 CC_SinTime;\nuniform vec4 CC_CosTime;\n"
                  "uniform vec4 CC_Random01;\n")


def render_checks(gl):
    Int, UInt, Float, Pointer = ct.c_int, ct.c_uint, ct.c_float, ct.c_void_p
    directory = Path(__file__).resolve().parents[1] / "resources/shaders"
    W, H = 128, 64

    def compile_shader(kind, source):
        handle = gl("glCreateShader", UInt, UInt)(kind)
        text = ct.c_char_p((COCOS_PREAMBLE + source).encode())
        gl("glShaderSource", None, UInt, Int, ct.POINTER(ct.c_char_p), Pointer)(
            handle, 1, ct.byref(text), None)
        gl("glCompileShader", None, UInt)(handle)
        ok = Int()
        gl("glGetShaderiv", None, UInt, UInt, ct.POINTER(Int))(handle, 0x8B81, ct.byref(ok))
        log = ct.create_string_buffer(8192)
        gl("glGetShaderInfoLog", None, UInt, Int, Pointer, Pointer)(handle, len(log), None, log)
        assert ok.value, log.value.decode()
        return handle

    bind_attribute = gl("glBindAttribLocation", None, UInt, UInt, ct.c_char_p)

    def link(vertex_file, fragment_file):
        program = gl("glCreateProgram", UInt)()
        for kind, name in ((0x8B31, vertex_file), (0x8B30, fragment_file)):
            gl("glAttachShader", None, UInt, UInt)(
                program, compile_shader(kind, (directory / name).read_text()))
        # Same slots as Cocos (kCCVertexAttrib_Position/Color/TexCoords).
        for index, name in ((0, b"a_position"), (1, b"a_color"), (2, b"a_texCoord")):
            bind_attribute(program, index, name)
        gl("glLinkProgram", None, UInt)(program)
        ok = Int()
        gl("glGetProgramiv", None, UInt, UInt, ct.POINTER(Int))(program, 0x8B82, ct.byref(ok))
        log = ct.create_string_buffer(8192)
        gl("glGetProgramInfoLog", None, UInt, Int, Pointer, Pointer)(program, len(log), None, log)
        assert ok.value, log.value.decode()
        return program

    use = gl("glUseProgram", None, UInt)
    location = gl("glGetUniformLocation", Int, UInt, ct.c_char_p)
    current = {"program": 0}

    def uniform(name, *values):
        where = location(current["program"], name.encode())
        assert where >= 0, name
        gl(f"glUniform{len(values)}f", None, Int, *([Float] * len(values)))(where, *values)

    def sampler(name, unit):
        gl("glUniform1i", None, Int, Int)(location(current["program"], name.encode()), unit)

    active = gl("glActiveTexture", None, UInt)
    bind = gl("glBindTexture", None, UInt, UInt)
    image = gl("glTexImage2D", None, UInt, Int, Int, Int, Int, Int, UInt, UInt, Pointer)
    parameter = gl("glTexParameteri", None, UInt, UInt, Int)

    def texture(width, height, pixels, filter_mode=0x2600):
        handle = UInt()
        gl("glGenTextures", None, Int, Pointer)(1, ct.byref(handle))
        bind(0x0DE1, handle)
        image(0x0DE1, 0, 0x1908, width, height, 0, 0x1908, 0x1401, pixels)
        for key, value in ((0x2801, filter_mode), (0x2800, filter_mode),
                           (0x2802, 0x812F), (0x2803, 0x812F)):
            parameter(0x0DE1, key, value)
        return handle.value

    # Raw GL order: bottom red/green, top blue/yellow.
    sharp = texture(2, 2, (ct.c_ubyte * 16)(255, 0, 0, 255, 0, 255, 0, 255,
                                           0, 0, 255, 255, 255, 255, 0, 255))
    gray = texture(1, 1, (ct.c_ubyte * 4)(128, 128, 128, 255))
    target = texture(W, H, None)
    fbo = UInt()
    gl("glGenFramebuffers", None, Int, Pointer)(1, ct.byref(fbo))
    gl("glBindFramebuffer", None, UInt, UInt)(0x8D40, fbo)
    gl("glFramebufferTexture2D", None, UInt, UInt, UInt, UInt, Int)(
        0x8D40, 0x8CE0, 0x0DE1, target, 0)
    assert gl("glCheckFramebufferStatus", UInt, UInt)(0x8D40) == 0x8CD5
    gl("glViewport", None, Int, Int, Int, Int)(0, 0, W, H)
    gl("glClearColor", None, Float, Float, Float, Float)(0, 0, 0, 0)

    attribute_pointer = gl("glVertexAttribPointer", None, UInt, Int, UInt, ct.c_ubyte, Int, Pointer)
    enable_attribute = gl("glEnableVertexAttribArray", None, UInt)
    disable_attribute = gl("glDisableVertexAttribArray", None, UInt)
    constant_attribute = gl("glVertexAttrib4f", None, UInt, Float, Float, Float, Float)

    def quad(positions, coords):
        keep = []
        for index, data in ((0, positions), (2, coords)):
            array = (Float * 8)(*data)
            keep.append(array)
            enable_attribute(index)
            attribute_pointer(index, 2, 0x1406, 0, 0, ct.cast(array, Pointer))
        disable_attribute(1)
        gl("glClear", None, UInt)(0x4000)
        gl("glDrawArrays", None, UInt, Int, Int)(0x0005, 0, 4)
        return keep

    def pixel(x, y):
        rgba = (ct.c_ubyte * 4)()
        gl("glReadPixels", None, Int, Int, Int, Int, UInt, UInt, Pointer)(
            x, y, 1, 1, 0x1908, 0x1401, rgba)
        return tuple(rgba)

    # Plate: local units equal framebuffer pixels (ortho over the target).
    plate = link("glass_plate_vertex.glsl", "liquid_glass_ui.glsl")
    current["program"] = plate
    use(plate)
    ortho = (Float * 16)(2 / W, 0, 0, 0, 0, 2 / H, 0, 0, 0, 0, -1, 0, -1, -1, 0, 1)
    gl("glUniformMatrix4fv", None, Int, Int, ct.c_ubyte, Pointer)(
        location(plate, b"CC_MVPMatrix"), 1, 0, ortho)
    sampler("u_texture", 0)
    sampler("u_blurTex", 1)
    base = {
        "u_size": (W, H), "u_shape": (12, 12, 1, 0),
        "u_fbOrigin": (0, 0), "u_fbAxisX": (1, 0), "u_fbAxisY": (0, 1),
        "u_sharpMap": (0, 0, 1 / W, 1 / H), "u_sharpClamp": (0, 0, 1, 1),
        "u_blurMap": (0, 0, 1 / W, 1 / H), "u_blurClamp": (0, 0, 1, 1),
        "u_tint": (0.2, 0.4, 0.8, 0), "u_material": (0, 0, 0, 1),
        "u_light": (-0.55, 0.83, 0, 0), "u_shadow": (0, 4, 0, 0), "u_state": (0, 0, 1, 0),
    }

    def configure(**changes):
        for name, values in {**base, **changes}.items():
            uniform(name, *values)

    active(0x84C1)
    bind(0x0DE1, gray)
    active(0x84C0)
    bind(0x0DE1, sharp)
    positions = (0, 0, W, 0, 0, H, W, H)

    def draw(alpha=1.0, **changes):
        configure(**changes)
        constant_attribute(1, alpha, alpha, alpha, alpha)
        quad(positions, positions)

    # Clear glass, no tint/rim: every quadrant shows exactly what is behind.
    draw()
    assert pixel(32, 16) == (255, 0, 0, 255), pixel(32, 16)
    assert pixel(96, 16) == (0, 255, 0, 255), pixel(96, 16)
    assert pixel(32, 48) == (0, 0, 255, 255), pixel(32, 48)
    assert pixel(96, 48) == (255, 255, 0, 255), pixel(96, 48)
    # The rounded corner is cut out and nothing is drawn there.
    assert pixel(0, 0) == (0, 0, 0, 0), pixel(0, 0)
    # A transformed plate samples where it lands: a mirrored axis flips sides.
    draw(u_fbOrigin=(W, 0), u_fbAxisX=(-1, 0))
    assert pixel(32, 16) == (0, 255, 0, 255), pixel(32, 16)
    # Samples stay inside this capture's rectangle in the shared copy.
    draw(u_sharpClamp=(0, 0, 0.25, 1))
    assert pixel(96, 16) == (255, 0, 0, 255), pixel(96, 16)
    # Full frost shows the blurred copy; tint mixes on top of it.
    draw(u_material=(1, 0, 0, 1))
    assert pixel(64, 32) == (128, 128, 128, 255), pixel(64, 32)
    draw(u_tint=(0, 0, 1, 0.5))
    r, g, b, a = pixel(32, 16)
    assert abs(r - 128) <= 1 and g == 0 and abs(b - 128) <= 1 and a == 255, (r, g, b, a)
    # The bezel bends the rim but leaves the flat centre untouched.
    draw(u_material=(0, 12, 1, 1))
    assert pixel(32, 16) == (255, 0, 0, 255), pixel(32, 16)
    assert pixel(64, 1) != (0, 0, 0, 0)
    # Specular rim brightens the edge facing the light, not the centre.
    draw(u_light=(0, 1, 0, 1))
    top = pixel(32, H - 1)
    assert top[1] > 40 and top[2] > 40, top
    assert pixel(32, 16) == (255, 0, 0, 255), pixel(32, 16)
    # Premultiplied output follows the plate opacity.
    draw(alpha=0.5)
    r, g, b, a = pixel(32, 16)
    assert abs(r - 128) <= 1 and g == 0 and b == 0 and abs(a - 128) <= 1, (r, g, b, a)
    # The shadow lives outside the shape only.
    draw(u_shadow=(0, 8, 0.5, 0))
    assert pixel(0, 0)[3] > 0 and pixel(0, 0)[:3] == (0, 0, 0), pixel(0, 0)
    assert pixel(32, 16) == (255, 0, 0, 255), pixel(32, 16)
    # Simulated: no copy needed, tint gradient with a translucent body.
    draw(u_state=(0, 1, 1, 0))
    r, g, b, a = pixel(64, 32)
    assert r < g < b and abs(a - 219) <= 1, (r, g, b, a)
    # Pressing lifts the whole lens.
    draw(u_state=(1, 0, 1, 0))
    r, g, b, a = pixel(32, 16)
    assert r == 255 and g > 20 and b > 20, (r, g, b, a)

    # Blur pass: downsample and upsample keep a flat colour flat, and the
    # clamp keeps taps inside the source rectangle.
    blur = link("glass_pass_vertex.glsl", "glass_blur.glsl")
    current["program"] = blur
    use(blur)
    sampler("u_texture", 0)
    clip = (-1, -1, 1, -1, -1, 1, 1, 1)
    for up in (0, 1):
        bind(0x0DE1, gray)
        uniform("u_step", 0.5, 0.5, 0, 0)
        uniform("u_clamp", 0.5, 0.5, 0.5, 0.5)
        uniform("u_up", up)
        quad(clip, (0, 0, 1, 0, 0, 1, 1, 1))
        assert pixel(64, 32) == (128, 128, 128, 255), (up, pixel(64, 32))
    bind(0x0DE1, sharp)
    uniform("u_step", 0.25, 0.25, 0, 0)
    uniform("u_clamp", 0.25, 0.25, 0.25, 0.25)
    uniform("u_up", 0)
    quad(clip, (0, 0, 1, 0, 0, 1, 1, 1))
    assert pixel(100, 50) == (255, 0, 0, 255), pixel(100, 50)
    assert gl("glGetError", UInt)() == 0
    print("PASS: Liquid Glass plate and blur shaders compile/link; mapping, clamping, frost, "
          "tint, bezel, rim, opacity, shadow, simulated and press pixels")


if __name__ == "__main__":
    if sys.platform != "win32":
        raise SystemExit("This pixel regression requires Windows OpenGL.")
    with hidden_gl() as load:
        render_checks(load)
