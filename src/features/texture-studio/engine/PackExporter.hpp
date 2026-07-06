#pragma once

#include "PackExporterTypes.hpp"

#include <Geode/Geode.hpp>

#include <functional>

namespace paimon::texture_studio {

class PackExporter final {
public:
    using ProgressCallback = std::function<void(int /*idx*/, int /*total*/, std::string const& /*name*/)>;

    static geode::Result<PackExportResult> exportPack(
        PackExportConfig const& cfg,
        std::filesystem::path const& outputZipPath,
        ProgressCallback progress = {});

private:
    PackExporter() = delete;
};

}  // namespace paimon::texture_studio
