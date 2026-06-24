#include "MediumPort.hpp"

#include "SheetTinter.hpp"

namespace paimon::texture_studio {

geode::Result<SheetTinterOutput> MediumPort::generate(
    SheetTinterRequest const& uhdRequest) {

    SheetTinterRequest hdReq = uhdRequest;
    hdReq.outputQualitySuffix = "-hd";
    hdReq.resizeScale = 0.5f;
    // The PackGen special-case for GJ_table_side_001 stays enabled.
    hdReq.preserveOffsetForTableSide = true;
    return SheetTinter::process(hdReq);
}

}  // namespace paimon::texture_studio
