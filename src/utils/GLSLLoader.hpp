#pragma once
// GLSLLoader — carga programas GLSL desde archivos en disco con fallback a
// literales inline. Objetivo: mover los literales `constexpr auto ...` de
// Shaders.hpp a archivos `.glsl` dedicados (mejor mantenibilidad, hot-reload
// en debug, binario mas chico tras Fase 5).
//
// Diseño:
//   - Durante la migracion (Fases 1-4) cada shader tiene `vertexFile`/
//     `fragmentFile` + `vertexFallback`/`fragmentFallback`. Si el archivo
//     existe en disco se usa; si falla I/O se compila el fallback.
//     Esto mantiene el mod funcional aunque la instalacion sea parcial.
//   - Tras Fase 5 los fallbacks se eliminan y `loadShader` fail-fast si
//     falta el archivo (log::error + nullptr).
//
// Thread-safety: todas las funciones de carga GPU DEBEN correr en el main
// thread (contexto GL). La lectura de archivos (`readShaderFile`) es
// reentrante y cachea el contenido para evitar re-I/O en re-compilaciones.

#include <Geode/cocos/shaders/CCGLProgram.h>
#include <string>
#include <string_view>

namespace paimon::shaders {

/// Carga (o devuelve de CCShaderCache) un CCGLProgram compilado desde los
/// archivos indicados.
///
/// Tras Fase 5 el comportamiento es fail-fast: si `vertexFile`/`fragmentFile`
/// no se pueden leer y el fallback es `nullptr`, la funcion hace log::error
/// y devuelve `nullptr`. Los callers DEBEN chequear el retorno — un nullptr
/// significa instalacion rota (mod sin resources/shaders/*.glsl empaquetado)
/// y el blur no funcionara.
///
/// Los parametros `vertexFallback`/`fragmentFallback` quedan para casos de
/// emergencia (por ejemplo un shader nuevo que aun no tiene .glsl propio),
/// no son el camino principal.
///
/// @param cacheKey       clave estable en CCShaderCache.
/// @param vertexFile     nombre relativo a resources/shaders/ (ej.
///                       "cell_vertex.glsl"). Vacio → usa fallback directo.
/// @param fragmentFile   idem para el fragment shader.
/// @param vertexFallback fuente inline, o nullptr para fail-fast.
/// @param fragmentFallback idem.
///
/// @return El CCGLProgram compilado/cacheado, o nullptr si falla.
cocos2d::CCGLProgram* loadShader(
    std::string_view cacheKey,
    std::string_view vertexFile,
    std::string_view fragmentFile,
    char const* vertexFallback,
    char const* fragmentFallback
);

/// Lee el contenido de `resources/shaders/<relName>` como UTF-8 / ASCII.
/// El resultado se cachea en RAM (unos pocos KB en total para todo el set
/// de shaders del mod). Devuelve string vacio si el archivo no existe o
/// no se puede leer.
///
/// Segura de llamar desde cualquier thread.
std::string readShaderFile(std::string_view relName);

/// Preload de shaders de blur en el GL thread. Compila las variantes `.glsl`
/// que el mod conoce, de modo que el primer uso no sufra el stutter del
/// compile. Idempotente (CCShaderCache hit despues de la primera llamada).
///
/// Fase 0: carga un shader de validacion (`kawase_realtime.glsl`) bajo una
/// clave dedicada (`paimon-blur-rt-preload`_spr) para verificar que el
/// pipeline de disco funciona en la maquina del usuario. No reemplaza
/// ningun shader en uso — ver Fases 1+ para las migraciones reales.
///
/// DEBE llamarse desde el main thread (contexto GL requerido).
void preloadBlurShaders();

/// Limpia el cache RAM de fuentes GLSL leidas. Util para hot-reload en
/// debug (combinado con `CCShaderCache::reloadDefaultShaders()`). No toca
/// CCShaderCache — eso es responsabilidad del caller.
void clearShaderFileCache();

// ─────────────────────────────────────────────────────────────────────
// Helpers tipados para los shaders de blur del mod. Cada uno encapsula la
// clave de cache, los archivos .glsl y los literales fallback. Los callers
// (`Shaders.cpp`, hooks que usan blur directamente) deben preferir estas
// funciones frente a `getOrCreateShader` — la duplicacion de literales
// desaparecera en Fase 5.
// ─────────────────────────────────────────────────────────────────────

cocos2d::CCGLProgram* getBlurHorizontalShader();
cocos2d::CCGLProgram* getBlurVerticalShader();
cocos2d::CCGLProgram* getKawaseDownShader();
cocos2d::CCGLProgram* getKawaseUpShader();
cocos2d::CCGLProgram* getKawaseRealtimeShader();

/// Blur "cell" de alta calidad (9x9 taps + puntos intermedios) — single-pass.
/// Usado para LevelCell / GJScoreCell / BadgeHooks / CommunityHubLayer, tanto
/// sobre sprites estaticos como sobre CCSprites animados (video / GIF fondo).
cocos2d::CCGLProgram* getBlurCellShader();

/// Dual Kawase single-pass 12-tap — mas barato que `getBlurCellShader`,
/// pensado para GIFs animados en LevelInfoLayer / InfoLayer.
cocos2d::CCGLProgram* getBlurSinglePassShader();

/// Fast blur de intensidad fija (3.5 px). Fallback en ProfileThumbs cuando
/// el realtime del BlurSystem no esta disponible.
cocos2d::CCGLProgram* getBlurFastShader();

/// YUV→RGB GPU shader for VideoPlayer. Uses 3 luminance textures (Y, Cb, Cr)
/// and converts to RGB on the GPU, eliminating CPU-side SIMD conversion.
cocos2d::CCGLProgram* getYUVShader();

/// YUV→RGBA blit shader — renders YUV planes to an RGBA FBO.
/// Used by VideoPlayer::resolveToRGBA() to produce an RGBA CCTexture2D
/// without CPU-side SIMD conversion. Eliminates the forceRGBA path.
cocos2d::CCGLProgram* getYUVBlitShader();

/// DominantColors GPU pre-reduction shader. Converts sRGB→LAB on the GPU
/// and outputs encoded LAB values to a tiny FBO for CPU-side K-means.
cocos2d::CCGLProgram* getDominantColorsDownsampleShader();

} // namespace paimon::shaders
