#pragma once

// private to the web wallpaper renderer and its GL-side surface.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <cstdint>

namespace paimon::wallpaper {

struct WebGpuShared {
    // created by the capture worker with multithread protection enabled, so
    // the GL thread may issue copies on the same immediate context.
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    // latest composed frame (B8G8R8A8). Replaced, never resized, on a size
    // change; holders of an older WebGpuShared keep their texture alive.
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    int width = 0;
    int height = 0;
    uint64_t generation = 0;
};

} // namespace paimon::wallpaper
#endif
