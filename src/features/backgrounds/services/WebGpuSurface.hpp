#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace paimon::wallpaper {

struct WebGpuShared;

// gL-thread side of the zero-copy web wallpaper path. Maps a private D3D11
// texture into GL with WGL_NV_DX_interop2 and copies each new browser frame
// into it on the gpu. The GL texture stays locked for GL except during that
// copy, so ordinary draws can sample it at any time.
//
// every method must run on the thread that owns the current GL context.
class WebGpuSurface {
public:
    enum class Result { Unchanged, Updated, Failed };

    // genTexture/deleteTexture let the caller keep its own GL binding cache
    // (cocos) consistent; they default to glGenTextures/glDeleteTextures.
    WebGpuSurface(std::function<unsigned()> genTexture = {}, std::function<void(unsigned)> deleteTexture = {});
    ~WebGpuSurface();
    WebGpuSurface(WebGpuSurface const&) = delete;
    WebGpuSurface& operator=(WebGpuSurface const&) = delete;

    // driver exposes WGL_NV_DX_interop2 for the current context.
    static bool supported();

    // copies `shared` into the GL texture when `changed` or on first use.
    Result update(WebGpuShared const& shared, bool changed);

    unsigned texture() const;
    int width() const;
    int height() const;
    // true right after update() created a new GL texture name.
    bool textureRecreated() const;

    // unregisters and deletes everything; the old context must be current.
    void release();

private:
    struct State;
    std::unique_ptr<State> m_state;
};

} // namespace paimon::wallpaper
