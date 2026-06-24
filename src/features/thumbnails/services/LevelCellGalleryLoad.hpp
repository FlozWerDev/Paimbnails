#pragma once

#include <Geode/Geode.hpp>
#include <memory>
#include <utility>
#include <vector>
#include "../../../utils/ThumbnailTypes.hpp"

namespace paimon::thumbnails::levelcell {

using GalleryListResult = ThumbnailGalleryResult;

using GalleryListCallback = geode::CopyableFunction<void(GalleryListResult)>;

// Lista de galeria del nivel; cancela si el token expiro (reciclado de celda).
void requestGalleryList(
    int32_t levelID,
    std::shared_ptr<std::monostate> cancelToken,
    GalleryListCallback onComplete
);

} // namespace paimon::thumbnails::levelcell