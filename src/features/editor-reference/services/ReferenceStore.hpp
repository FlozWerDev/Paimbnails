#pragma once

#include "../ReferenceTypes.hpp"

#include <filesystem>
#include <string>

class GJGameLevel;

namespace paimon::editorref::store {

std::filesystem::path assetsDir();
std::filesystem::path resolveAssetPath(std::string const& assetFile);

void restoreLevelIdentity(GJGameLevel* level, std::string const& identity);
std::string levelKey(GJGameLevel* level);
void copyLevelDocument(GJGameLevel* source, GJGameLevel* destination);

ReferenceDocument loadLevelDocument(GJGameLevel* level);
ReferenceDocument loadDocument(std::string const& key);
bool saveDocument(std::string const& key, ReferenceDocument const& doc);

bool importImageFile(
    std::filesystem::path const& srcPath,
    std::string& outAssetFile,
    std::string& outName,
    int& outWidth,
    int& outHeight,
    std::string& outError
);

bool importClipboardImage(
    std::string& outAssetFile,
    std::string& outName,
    int& outWidth,
    int& outHeight,
    std::string& outError
);

std::string cloneAssetFile(std::string const& assetFile);
void deleteAssetIfUnused(std::string const& assetFile, ReferenceDocument const& doc);

// Global image history across all levels.
std::filesystem::path historyAssetsDir();
std::filesystem::path resolveHistoryAssetPath(std::string const& assetFile);
std::vector<HistoryEntry> loadHistory();
void recordHistoryFromAsset(
    std::string const& levelAssetFile,
    std::string const& name,
    int width,
    int height
);
bool importFromHistory(
    std::string const& historyId,
    std::string& outAssetFile,
    std::string& outName,
    int& outWidth,
    int& outHeight,
    std::string& outError
);
bool removeHistoryEntry(std::string const& historyId);
void clearHistory();

} // namespace paimon::editorref::store
