"""Headless GLES2 regression. Run with python3 tests/death_animation_shader_regression.py.

Requires Mesa EGL/GLES2 shared libraries; no third-party Python packages.
Checks actual shader compilation, linking, and pixels for every animation.
"""
import ctypes as ct
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
    log = ct.create_string_buffer(4096)
    api(gl, "glGetShaderInfoLog", None, UInt, Int, Pointer, Pointer)(
        handle, len(log), None, log
    )
    assert success.value, log.value.decode()
    return handle


def run():
    display = api(egl, "eglGetDisplay", Pointer, Pointer)(None)
    assert api(egl, "eglInitialize", UInt, Pointer, Pointer, Pointer)(display, None, None)
    # Pbuffer surface, GLES2, RGBA8 (alpha must not be discarded).
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
        display, config, (Int * 5)(0x3057, 128, 0x3056, 128, 0x3038)
    )
    assert api(egl, "eglMakeCurrent", UInt, Pointer, Pointer, Pointer, Pointer)(
        display, surface, surface, context
    )
    directory = Path(__file__).resolve().parents[1] / "resources" / "shaders"
    vertex = shader(0x8B31, "uniform mat4 CC_MVPMatrix;\n" +
                    (directory / "position.vert").read_text())
    fragment = shader(0x8B30, (directory / "death_animation.fsh").read_text())
    program = api(gl, "glCreateProgram", UInt)()
    attach = api(gl, "glAttachShader", None, UInt, UInt)
    attach(program, vertex)
    attach(program, fragment)
    api(gl, "glLinkProgram", None, UInt)(program)
    success = Int()
    api(gl, "glGetProgramiv", None, UInt, UInt, ct.POINTER(Int))(
        program, 0x8B82, ct.byref(success)
    )
    log = ct.create_string_buffer(4096)
    api(gl, "glGetProgramInfoLog", None, UInt, Int, Pointer, Pointer)(
        program, len(log), None, log
    )
    assert success.value, log.value.decode()
    api(gl, "glUseProgram", None, UInt)(program)
    location = api(gl, "glGetUniformLocation", Int, UInt, ct.c_char_p)
    uniform = api(gl, "glUniform1f", None, Int, Float)
    identity = (Float * 16)(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
    api(gl, "glUniformMatrix4fv", None, Int, Int, UInt, Pointer)(
        location(program, b"CC_MVPMatrix"), 1, 0, identity
    )
    api(gl, "glUniform3f", None, Int, Float, Float, Float)(
        location(program, b"u_tint"), 0.4, 0.8, 1.0
    )
    # Retain client-side vertex arrays until rendering has finished.
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
    api(gl, "glViewport", None, Int, Int, Int, Int)(0, 0, 128, 128)
    draw = api(gl, "glDrawArrays", None, UInt, Int, Int)
    read = api(gl, "glReadPixels", None, Int, Int, Int, Int, UInt, UInt, Pointer)
    pixels = (ct.c_ubyte * (128 * 128 * 4))()
    for style in range(12):
        uniform(location(program, b"u_style"), style)
        for time in [0, 0.15, 0.4, 0.7, 1]:
            uniform(location(program, b"u_progress"), time)
            draw(5, 0, 4)
            read(0, 0, 128, 128, 0x1908, 0x1401, pixels)
            alpha = list(pixels)[3::4]
            visible = max(alpha) > 0
            assert visible == (0 < time < 1), (style, time, "unexpected visibility")
            corners = [0, 127, 127 * 128, 128 * 128 - 1]
            assert all(alpha[index] == 0 for index in corners), (style, time, "edge leak")
    assert api(gl, "glGetError", UInt)() == 0
    api(egl, "eglMakeCurrent", UInt, Pointer, Pointer, Pointer, Pointer)(
        display, None, None, None
    )
    api(egl, "eglDestroySurface", UInt, Pointer, Pointer)(display, surface)
    api(egl, "eglDestroyContext", UInt, Pointer, Pointer)(display, context)
    api(egl, "eglTerminate", UInt, Pointer)(display)
    print("PASS: 12 GLES2 styles compile/link and pass 60 rendered-frame checks.")


if __name__ == "__main__":
    run()
