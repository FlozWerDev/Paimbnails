// tests de imagen e infraestructura de packgen v2: frameimage, maxrects,
// contenthash, packcache, packgraph y packscheduler.
// compila sin geode (nucleo puro):
//   g++ -std=c++17 -o2 -pthread -o /tmp/pgimg tests/packgen_image_regression.cpp && /tmp/pgimg
// cada prueba es una funcion bool. main() las ejecuta todas y devuelve 0 si
// pasan o 1 si alguna falla.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "../src/features/texture-studio/packgen/ContentHash.hpp"
#include "../src/features/texture-studio/packgen/FrameImage.hpp"
#include "../src/features/texture-studio/packgen/MaxRectsPacker.hpp"
#include "../src/features/texture-studio/packgen/PackCache.hpp"
#include "../src/features/texture-studio/packgen/PackGraph.hpp"
#include "../src/features/texture-studio/packgen/PackScheduler.hpp"

using namespace paimon::texture_studio::packgen;

namespace {

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cout << "FAIL " << __func__ << ":" << __LINE__ << ": " #cond "\n"; \
        return false; \
    } \
} while (0)

// --- frameimage ---

bool frame_subrect_roundtrip() {
    FrameImage img(6, 6);
    for (int y = 0; y < 6; ++y)
        for (int x = 0; x < 6; ++x)
            img.setAt(x, y, {static_cast<std::uint8_t>(x * 10),
                             static_cast<std::uint8_t>(y * 10),
                             static_cast<std::uint8_t>(x + y), 255});
    FrameImage sub = img.subRect(2, 1, 3, 4);
    CHECK(sub.width() == 3 && sub.height() == 4);
    CHECK(sub.at(0, 0).r == 20 && sub.at(0, 0).g == 10);
    CHECK(sub.at(2, 3).r == 40 && sub.at(2, 3).g == 40);

    // fuera de limites -> transparente, sin crash.
    FrameImage oob = img.subRect(-2, -2, 4, 4);
    CHECK(oob.width() == 4 && oob.height() == 4);
    CHECK(oob.at(0, 0).a == 0);
    CHECK(oob.at(2, 2).r == 0);  // src (0,0)
    return true;
}

bool frame_blit_overwrite() {
    FrameImage dst(8, 8), tile(3, 3);
    dst.clear({1, 2, 3, 255});
    tile.clear({9, 8, 7, 255});
    dst.blitOverwrite(2, 5, tile);
    CHECK(dst.at(2, 5).r == 9 && dst.at(4, 7).b == 7);
    CHECK(dst.at(1, 5).r == 1 && dst.at(5, 5).r == 1);

    // blit recortado por los bordes no escribe fuera ni falla.
    dst.blitOverwrite(-1, -1, tile);
    CHECK(dst.at(0, 0).r == 9);
    dst.blitOverwrite(7, 7, tile);
    CHECK(dst.at(7, 7).r == 9);
    CHECK(dst.at(6, 7).r == 1);
    return true;
}

