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

// On-disk fusion payload for one sprite: R8 region mask + blend metadata.
// The source texture is stored next to it as .png or .gif.
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
    static constexpr std::uint32_t kMagic   = 0x53554650u;  // "PFUS"
    static constexpr std::uint16_t kVersion = 1;

    static geode::Result<> save(std::filesystem::path const& path,
                                FusionPayload const& payload);

    static geode::Result<FusionPayload> load(std::filesystem::path const& path);

    static geode::Result<> saveForSlot(std::string_view slotId,
                                       std::string_view spriteName,
                                       FusionPayload const& payload);

    static geode::Result<FusionPayload> loadForSlot(std::string_view slotId,
                                                    std::string_view spriteName);

    // Remove only the painted mask/metadata, preserving the picked texture so
    // the user can paint a different region without importing it again.
    static geode::Result<> deleteMaskForSlot(std::string_view slotId,
                                             std::string_view spriteName);

    // Idempotent: Ok even if nothing existed.
    static geode::Result<> deleteForSlot(std::string_view slotId,
                                         std::string_view spriteName);

    // Copy a user-picked texture into the slot fusion folder. Preserves GIF
    // bytes so animation survives; static images are re-encoded as PNG.
    static geode::Result<std::filesystem::path> importTexture(
        std::string_view slotId,
        std::string_view spriteName,
        std::filesystem::path const& sourcePath);

private:
    FusionStore() = delete;
};

}  // namespace paimon::texture_studio
