#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace paimon::texture_studio {

// tone code stored in the live role mask. Keep the order in sync with
// toneForCode() in resources/shaders/live_slot.glsl.
enum class LiveTone : std::uint8_t {
    Primary = 0,  // GD green UI (main buttons, arrows)
    Secondary,    // cyan UI
    Blue,         // blue / blue-gray UI
    Pink,         // pink and purple UI
    Red,          // red UI (delete, cancel)
    Surface,      // brown, orange and gold UI (popups, lists, frames)
    Neutral,      // gray and white UI
    Background,   // scene backgrounds (painted without analysis)
    Count
};

// vanilla color family of an HSV color (h in degrees, s/v in 0..1).
LiveTone liveToneFamily(float hueDeg, float sat, float val);

struct LiveMaskOptions {
    float softness = .35f;
    int clusterPrecision = 5;
    int edgeCleanup = 1;
    int alphaCutoff = 16;
};

// frame slot in atlas pixels (rotated frames use their transposed slot).
struct LiveFrameRect {
    int x = 0, y = 0, w = 0, h = 0;
};

// builds the GPU role mask for live UI painting. Each texel is RGBA4444:
// r = body, g = detail, b = glow (fractions of the pixel's own coverage)
// and a = LiveTone code of the body. A zero texel keeps the original pixel.
//
// the analysis works on the distinct colors of a frame instead of every
// pixel: k-means, nearest-cluster lookups and the mask split run once per
// color, then a per-pixel pass only reads the lookup tables.
class LiveMaskAnalyzer final {
public:
    // analyzes one frame of a straight-alpha RGBA8 atlas into `out`
    // (atlasW * atlasH texels). Only texels inside `rect` are written.
    void analyze(std::uint8_t const* atlas, int atlasW, int atlasH,
                 LiveFrameRect rect, LiveMaskOptions const& options,
                 std::uint16_t* out);

    // analyzes every rect on up to `threads` workers. Overlapping rects run
    // on one worker. Returns false when cancelled; throws on worker errors.
    // `framesTotal` receives the count left after dropping duplicate/invalid rects.
    static bool analyzeAll(std::uint8_t const* atlas, int atlasW, int atlasH,
                           std::vector<LiveFrameRect> rects,
                           LiveMaskOptions const& options, std::uint16_t* out,
                           unsigned threads, std::function<bool()> const& cancelled,
                           std::atomic<int>* framesDone = nullptr,
                           std::atomic<int>* framesTotal = nullptr);

private:
    struct Color {
        std::uint32_t rgb = 0;
        float weight = 0.f;
        int count = 0;
        float h = 0.f, s = 0.f, v = 0.f;
        // nearest clusters and the soft split towards the second one.
        int nearest = -1;
        int second = -1;
        float secondShare = 0.f;
    };

    int findOrInsert(std::uint32_t rgb);

    std::vector<Color> m_colors;
    std::vector<std::int32_t> m_table;
    std::uint32_t m_tableMask = 0;
    std::vector<std::int32_t> m_pixelColor;
    std::vector<std::uint8_t> m_alpha;
    // role planes in alpha units: c1, c2, glow, outline, detail.
    std::vector<std::uint8_t> m_planes[5];
    std::vector<std::uint8_t> m_scratch[5];
    std::vector<std::uint8_t> m_visited;
    std::vector<int> m_stack;
    std::vector<int> m_component;
};

}  // namespace paimon::texture_studio
