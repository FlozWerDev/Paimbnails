#pragma once

#include "../GifImportTypes.hpp"

#include <cstdint>
#include <vector>

namespace paimon::gifimport {

// stamp indices refer to the library while tracing and are remapped when the plan is collected.
std::vector<Primitive> vectorizeFree(
    std::vector<int> const& positions,
    int width,
    int height,
    int color,
    int rank,
    std::vector<std::uint8_t> const& blocked = {},
    std::vector<std::uint8_t> const& empty = {},
    bool gridExact = true
);

} // namespace paimon::gifimport
