#pragma once


#include <cocos2d.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

cocos2d::CCTexture2D* getProfileImgCachedTexture(int accountID);

void clearProfileImgCache();

void invalidateProfileImgCache(int accountID);

void cacheProfileImgTexture(int accountID, cocos2d::CCTexture2D* texture);

std::filesystem::path getProfileImgCachePath(int accountID);
std::string getProfileImgGifCacheKey(int accountID);
cocos2d::CCTexture2D* loadProfileImgFromDisk(int accountID);
// Null for GIF/MP4 payloads, which the animated path owns; skips a second stat + read.
cocos2d::CCTexture2D* decodeProfileImgBytes(uint8_t const* data, size_t size);
void saveProfileImgToDisk(int accountID, std::vector<uint8_t> const& data);
