#pragma once

#include "../GifImportTypes.hpp"

namespace paimon::gifimport {

BuildResult buildPlan(SourceAnimation const& source, Options const& options);

} // namespace paimon::gifimport
