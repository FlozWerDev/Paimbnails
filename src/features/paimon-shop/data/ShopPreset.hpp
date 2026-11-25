#pragma once

#include <matjson.hpp>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace paimon::shop {

inline constexpr size_t kMaxPreviewBytes = 1024 * 1024;
inline constexpr size_t kMaxPresetBytes = 2 * 1024 * 1024;
inline constexpr size_t kMaxLibraryEntries = 500;

enum class Category { Layout, RTX, BeatShaders, Pack };
char const* categoryId(Category category);
char const* categoryName(Category category);
geode::Result<Category> parseCategory(std::string_view id);

struct Preset {
    std::string title;
    std::string description;
    std::string author;
    int accountID = 0;
    int64_t createdAt = 0;
    Category category = Category::Layout;
    matjson::Value configuration;
    std::string preview; // Portable, embedded PNG; no local paths or remote URLs.
};

struct Listing {
    std::string id;
    std::string title;
    std::string author;
    Category category = Category::Layout;
    int64_t createdAt = 0;
};

geode::Result<> validateConfiguration(Category category, matjson::Value const& configuration);
geode::Result<> validatePreview(std::vector<uint8_t> const& png);
geode::Result<> validatePreset(Preset const& preset);
geode::Result<matjson::Value> parseBoundedJson(std::string_view content);
matjson::Value presetToJson(Preset const& preset);
geode::Result<Preset> presetFromJson(matjson::Value const& value);

} // namespace paimon::shop
