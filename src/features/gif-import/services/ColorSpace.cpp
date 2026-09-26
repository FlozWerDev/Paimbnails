#include "ColorSpace.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace paimon::gifimport {

namespace {

// sRGB->linear is a 256-value single-channel function: the table saves three
// pow()s per pixel of quantization.
const std::array<float, 256>& linearTable() {
    static const std::array<float, 256> table = [] {
        std::array<float, 256> values{};
        for (int i = 0; i < 256; ++i) {
            float const c = static_cast<float>(i) / 255.f;
            values[static_cast<std::size_t>(i)] =
                c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        return values;
    }();
    return table;
}

float channelToLinear(std::uint8_t value) {
    return linearTable()[static_cast<std::size_t>(value)];
}

} // namespace

OkLab rgbToOkLab(Color color) {
    float const r = channelToLinear(color.r);
    float const g = channelToLinear(color.g);
    float const b = channelToLinear(color.b);

    float const l = 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b;
    float const m = 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b;
    float const s = 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b;

    float const lRoot = std::cbrt(l);
    float const mRoot = std::cbrt(m);
    float const sRoot = std::cbrt(s);

    return {
        0.2104542553f * lRoot + 0.7936177850f * mRoot - 0.0040720468f * sRoot,
        1.9779984951f * lRoot - 2.4285922050f * mRoot + 0.4505937099f * sRoot,
        0.0259040371f * lRoot + 0.7827717662f * mRoot - 0.8086757660f * sRoot
    };
}

float oklabDistance(OkLab const& first, OkLab const& second) {
    float const dL = first.L - second.L;
    float const da = first.a - second.a;
    float const db = first.b - second.b;
    return std::sqrt(dL * dL + da * da + db * db);
}

Color oklabToRgb(OkLab color) {
    float const lRoot = color.L + 0.3963377774f * color.a + 0.2158037573f * color.b;
    float const mRoot = color.L - 0.1055613458f * color.a - 0.0638541728f * color.b;
    float const sRoot = color.L - 0.0894841775f * color.a - 1.2914855480f * color.b;

    float const l = lRoot * lRoot * lRoot;
    float const m = mRoot * mRoot * mRoot;
    float const s = sRoot * sRoot * sRoot;

    float const r = +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    float const g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    float const b = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;

    auto channel = [](float linear) {
        float const c = linear <= 0.0031308f
            ? linear * 12.92f
            : 1.055f * std::pow(linear, 1.f / 2.4f) - 0.055f;
        return static_cast<std::uint8_t>(std::clamp(std::lround(c * 255.f), 0L, 255L));
    };
    return {channel(r), channel(g), channel(b)};
}

} // namespace paimon::gifimport