bool frame_rotate_roundtrip() {
    FrameImage img(4, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            img.setAt(x, y, {static_cast<std::uint8_t>(y * 4 + x), 0, 0, 255});
    img.rotateCW90();
    CHECK(img.width() == 2 && img.height() == 4);
    // (0,0) original -> (1,0) tras cw90 en un 4x2.
    CHECK(img.at(1, 0).r == 0);
    CHECK(img.at(0, 3).r == 7 || img.at(1, 3).r == 7);
    for (int k = 0; k < 3; ++k) img.rotateCW90();
    CHECK(img.width() == 4 && img.height() == 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(img.at(x, y).r == static_cast<std::uint8_t>(y * 4 + x));
    return true;
}

bool frame_box_half() {
    FrameImage img(4, 4);
    img.clear({100, 150, 200, 255});
    FrameImage half = img.boxHalf();
    CHECK(half.width() == 2 && half.height() == 2);
    CHECK(half.at(0, 0).r == 100 && half.at(1, 1).b == 200);

    // promedio real de bloque 2x2 mixto.
    img.setAt(0, 0, {0, 0, 0, 255});
    img.setAt(1, 0, {100, 0, 0, 255});
    img.setAt(0, 1, {200, 0, 0, 255});
    img.setAt(1, 1, {100, 0, 0, 255});
    half = img.boxHalf();
    CHECK(half.at(0, 0).r == 100);  // (0+100+200+100)/4
    return true;
}

bool frame_ink_and_equality() {
    FrameImage a(4, 4), b(4, 4);
    CHECK(!a.hasInk());
    a.setAt(2, 2, {1, 2, 3, 1});
    CHECK(a.hasInk());
    CHECK(a != b);
    b.setAt(2, 2, {1, 2, 3, 1});
    CHECK(a == b);
    return true;
}

// --- maxrectspacker ---

namespace packer {

bool noOverlapContained(const MaxRectsPacker::Result& r) {
    auto norm = [](const PackPlacement& p) {
        int w = p.rotated ? p.h : p.w;
        int h = p.rotated ? p.w : p.h;
        return std::tuple<int, int, int, int>(p.x, p.y, w, h);
    };
    for (std::size_t i = 0; i < r.placements.size(); ++i) {
        auto [x0, y0, w0, h0] = norm(r.placements[i]);
        if (x0 < 0 || y0 < 0 || x0 + w0 > r.atlasW || y0 + h0 > r.atlasH)
            return false;
        for (std::size_t j = i + 1; j < r.placements.size(); ++j) {
            auto [x1, y1, w1, h1] = norm(r.placements[j]);
            if (x0 < x1 + w1 && x1 < x0 + w0 && y0 < y1 + h1 && y1 < y0 + h0)
                return false;
        }
    }
    return true;
}

}  // namespace packer

bool packer_invariants() {
    std::vector<PackRect> rects = {
        {64, 64, 0}, {32, 96, 1}, {96, 32, 2}, {16, 16, 3}, {48, 48, 4},
        {128, 8, 5}, {8, 128, 6}, {200, 200, 7},
    };
    auto r = MaxRectsPacker().pack(rects);
    CHECK(r.fits);
    CHECK(r.placements.size() == rects.size());
    CHECK(packer::noOverlapContained(r));

    // vacio: atlas 0x0 que encaja.
    auto empty = MaxRectsPacker().pack({});
    CHECK(empty.fits && empty.atlasW == 0 && empty.atlasH == 0);
    return true;
}

bool packer_deterministic() {
    std::vector<PackRect> rects;
    for (int i = 0; i < 40; ++i)
        rects.push_back({8 + (i * 37) % 120, 8 + (i * 53) % 120, i});
    auto a = MaxRectsPacker().pack(rects);
    auto b = MaxRectsPacker().pack(rects);
    CHECK(a.fits && b.fits);
    CHECK(a.atlasW == b.atlasW && a.atlasH == b.atlasH);
    CHECK(a.placements.size() == b.placements.size());
    for (std::size_t i = 0; i < a.placements.size(); ++i) {
        CHECK(a.placements[i].x == b.placements[i].x);
        CHECK(a.placements[i].y == b.placements[i].y);
        CHECK(a.placements[i].rotated == b.placements[i].rotated);
    }
    return true;
}

bool packer_utilization_and_overflow() {
    // set homogeneo: maxrects debe aprovechar bien el atlas.
    std::vector<PackRect> rects;
    for (int i = 0; i < 16; ++i) rects.push_back({64, 64, i});
    MaxRectsPacker::Options opts;
    opts.padding = 0;
    opts.allowRotate = false;
    auto r = MaxRectsPacker(opts).pack(rects);
    CHECK(r.fits);
    CHECK(packer::noOverlapContained(r));
    double used = 16.0 * 64 * 64;
    double atlas = static_cast<double>(r.atlasW) * r.atlasH;
    CHECK(atlas > 0 && used / atlas >= 0.5);

    // un rect mayor que maxsize no cabe nunca.
    MaxRectsPacker::Options tiny;
    tiny.maxSize = 64;
    auto bad = MaxRectsPacker(tiny).pack({{128, 128, 0}});
    CHECK(!bad.fits);
    return true;
}

// --- contenthash ---

bool hash_stable_and_sensitive() {
    TintParams p, q;
    p.c1r = 255; q.c1r = 255;
    CHECK(p.fingerprint() == q.fingerprint());
    q.brightness = 161;
    CHECK(p.fingerprint() != q.fingerprint());
    q = p;
    q.saturation = 1.0f + 1.0f / 4096.0f;  // cuarto de paso de 1/1024: mismo cuanto
    CHECK(p.fingerprint() == q.fingerprint());
    q.saturation = 1.5f;
    CHECK(p.fingerprint() != q.fingerprint());

    // nan no envenena la clave.
    CHECK(normalizeFloat(std::numeric_limits<float>::quiet_NaN()) == 0);
    float inf = std::numeric_limits<float>::infinity();
    CHECK(normalizeFloat(inf) == normalizeFloat(inf));

    std::uint8_t bytes[4] = {1, 2, 3, 4};
    CHECK(hashBytes(bytes, 4) == hashBytes(bytes, 4));
    std::uint8_t other[4] = {1, 2, 3, 5};
    CHECK(hashBytes(bytes, 4) != hashBytes(other, 4));
    return true;
}

// --- packcache ---

bool cache_hit_miss_and_versioning() {
    PackCache cache(1u << 20);
    NodeKey k1{12345, 1}, k2{12345, 2};
    CHECK(cache.lookup(k1) == nullptr);  // miss inicial
    cache.store(k1, PackCache::Bytes{7, 8, 9});
    auto hit = cache.lookup(k1);
    CHECK(hit && hit->size() == 3 && (*hit)[0] == 7);
    CHECK(cache.stats().hits == 1 && cache.stats().misses == 1);

    // otra version = otra entrada (invalida lo viejo al bump).
    CHECK(cache.lookup(k2) == nullptr);
    cache.store(k2, PackCache::Bytes{1});
    CHECK(cache.lookup(k2)->size() == 1);
    CHECK(cache.lookup(k1)->size() == 3);

    CHECK(cache.erase(k1));
    CHECK(cache.lookup(k1) == nullptr);
    CHECK(!cache.erase(k1));
    return true;
}

bool cache_lru_eviction() {
    PackCache cache(64);  // presupuesto minusculo para forzar eviccion
    for (int i = 0; i < 16; ++i) {
        NodeKey k{static_cast<std::uint64_t>(i), 1};
        cache.store(k, PackCache::Bytes(16, static_cast<std::uint8_t>(i)));
    }
    CHECK(cache.bytesHeld() <= 64);
    CHECK(cache.stats().evictions > 0);
    CHECK(cache.entries() * 16 == cache.bytesHeld());
    return true;
}

bool cache_disk_tier() {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / "pgimg-cache-test";
    fs::remove_all(dir);
    {
        PackCache cache(1u << 20);
        cache.setDiskDir(dir);
        NodeKey k{0xDEADBEEF, 3};
        cache.store(k, PackCache::Bytes{4, 5, 6, 7});
        CHECK(cache.lookup(k) != nullptr);
    }
    // nueva instancia: el disco rescata la entrada (miss de memoria).
    {
        PackCache cache(1u << 20);
        cache.setDiskDir(dir);
        NodeKey k{0xDEADBEEF, 3};
        auto hit = cache.lookup(k);
        CHECK(hit && hit->size() == 4 && (*hit)[3] == 7);
        CHECK(cache.stats().diskHits == 1);
        // version distinta no se rescata del disco.
        NodeKey stale{0xDEADBEEF, 2};
        CHECK(cache.lookup(stale) == nullptr);
    }
    fs::remove_all(dir);
    return true;
}

// --- packgraph ---

bool graph_pruning_and_topo() {
    PackCache cache(1u << 20);
    PackGraph g;
    g.setCache(&cache);
    std::vector<int> order;
    int leafRuns = 0, midRuns = 0;
    auto leaf = g.addNode("leaf", [&] {
        ++leafRuns;
        order.push_back(0);
        return PackCache::Bytes{1};
    });
    auto mid = g.addNode(
        "mid",
        [&] {
            ++midRuns;
            order.push_back(1);
            auto v = *g.result(leaf);
            v.push_back(2);
            return v;
        },
        {leaf});
    auto root = g.addNode(
        "root",
        [&] {
            order.push_back(2);
            auto v = *g.result(mid);
            v.push_back(3);
            return v;
        },
        {mid});

    PackGraph::EvalStats s1;
    CHECK(g.evaluate(&s1));
    CHECK(s1.computed == 3 && leafRuns == 1 && midRuns == 1);
    CHECK(order == std::vector<int>({0, 1, 2}));  // deps primero

    PackGraph::EvalStats s2;
    CHECK(g.evaluate(&s2));
    CHECK(s2.computed == 0);  // todo podado
    auto res = g.result(root);
    CHECK(res && res->size() == 3 && (*res)[2] == 3);

    // cambiar una entrada invalida solo la rama afectada.
    order.clear();
    g.setInputs(leaf, {999});
    g.markDirty(leaf);
    PackGraph::EvalStats s3;
    CHECK(g.evaluate(&s3));
    CHECK(s3.computed == 3 && leafRuns == 2 && midRuns == 2);
    CHECK(order == std::vector<int>({0, 1, 2}));
    return true;
}

bool graph_cycle_rejected() {
    // ciclo real x->y->x: las deps son ids, asi que declarar x con dep
    // futura {1} y luego y con dep {0} cierra el ciclo.
    PackGraph cyc;
    auto x = cyc.addNode("x", [] { return PackCache::Bytes{1}; }, {1});
    auto y = cyc.addNode("y", [] { return PackCache::Bytes{2}; }, {x});
    (void)y;
    CHECK(!cyc.evaluate());  // ciclo detectado

    // dependencia fuera de rango tambien se rechaza.
    PackGraph bad;
    bad.addNode("lonely", [] { return PackCache::Bytes{1}; }, {7});
    CHECK(!bad.evaluate());

    // un dag sano con el mismo contenido evalua bien.
    PackGraph g;
    auto a = g.addNode("a", [] { return PackCache::Bytes{1}; });
    auto b = g.addNode("b", [] { return PackCache::Bytes{2}; }, {a});
    auto c = g.addNode("c", [] { return PackCache::Bytes{3}; }, {b, a});
    CHECK(g.evaluate());
    CHECK(g.node(c).deps.size() == 2);
    return true;
}

// --- packscheduler ---

bool scheduler_parallel_for() {
    PackScheduler pool(4);
    constexpr int N = 10000;
    std::vector<int> v(N, 0);
    pool.parallelFor(0, N, [&](int i) { v[i] = i * 2; });
    for (int i = 0; i < N; ++i) CHECK(v[i] == i * 2);

    // rango vacio: no hace nada, no falla.
    pool.parallelFor(5, 5, [&](int) { CHECK(false); });

    std::atomic<int> sum{0};
    pool.parallelFor(0, 1000, [&](int i) { sum.fetch_add(i); }, 64);
    CHECK(sum.load() == 999 * 1000 / 2);
    return true;
}

bool scheduler_exceptions_propagate() {
    PackScheduler pool(4);
    bool caught = false;
    try {
        pool.parallelFor(0, 100, [](int i) {
            if (i == 42) throw std::runtime_error("boom-42");
        });
    } catch (std::runtime_error& e) {
        caught = std::string(e.what()) == "boom-42";
    }
    CHECK(caught);

    pool.submit([] { throw std::logic_error("async-boom"); });
    bool caught2 = false;
    try {
        pool.waitAll();
    } catch (std::logic_error&) {
        caught2 = true;
    }
    CHECK(caught2);
    // la piscina sigue usable tras una excepcion.
    std::atomic<int> n{0};
    pool.parallelFor(0, 100, [&](int) { n.fetch_add(1); });
    CHECK(n.load() == 100);
    return true;
}

}  // namespace

int main() {
    int passed = 0, total = 0;
#define RUN(fn) do { ++total; std::cout << (#fn) << "... "; \
    if (fn()) { ++passed; std::cout << "ok\n"; } else { std::cout << "FALLO\n"; } } while (0)

    RUN(frame_subrect_roundtrip);
    RUN(frame_blit_overwrite);
    RUN(frame_rotate_roundtrip);
    RUN(frame_box_half);
    RUN(frame_ink_and_equality);
    RUN(packer_invariants);
    RUN(packer_deterministic);
    RUN(packer_utilization_and_overflow);
    RUN(hash_stable_and_sensitive);
    RUN(cache_hit_miss_and_versioning);
    RUN(cache_lru_eviction);
    RUN(cache_disk_tier);
    RUN(graph_pruning_and_topo);
    RUN(graph_cycle_rejected);
    RUN(scheduler_parallel_for);
    RUN(scheduler_exceptions_propagate);

    std::cout << passed << "/" << total << " tests de imagen OK\n";
    return (passed == total) ? 0 : 1;
}
