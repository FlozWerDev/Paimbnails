#include "GLSLLoader.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/cocos/shaders/CCShaderCache.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <system_error>

using namespace cocos2d;

namespace paimon::shaders {

namespace {

// Cache en RAM del contenido leido de disco. Se pobla de forma lazy la
// primera vez que cada archivo se solicita. Shared_mutex porque lo mas
// comun es leer (hit); solo se escribe en la primera carga.
struct ShaderSourceCache {
    std::shared_mutex mutex;
    std::unordered_map<std::string, std::string> contents;
};

ShaderSourceCache& sourceCache() {
    // Heap-alojado intencionalmente para no ejecutar destructores en
    // execute_onexit_table (evita crashes por orden de destruccion de
    // estaticos entre DLLs, igual que el patron usado en PopupBlurService).
    static auto* cache = new ShaderSourceCache();
    return *cache;
}

std::filesystem::path shadersDir() {
    // Dev layout: src/../resources/shaders/<name>.glsl
    return geode::Mod::get()->getResourcesDir() / "shaders";
}

// Geode empaqueta el contenido de `resources` aplanado dentro del .geode
// (todos los archivos quedan en `<resourcesDir>/` sin preservar subfolders).
// En ejecucion instalada la ruta real es `getResourcesDir() / <name>.glsl`.
// Probamos ambas para soportar dev (workspace con subfolder) e instalado.
std::filesystem::path shadersDirFlat() {
    return geode::Mod::get()->getResourcesDir();
}

// Leer el archivo del disco sin tocar el cache. Devuelve string vacio si
// el archivo no existe o no se puede leer.
std::string readFileRaw(std::filesystem::path const& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) return {};

    std::ifstream f(path, std::ios::binary);
    if (!f) return {};

    std::ostringstream buf;
    buf << f.rdbuf();
    if (!f && !f.eof()) return {};
    return buf.str();
}

} // namespace

std::string readShaderFile(std::string_view relName) {
    std::string key(relName);

    {
        std::shared_lock<std::shared_mutex> lock(sourceCache().mutex);
        auto it = sourceCache().contents.find(key);
        if (it != sourceCache().contents.end()) {
            return it->second;
        }
    }

    auto contents = readFileRaw(shadersDir() / key);
    if (contents.empty()) {
        // Fallback al layout plano usado por el .geode instalado.
        contents = readFileRaw(shadersDirFlat() / key);
    }

    {
        std::unique_lock<std::shared_mutex> lock(sourceCache().mutex);
        // Double-check: otro thread pudo haber insertado mientras releamos.
        auto [it, inserted] = sourceCache().contents.emplace(std::move(key), std::move(contents));
        return it->second;
    }
}

void clearShaderFileCache() {
    std::unique_lock<std::shared_mutex> lock(sourceCache().mutex);
    sourceCache().contents.clear();
}

CCGLProgram* loadShader(
    std::string_view cacheKey,
    std::string_view vertexFile,
    std::string_view fragmentFile,
    char const* vertexFallback,
    char const* fragmentFallback
) {
    if (cacheKey.empty()) {
        geode::log::error("[GLSLLoader] loadShader called with empty cacheKey");
        return nullptr;
    }

    auto* shaderCache = CCShaderCache::sharedShaderCache();
    std::string keyStr(cacheKey);
    if (auto* program = shaderCache->programForKey(keyStr.c_str())) {
        return program;
    }

    // Resuelve el source del vertex shader
    std::string vertexFromDisk;
    if (!vertexFile.empty()) {
        vertexFromDisk = readShaderFile(vertexFile);
    }
    char const* vertexSrc = vertexFromDisk.empty() ? vertexFallback : vertexFromDisk.c_str();

    // Resuelve el source del fragment shader
    std::string fragmentFromDisk;
    if (!fragmentFile.empty()) {
        fragmentFromDisk = readShaderFile(fragmentFile);
    }
    char const* fragmentSrc = fragmentFromDisk.empty() ? fragmentFallback : fragmentFromDisk.c_str();

    if (!vertexSrc || !*vertexSrc || !fragmentSrc || !*fragmentSrc) {
        geode::log::error(
            "[GLSLLoader] No source for shader '{}' (vertexFile='{}', fragmentFile='{}')",
            keyStr, vertexFile, fragmentFile);
        return nullptr;
    }

    // Log util para QA: indica de donde vino cada source.
    bool vertexFromFile = !vertexFromDisk.empty();
    bool fragmentFromFile = !fragmentFromDisk.empty();
    geode::log::debug(
        "[GLSLLoader] Compiling '{}' (vertex: {}, fragment: {})",
        keyStr,
        vertexFromFile ? "file" : "inline",
        fragmentFromFile ? "file" : "inline");

    auto* program = new CCGLProgram();
    if (!program->initWithVertexShaderByteArray(vertexSrc, fragmentSrc)) {
        geode::log::error("[GLSLLoader] initWithVertexShaderByteArray failed for '{}'", keyStr);
        program->release();
        return nullptr;
    }

    // Atributos estandar cocos2d. Todos los shaders del mod usan estos
    // nombres; si alguno diverge se puede exponer una sobrecarga adicional.
    program->addAttribute("a_position", kCCVertexAttrib_Position);
    program->addAttribute("a_color", kCCVertexAttrib_Color);
    program->addAttribute("a_texCoord", kCCVertexAttrib_TexCoords);

    if (!program->link()) {
        geode::log::error("[GLSLLoader] link failed for '{}'", keyStr);
        program->release();
        return nullptr;
    }

    program->updateUniforms();
    shaderCache->addProgram(program, keyStr.c_str());
    program->release();
    return shaderCache->programForKey(keyStr.c_str());
}

