#include "../src/features/thumbnails/services/CacheMemoryBudget.hpp"

#include <cassert>
#include <limits>

int main() {
    using paimon::cache::cacheByteLimit;
    constexpr std::size_t MiB = 1024 * 1024;
    assert(cacheByteLimit(200 * MiB, 150 * MiB, 0) == 150 * MiB);
    assert(cacheByteLimit(100 * MiB, 80 * MiB, 0) == 80 * MiB);
    assert(cacheByteLimit(200 * MiB, 150 * MiB, 90 * MiB, 20 * MiB) == 40 * MiB);
    assert(cacheByteLimit(32 * MiB, 150 * MiB, 90 * MiB, 50 * MiB) == 10 * MiB);
    assert(cacheByteLimit(32 * MiB, 150 * MiB, 150 * MiB, 1) == 0);
    assert(cacheByteLimit(32 * MiB, 150 * MiB, std::numeric_limits<std::size_t>::max(), 1) == 0);
    assert(cacheByteLimit(0, 150 * MiB, 0) == 0);
}
