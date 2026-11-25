#include "../src/features/backgrounds/services/WallpaperLayout.hpp"
#include <cassert>
#include <cmath>

using namespace paimon::wallpaper;

int main() {
    // Changing frame resolution never changes the displayed viewport.
    for (auto const width : {1.f, 960.f, 1280.f, 1920.f}) {
        auto const fit = fitWallpaper(width, width * 9.f / 16.f, 1366.f, 768.f, "cover");
        assert(fit.width >= 1365.9f && fit.height >= 767.9f);
        assert(std::abs(fit.left * 2.f + fit.width - 1366.f) < .01f);
        assert(std::abs(fit.bottom * 2.f + fit.height - 768.f) < .01f);
    }
    auto portrait = fitWallpaper(540.f, 960.f, 960.f, 540.f, "contain");
    assert(std::abs(portrait.height - 540.f) < .01f && portrait.left > 300.f);
    auto cover = fitWallpaper(540.f, 960.f, 960.f, 540.f, "cover");
    assert(cover.width == 960.f && cover.bottom < 0.f);
    auto stretch = fitWallpaper(540.f, 960.f, 960.f, 540.f, "stretch");
    assert(stretch.width == 960.f && stretch.height == 540.f);
    assert(stretch.left == 0.f && stretch.bottom == 0.f);
    auto preview = fitWallpaper(1280.f, 720.f, 240.f, 135.f, "cover");
    assert(preview.width == 240.f && preview.height == 135.f);
    for (float contentScale : {1.f, 2.f, 4.f}) {
        auto const logical = fitWallpaper(1280.f / contentScale, 720.f / contentScale, 640.f, 360.f, "cover");
        assert(logical.width == 640.f && logical.height == 360.f);
        assert(logical.left == 0.f && logical.bottom == 0.f);
    }
}
