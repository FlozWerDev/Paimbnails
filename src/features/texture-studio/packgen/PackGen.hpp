#pragma once
// PackGen v2: modern texture-pack generation core for Texture Studio.
//
// Layout:
//   packgen/               pure C++17/23 core, zero Geode/cocos includes.
//     ContentHash.hpp      FNV-1a64 content hashing + normalized param keys.
//     FrameImage.hpp       row-major RGBA8 value image (tests + pipeline).
//     TintEngine.hpp       bit-exact PackGen luminance tint kernel, hoisted
//                          invariants, alpha LUT, fused multi-role row loops.
//     PackCache.hpp        byte-budgeted LRU blob cache (memory + disk).
//     PackGraph.hpp        reactive DAG: dirty-set evaluate with hash pruning.
//     PackScheduler.hpp    tiny thread pool + parallelFor (local pools only,
//                          so Geode unload never strands worker threads).
//     MaxRectsPacker.hpp    Best-Short-Side-Fit rect packer, deterministic.
//     SelfCheck.hpp        headless runtime self-check (also run by tests).
//
// The legacy engine in ../engine (SheetTinter, LuminanceTinter,
// OverlayTinter, ColorClustering, ...) delegates its hot loops to this core.
// Math is replicated op-for-op so outputs stay bit-identical; speed comes
// from hoisted invariants, single-pass accumulation, row-band parallelism
// and frame-level parallelism — never from changing the formulas.
//
// Version every behavioral change below; PackGraph::NodeKey carries the
// version so stale disk caches invalidate automatically.
namespace paimon::texture_studio::packgen {

inline constexpr int kTintPipelineVersion = 2;
inline constexpr int kPackerVersion = 2;
inline constexpr int kCacheSchemaVersion = 1;

}  // namespace paimon::texture_studio::packgen
