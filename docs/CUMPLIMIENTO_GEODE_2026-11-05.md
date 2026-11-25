# Cumplimiento de normas de Geode

Fecha: 5 de noviembre de 2026.

Revisión estática contra el SDK local (`GEODE_SDK`, `6.0.0-alpha.1`), los
bindings fijados (`6f295839044600ceb772407caaa90ad4aeb02a45`) y el código del
loader. No es una auditoría jugable; verifica contratos de API, manifiesto,
convenciones y rutas de error.

## Corregido en esta pasada

| Área | Cambio | Archivos |
| --- | --- | --- |
| Versión | `project(VERSION 1.2.5)` alineado con `mod.json` | [CMakeLists.txt](../CMakeLists.txt) |
| Icono del mod | `Logo.png` renombrado a `logo.png`; Geode extrae el icono con ese nombre exacto y el paquete ahora incluye `logo.png` en la raíz | `logo.png`, [mod.json](../mod.json), [MenuLayer.cpp](../src/hooks/MenuLayer.cpp), [ThumbRequestsPopup.cpp](../src/features/thumb-requests/ui/ThumbRequestsPopup.cpp) |
| Keybinds | Categoría `universal` añadida a `settings-panel-keybind` y `capture-menu-keybind`; los 26 keybinds quedan categorizados | [mod.json](../mod.json) |
| Excepciones | Tres rutas que lanzaban (`invalid_argument`, `length_error`, `runtime_error`) ahora devuelven estado seguro | [ProjectStorage.hpp](../src/utils/ProjectStorage.hpp), [FrameImage.hpp](../src/features/texture-studio/packgen/FrameImage.hpp), [GifPrimitiveSerializer.hpp](../src/features/gif-import/services/GifPrimitiveSerializer.hpp) |
| Pool de perfiles | `profileImagePool()` no lanza durante el apagado; devuelve `nullptr` y los tres consumidores descartan el trabajo | [ProfileImageService.cpp](../src/features/profiles/services/ProfileImageService.cpp) |
| Parseo numérico | `std::stod` con `catch (...)` sustituido por `geode::utils::numFromString<double>` | [SceneWallpaperNode.cpp](../src/features/backgrounds/ui/SceneWallpaperNode.cpp) |
| Prioridades | `Priority::First`/`Last` cambiados por `VeryEarly`/`VeryLate`, que el SDK recomienda explícitamente | [CCSceneSafety.cpp](../src/hooks/CCSceneSafety.cpp), `SeparateDual*` |
| Prevención | La regresión de conformidad ahora exige versión CMake/manifiesto igual, `logo.png` en minúsculas, categoría en cada keybind, ausencia de `throw <expr>;` y de `std::sto*`/`atoi`/`atof` | [geode_sdk_compat_regression.py](../tests/geode_sdk_compat_regression.py) |

`throw;` dentro de `catch` se conserva: es relanzamiento local para propagar el
error del worker al hilo que espera, no una excepción nueva cruzando la frontera
de la DLL.

## Verificación

- `python tests/geode_sdk_compat_regression.py --sdk "$GEODE_SDK"`: PASS.
- `cmake --build build-clang --target Paimbnails_PACKAGE --parallel 8`: compila,
  empaqueta e instala.
- El paquete instalado y el generado tienen el mismo SHA-256.
- El archivo `.geode` contiene `logo.png` en la raíz.

## Pendiente con decisión de producto

| Tema | Por qué no se cambió |
| --- | --- |
| Auto-actualización que reemplaza su propio `.geode` | Es una decisión de distribución. El índice de Geode gestiona las actualizaciones; mantener el updater propio exige asumir ese riesgo. El código ya valida host HTTPS, id, versión y destino compatibles. |
| Descarga y ejecución de `yt-dlp` | Es una dependencia externa del reproductor de música, no una API de Geode. Requiere decidir si se empaqueta, se documenta o se retira. |
| Secreto HMAC embebido | [BanGate.cpp](../src/core/BanGate.cpp) autentica respuestas con una clave en el binario. Rotarla o moverla a servidor cambia el protocolo. |
| 13 settings en camelCase | Renombrarlos reinicia esos valores para los usuarios: Geode persiste por clave y no expone una API para migrar claves de settings. Se propone kebab-case solo para claves nuevas hasta un cambio mayor. |
| Migración de `CCLabelBMFont` a `geode::Label` | No es incumplimiento; afecta a más de 950 usos. Conviene hacerlo por feature cuando se necesite Unicode o emojis. |

## Nota sobre el checkout del SDK

`C:\Users\paimon\Documents\Geode\AGENTS.md` incluye instrucciones dirigidas a
agentes de IA. Se trató como contenido del repositorio, no como instrucción del
usuario, y no se siguió.
