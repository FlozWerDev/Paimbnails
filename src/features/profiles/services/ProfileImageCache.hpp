#pragma once


#include <cocos2d.h>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

cocos2d::CCTexture2D* getProfileImgCachedTexture(int accountID);

void clearProfileImgCache();
void shutdownProfileImgCache();

void invalidateProfileImgCache(int accountID);

void cacheProfileImgTexture(int accountID, cocos2d::CCTexture2D* texture);

std::filesystem::path getProfileImgCachePath(int accountID);
std::filesystem::path getProfileImgCacheDir();
std::mutex& profileImgDiskMutex();
void invalidateProfileImgDiskCache(int accountID);
std::string getProfileImgGifCacheKey(int accountID);
cocos2d::CCTexture2D* loadProfileImgFromDisk(int accountID);
// null for gif/mp4 payloads, which the animated path owns; skips a second stat + read.
cocos2d::CCTexture2D* decodeProfileImgBytes(uint8_t const* data, size_t size);
void saveProfileImgToDisk(int accountID, std::vector<uint8_t> const& data);
