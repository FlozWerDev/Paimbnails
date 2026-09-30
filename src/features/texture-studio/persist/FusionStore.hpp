#pragma once

#include "../data/ImageTransform.hpp"
#include "../engine/FusionEngine.hpp"
#include "../engine/MaskBuilder.hpp"

#include <Geode/Geode.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace paimon::texture_studio {

// on-disk fusion payload for one sprite: r8 region mask + blend metadata.
// the source texture is stored next to it as .png or .gif.
struct FusionPayload {
    MaskBuffer mask;
    FusionBlendMode blendMode = FusionBlendMode::MultiplyLuma;
    int colorTolerance = 28;
    float opacity = 1.0f;
    ImageTransform transform{};
    bool animated = false;
    std::string textureExt = ".png";  // ".png" or ".gif"
};

class FusionStore final {
public:
    static constexpr std::uint32_t kMagic   = 0x53554650u;  // "pfus"
    static constexpr std::uint16_t kVersion = 1;

    static geode::Result<> save(std::filesystem::path const& path,
                                FusionPayload const& payload);

    static geode::Result<FusionPayload> load(std::filesystem::path const& path);

    static geode::Result<> saveForSlot(std::string_view slotId,
                                       std::string_view spriteName,
                                       FusionPayload const& payload);

    static geode::Result<FusionPayload> loadForSlot(std::string_view slotId,
                                                    std::string_view spriteName);

    // drops mask/metadata only; picked texture stays for repainting without re-import.
    static geode::Result<> deleteMaskForSlot(std::string_view slotId,
                                             std::string_view spriteName);

    // idempotent: ok even if nothing existed.
    static geode::Result<> deleteForSlot(std::string_view slotId,
                                         std::string_view spriteName);

    // copy a picked texture in: gif bytes preserved, statics re-encoded as png.
    static geode::Result<std::filesystem::path> importTexture(
        std::string_view slotId,
        std::string_view spriteName,
        std::filesystem::path const& sourcePath);

private:
    FusionStore() = delete;
};

}  // namespace paimon::texture_studio
