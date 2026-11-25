#pragma once

#include <algorithm>
#include <cstddef>

namespace paimon::cache {

// saturating subtraction also handles pinned resources exceeding the budget.
constexpr std::size_t remainingCacheBudget(std::size_t limit, std::size_t used) {
    return used >= limit ? 0 : limit - used;
}

constexpr std::size_t cacheByteLimit(std::size_t ownLimit, std::size_t aggregateLimit,
    std::size_t firstOther, std::size_t secondOther = 0) {
    auto remaining = remainingCacheBudget(aggregateLimit, firstOther);
    return std::min(ownLimit, remainingCacheBudget(remaining, secondOther));
}

} // namespace paimon::cache
