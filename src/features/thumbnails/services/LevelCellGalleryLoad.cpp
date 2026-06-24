#include "LevelCellGalleryLoad.hpp"
#include "LevelCellThumbHelpers.hpp"
#include "../../../managers/ThumbnailAPI.hpp"

using namespace geode::prelude;

namespace paimon::thumbnails::levelcell {

void requestGalleryList(
    int32_t levelID,
    std::shared_ptr<std::monostate> cancelToken,
    GalleryListCallback onComplete
) {
    if (!onComplete) return;

    std::weak_ptr<std::monostate> cancelWeak = cancelToken;
    ThumbnailAPI::get().getThumbnails(levelID, [cancelWeak, levelID, onComplete = std::move(onComplete)](
        bool success,
        std::vector<ThumbnailAPI::ThumbnailInfo> const& thumbs
    ) {
        if (cancelWeak.expired()) return;

        GalleryListResult result{
            .success = success,
            .thumbnails = paimon::levelcell::normalizeLevelCellGalleryThumbnails(
                levelID,
                success ? thumbs : std::vector<ThumbnailAPI::ThumbnailInfo>{}
            ),
        };
        onComplete(std::move(result));
    });
}

} // namespace paimon::thumbnails::levelcell