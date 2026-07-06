#pragma once

#include "PackExporterTypes.hpp"

#include <Geode/Geode.hpp>

#include <string>
#include <string_view>

namespace paimon::texture_studio {

class PackMetadataBuilder final {
public:
    static std::string buildPackJson(std::string_view packName,
                                     std::string_view author);

    static std::string buildUiColorsJson(PackExportConfig const& cfg);

    static std::string buildModsLayerJson(PackExportConfig const& cfg);

    static std::string buildLoadingLayerJson(PackExportConfig const& cfg);

    static std::string buildPackId(std::string_view packName);

private:
    PackMetadataBuilder() = delete;
};

}  // namespace paimon::texture_studio
