#pragma once

// ProfileImageCache.hpp — Acceso publico al cache de texturas de profileimg.
// La implementacion vive en hooks/ProfilePage.cpp (donde se setea el cache
// como side-effect de cargar un perfil). Este header reemplaza las
// declaraciones cruzadas `extern CCTexture2D*` que aparecian en multiples
// archivos y reduce el acoplamiento.

#include <cocos2d.h>

// Devuelve la textura cacheada del avatar del usuario, o nullptr si no esta
// cacheada o si el sistema esta en shutdown.
cocos2d::CCTexture2D* getProfileImgCachedTexture(int accountID);

// Limpia todo el cache (RAM + disco). Llamado por la accion de mantenimiento
// "Limpiar caches".
void clearProfileImgCache();
