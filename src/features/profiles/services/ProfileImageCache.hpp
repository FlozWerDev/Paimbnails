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
void saveProfileImgToDisk(int accountID, std::vector<uint8_t> const& data);
