#include "../src/features/backgrounds/services/WebWallpaperRenderer.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

using namespace paimon::wallpaper;

namespace {

std::string readText(std::filesystem::path const& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

bool pixel(WallpaperFrame const& frame, int x, int y, int r, int g, int b) {
    if (x >= frame.width || y >= frame.height || frame.rgba.empty()) return false;
    auto const offset = (static_cast<size_t>(y) * frame.width + x) * 4;
    return std::abs(frame.rgba[offset] - r) < 8 && std::abs(frame.rgba[offset + 1] - g) < 8 &&
        std::abs(frame.rgba[offset + 2] - b) < 8;
}

template<typename Predicate>
bool awaitFrame(WebWallpaperRenderer& renderer, WallpaperFrame& frame, Predicate predicate) {
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto error = renderer.error(); !error.empty()) { std::cerr << error << std::endl; return false; }
        if (renderer.copyLatestFrame(frame.sequence, frame) && predicate(frame)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
    std::cerr << "Timed out waiting for input result; frame " << frame.width << 'x' << frame.height << std::endl;
    return false;
}

void saveFrame(WallpaperFrame const& frame, std::filesystem::path const& path) {
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
    for (size_t i = 0; i + 3 < frame.rgba.size(); i += 4) {
        file.write(reinterpret_cast<char const*>(frame.rgba.data() + i), 3);
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3 || argc > 4) { std::cerr << "Usage: web-test output-directory bridge.js [original-web-folder]\n"; return 2; }
    if (!WebWallpaperRenderer::isAvailable()) { std::cerr << "WebView2 unavailable\n"; return 3; }
    std::filesystem::path const output(argv[1]);
    auto const root = output / std::filesystem::path(u8"web-fixture-ñ-雪");
    std::filesystem::create_directories(root);
    std::ofstream(root / "project.json") << R"({"type":"web","file":"index.html","general":{"properties":{"test":{"value":9}}}})";
    std::ofstream(root / "index.html") << R"HTML(<!doctype html><style>html,body{margin:0;width:100%;height:100%;overflow:hidden}canvas{width:100%;height:100%;display:block}</style><canvas></canvas><script>
const c=document.querySelector('canvas');c.width=innerWidth;c.height=innerHeight;const g=c.getContext('2d');
let moved=false,down=false,up=false,audio=false,properties=false,fpsOk=false;
function paint(){g.fillStyle='rgb(18,52,86)';g.fillRect(0,0,c.width,c.height);let colors=[moved?'#00ff00':'#ff0000',down?'#0000ff':'#ff0000',up?'#ffff00':'#ff0000',audio?'#ff00ff':'#ff0000',properties?'#00ffff':'#ff0000',fpsOk?'#00ff00':'#ff0000'];colors.forEach((color,i)=>{g.fillStyle=color;g.fillRect(i*100,0,100,100)})}
c.addEventListener('mousemove',e=>{moved=e.isTrusted&&Math.abs(e.clientX-innerWidth*.75)<3&&Math.abs(e.clientY-innerHeight*.5)<3;paint()});
c.addEventListener('mousedown',e=>{down=e.isTrusted&&e.button===0;paint()});c.addEventListener('mouseup',e=>{up=e.isTrusted&&e.button===0;paint()});
wallpaperRegisterAudioListener(values=>{audio=values.length===128&&values[0]>.25&&values[64]>.25;paint()});
window.wallpaperPropertyListener={applyUserProperties:p=>{properties=p.test.value===9;paint()},applyGeneralProperties:p=>{fpsOk=p.fps>=60;paint()}};paint();
</script>)HTML";
    WebWallpaperOptions options;
    options.root = std::filesystem::absolute(root);
    options.browserStorage = std::filesystem::absolute(output / "webview-profile");
    options.entryFile = "index.html";
    options.bridgeScript = readText(argv[2]);
    options.width = 960; options.height = 540;
    for (int iteration = 0; iteration < 2; ++iteration) {
        std::cout << "Embedded WebView2 session " << iteration + 1 << std::endl;
        WebWallpaperRenderer renderer(options);
        WallpaperFrame frame;
        // index 5 turns green when the wallpaper was told to run at 60 fps or more.
        if (!awaitFrame(renderer, frame, [](auto const& f) {
                return pixel(f, 950, 530, 18, 52, 86) && pixel(f, 450, 50, 0, 255, 255) &&
                    pixel(f, 550, 50, 0, 255, 0); })) return 1;
        if (frame.width != 960 || frame.height != 540) { std::cerr << "Wrong viewport size\n"; return 1; }
        renderer.setPointer({.75f, .5f, true, false, false, false});
        if (!awaitFrame(renderer, frame, [](auto const& f) { return pixel(f, 50, 50, 0, 255, 0); })) return 1;
        renderer.setPointer({.75f, .5f, true, true, false, false});
        if (!awaitFrame(renderer, frame, [](auto const& f) { return pixel(f, 150, 50, 0, 0, 255); })) return 1;
        renderer.setPointer({.75f, .5f, true, false, false, false});
        if (!awaitFrame(renderer, frame, [](auto const& f) { return pixel(f, 250, 50, 255, 255, 0); })) return 1;
        std::array<float, 128> spectrum;
        spectrum.fill(.5f);
        renderer.setAudio(spectrum);
        if (!awaitFrame(renderer, frame, [](auto const& f) { return pixel(f, 350, 50, 255, 0, 255); })) return 1;
        saveFrame(frame, output / "embedded-web.ppm");
        renderer.stop();
        std::cout << "PASS: full viewport, trusted movement/click/release, audio, properties and shutdown\n";
    }
    if (argc == 4) {
        options.root = std::filesystem::absolute(argv[3]);
        options.width = 1280; options.height = 720;
        WebWallpaperRenderer renderer(options);
        WallpaperFrame frame;
        if (!awaitFrame(renderer, frame, [](auto const& f) { return !f.rgba.empty(); })) return 1;
        uint64_t lastHash = 0;
        int changes = 0;
        std::cout << "Testing original WebGL wallpaper without an external player\n";
        for (int step = 0; step < 80 && changes < 20; ++step) {
            renderer.setPointer({.1f + .8f * static_cast<float>(step % 25) / 24.f,
                .1f + .8f * static_cast<float>((step * 7) % 25) / 24.f, true, step % 10 == 0, false, false});
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            if (!renderer.copyLatestFrame(frame.sequence, frame)) continue;
            uint64_t hash = 14695981039346656037ull;
            int colored = 0;
            for (size_t i = 0; i < frame.rgba.size(); ++i) {
                hash = (hash ^ frame.rgba[i]) * 1099511628211ull;
                if (i % 4 != 3 && frame.rgba[i] > 40) ++colored;
            }
            if (colored > 500 && lastHash && hash != lastHash) ++changes;
            lastHash = hash;
        }
        saveFrame(frame, output / "native-fluid.ppm");
        if (changes < 20) { std::cerr << "Original WebGL did not respond to input\n"; return 1; }
        renderer.stop();
        std::cout << "PASS: original WebGL, 20 changing colored frames at " << frame.width << 'x' << frame.height << '\n';
    }
}
