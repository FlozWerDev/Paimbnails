// AutoPreview offscreen-render pre-guards
// (OffscreenLevelRenderer::render in
// src/features/auto-preview/services/OffscreenLevelRenderer.cpp): levels
// with unknown object count (objCount == 0) and a giant level string must be
// skipped before any PlayLayer is created, as must oversize strings and
// over-cap object counts. PlayLayer creation needs the game at runtime, so
// this test enforces the isolated guard predicate with the production
// constants and boundary semantics (strict `>` comparisons).

#include <cassert>
#include <cstddef>

namespace {

constexpr std::size_t kMaxLevelStringBytes = 3 * 1024 * 1024; // hard ~3MB pre-guard
constexpr std::size_t kUnknownCountMaxBytes = 512 * 1024;    // strict cap when m_objectCount unknown

// Mirrors the pre-render checks: empty string, hard size cap, unknown-count
// cap, and object-count cap. PlayLayer-active / editor-active / Globed checks
// need the live game and are intentionally out of scope here.
bool shouldAttemptRender(long objCount, std::size_t levelSize, long cap, bool levelStringEmpty) {
    if (levelStringEmpty) return false;
    if (levelSize > kMaxLevelStringBytes) return false;
    if (objCount <= 0) {
        if (levelSize > kUnknownCountMaxBytes) return false;
    } else if (objCount > cap) {
        return false;
    }
    return true;
}

} // namespace

int main() {
    constexpr long kCap = 10000;

    // Item case: unknown object count + giant level string -> skip.
    assert(!shouldAttemptRender(0, 4 * 1024 * 1024, kCap, false));
    assert(!shouldAttemptRender(0, kUnknownCountMaxBytes + 1, kCap, false));
    assert(!shouldAttemptRender(-1, kUnknownCountMaxBytes + 1, kCap, false));

    // Unknown count with a small string may still be attempted.
    assert(shouldAttemptRender(0, 100, kCap, false));
    assert(shouldAttemptRender(0, kUnknownCountMaxBytes, kCap, false)); // boundary: `>` skips

    // Hard size cap applies regardless of a known-good object count.
    assert(!shouldAttemptRender(500, kMaxLevelStringBytes + 1, kCap, false));
    assert(shouldAttemptRender(500, kMaxLevelStringBytes, kCap, false));

    // Empty level strings never render.
    assert(!shouldAttemptRender(0, 0, kCap, true));
    assert(!shouldAttemptRender(500, 0, kCap, true));

    // Object-count cap (post-create guard uses the same `>` semantics).
    assert(!shouldAttemptRender(kCap + 1, 1024, kCap, false));
    assert(shouldAttemptRender(kCap, 1024, kCap, false));

    // Ordinary level renders.
    assert(shouldAttemptRender(1500, 200 * 1024, kCap, false));
}
