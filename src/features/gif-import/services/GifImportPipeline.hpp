#pragma once

#include "../GifImportTypes.hpp"

#include <functional>

namespace paimon::gifimport {

using BuildProgressCallback = std::function<void(BuildProgress const&)>;

BuildResult buildPlan(
    SourceAnimation const& source,
    Options const& options,
    BuildProgressCallback progress = {}
);

} // namespace paimon::gifimport
