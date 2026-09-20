#pragma once

#include "../GifImportTypes.hpp"

#include <functional>

namespace paimon::gifimport {

using BuildProgressCallback = std::function<void(BuildProgress const&)>;
using BuildPreviewCallback = std::function<void(PreviewImage)>;

BuildResult buildPlan(
    SourceAnimation const& source,
    Options const& options,
    BuildProgressCallback progress = {},
    BuildPreviewCallback preview = {}
);

} // namespace paimon::gifimport
