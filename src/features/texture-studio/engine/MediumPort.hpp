#pragma once
//
// MediumPort.hpp - Generates a half-resolution (-hd) variant of a UHD sheet.
//
// PackGen offers an "Include Medium Port" checkbox that, when active, also
// emits sheets at -hd quality (50% of -uhd dimensions). Some users prefer
// this for performance; it's also what GD itself does internally for older
// devices.
//
// Implementation is a thin wrapper around SheetTinter — we just override
// `resizeScale` to 0.5 and the `outputQualitySuffix` to "-hd". Living in a
// dedicated file keeps the orchestration layer (PackExporter) readable.
//

#include "SheetTinter.hpp"

#include <Geode/Geode.hpp>

namespace paimon::texture_studio {

class MediumPort final {
public:
    // Generate the -hd version corresponding to a -uhd request. The input
    // `uhdRequest` should already be valid for a -uhd output; we copy and
    // adjust two fields.
    //
    // Returns the same SheetTinterOutput shape as SheetTinter::process —
    // PNG bytes + plist XML — but at half the linear resolution.
    static geode::Result<SheetTinterOutput> generate(SheetTinterRequest const& uhdRequest);

private:
    MediumPort() = delete;
};

}  // namespace paimon::texture_studio
