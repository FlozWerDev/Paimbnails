#pragma once

// ProfileImageCache.hpp — Acceso publico al cache de texturas de profileimg.
// La implementacion vive en hooks/ProfilePage.cpp (donde se setea el cache
// como side-effect de cargar un perfil). Este header reemplaza las
// declaraciones cruzadas `extern CCTexture2D*` que aparecian en multiples
// archivos y reduce el acoplamiento.

#include <cocos2d.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// Devuelve la textura cacheada del avatar del usuario, o nullptr si no esta
// cacheada o si el sistema esta en shutdown.
cocos2d::CCTexture2D* getProfileImgCachedTexture(int accountID);

// Limpia todo el cache (RAM + disco). Llamado por la accion de mantenimiento
// "Limpiar caches".
void clearProfileImgCache();

// Invalida el cache RAM de un accountID especifico (usado despues de uploads)
void invalidateProfileImgCache(int accountID);

// Cache RAM (LRU acotado por entradas y por bytes) de la textura del avatar.
void cacheProfileImgTexture(int accountID, cocos2d::CCTexture2D* texture);

// Cache en disco (carpeta profileimg_cache/).
std::filesystem::path getProfileImgCachePath(int accountID);
std::string getProfileImgGifCacheKey(int accountID);
cocos2d::CCTexture2D* loadProfileImgFromDisk(int accountID);
void saveProfileImgToDisk(int accountID, std::vector<uint8_t> const& data);