void preloadBlurShaders() {
    // Compila de golpe las 8 variantes de blur que el mod usa. Asi el
    // primer popup con blur, el primer scroll con LevelCells o el primer
    // LevelInfoLayer no pagan el costo del compile (4-10 ms por shader
    // en drivers normales, visible como micro-stutter).
    //
    // Los shaders viven en resources/shaders/*.glsl (empaquetados planos
    // por Geode en el .geode). Sin fallback inline: si falta el archivo,
    // loadShader loggea `No source for shader '...'` y devuelve nullptr;
    // el caller cae a su propio fallback path (sprite sin blur).

    auto* h     = getBlurHorizontalShader();
    auto* v     = getBlurVerticalShader();
    auto* down  = getKawaseDownShader();
    auto* up    = getKawaseUpShader();
    auto* rt    = getKawaseRealtimeShader();
    auto* cell  = getBlurCellShader();
    auto* sp    = getBlurSinglePassShader();
    auto* fast  = getBlurFastShader();

    int compiled = 0;
    for (auto* p : {h, v, down, up, rt, cell, sp, fast}) if (p) ++compiled;
    geode::log::info(
        "[GLSLLoader] Blur preload completo: {}/8 shaders compilados",
        compiled);

    // Hint util para debug de empaquetado: imprime la ruta absoluta una vez.
    geode::log::debug(
        "[GLSLLoader] Shaders dir (subfolder): {}",
        geode::utils::string::pathToString(shadersDir()));
    geode::log::debug(
        "[GLSLLoader] Shaders dir (flat): {}",
        geode::utils::string::pathToString(shadersDirFlat()));
}

// ─────────────────────────────────────────────────────────────────────
// Helpers tipados — cada shader vive bajo su propia cache key (`-v3`) para
// no mezclarse con programas cacheados bajo keys antiguas si el mod se
// actualiza en caliente. Todos pasan nullptr como fallback (fail-fast si
// el .glsl falta; ver comentario en loadShader).
// ─────────────────────────────────────────────────────────────────────

CCGLProgram* getBlurHorizontalShader() {
    return loadShader(
        "paimon-blur-h-v3",
        "cell_vertex.glsl",
        "blur_h.glsl",
        nullptr,
        nullptr
    );
}

CCGLProgram* getBlurVerticalShader() {
    return loadShader(
        "paimon-blur-v-v3",
        "cell_vertex.glsl",
        "blur_v.glsl",
        nullptr,
        nullptr
    );
}

CCGLProgram* getKawaseDownShader() {
    return loadShader(
        "paimon-kawase-down-v3",
        "cell_vertex.glsl",
        "kawase_down.glsl",
        nullptr,
        nullptr
    );
}

CCGLProgram* getKawaseUpShader() {
    return loadShader(
        "paimon-kawase-up-v3",
        "cell_vertex.glsl",
        "kawase_up.glsl",
        nullptr,
        nullptr
    );
}

CCGLProgram* getKawaseRealtimeShader() {
    return loadShader(
        "paimon-kawase-rt-v3",
        "cell_vertex.glsl",
        "kawase_realtime.glsl",
        nullptr,
        nullptr
    );
}

CCGLProgram* getBlurCellShader() {
    return loadShader(
        "paimon-blur-cell-v3",
        "cell_vertex.glsl",
        "kawase_cell.glsl",
        nullptr,
        nullptr
    );
}

CCGLProgram* getBlurSinglePassShader() {
    return loadShader(
        "paimon-blur-single-v3",
        "cell_vertex.glsl",
        "blur_single.glsl",
        nullptr,
        nullptr
    );
}

CCGLProgram* getBlurFastShader() {
    return loadShader(
        "paimon-blur-fast-v3",
        "cell_vertex.glsl",
        "blur_fast.glsl",
        nullptr,
        nullptr
    );
}

CCGLProgram* getYUVShader() {
    return loadShader(
        "paimon-yuv-v1",
        "yuv_vertex.glsl",
        "yuv_fragment.glsl",
        nullptr,
        nullptr
    );
}

CCGLProgram* getYUVBlitShader() {
    return loadShader(
        "paimon-yuv-blit-v1",
        "yuv_vertex.glsl",
        "yuv_to_rgba_blit.glsl",
        nullptr,
        nullptr
    );
}

CCGLProgram* getDominantColorsDownsampleShader() {
    return loadShader(
        "paimon-dc-downsample-v1",
        "cell_vertex.glsl",
        "dominant_colors_downsample.glsl",
        nullptr,
        nullptr
    );
}

} // namespace paimon::shaders
