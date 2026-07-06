#pragma once

#include "SheetTinter.hpp"

#include <Geode/Geode.hpp>

namespace paimon::texture_studio {

class MediumPort final {
public:
    // Generate the -hd (half-resolution) version of a -uhd request.
    static geode::Result<SheetTinterOutput> generate(SheetTinterRequest const& uhdRequest);

private:
    MediumPort() = delete;
};

}  // namespace paimon::texture_studio
