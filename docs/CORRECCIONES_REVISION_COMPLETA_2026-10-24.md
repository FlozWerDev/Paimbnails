# Correcciones de la revisión completa — 24 de octubre de 2026

Se aplicaron correcciones sobre los hallazgos P1/P2 que podían resolverse en cliente con evidencia y prueba. Los tres P1 abiertos dependen del servidor (TLS de colaboración, prueba de identidad de versus y sesión firmada del foro) y se documentan sin cambiar el comportamiento para no romper esas funciones.

- Correcciones aplicadas: **45** (4 P1, 39 P2, 2 P3).
- Correcciones parciales: **2** (cap o guarda local; el resto requiere un cambio mayor).
- Registro completo: [`findings.json`](../build-clang/review-completa-2026-10-24/findings.json).

## Correcciones aplicadas

| ID | Prioridad | Hallazgo | Ubicación | Corrección | Verificación |
|---|---|---|---|---|---|
| RC-001 | P1 | Cast a PaimonShaderGradient sobre un CCSprite plano escribe fuera del objeto | `src/hooks/LevelCell.cpp:2945` | typeinfo_cast + flag m_gradientIsPSG; degradar a CCSprite si no coincide | revision estatica + ASan manual con gradiente + efecto de shader |
| RC-002 | P1 | cleanTasks deja m_manifestFetchInFlight y los mapas inflight bloqueados | `src/utils/HttpClient.cpp:127` | resetear bandera, drenar callbacks pendientes y limpiar mapas bajo lock | tests/review_guards_regression.py |
| RC-003 | P1 | Callbacks de LazySprite del ribbon de mod-previews usan this crudo tras cerrar el popup | `src/features/mod-previews/hooks/ModPreviewsListener.cpp:69` | WeakRef al ribbon, cancelLoad en destructor y comprobar popup en escena | revision estatica; pendiente prueba en juego |
| RC-004 | P1 | setFntFile del progress bar se ejecuta cada frame | `src/features/progressbar/services/ProgressBarManager.cpp:712` | comparar getFntFile() antes de reaplicar | tests/review_guards_regression.py |
| RC-008 | P2 | cdnUrl del manifiesto se descarga sin isUrlSafe | `src/utils/HttpClient.cpp:1091` | validar cdnUrl y _cdnBaseUrl; guarda tambien en fetchViaCdn | tests/review_fixtures/http_url_safety.cpp.in |
| RC-009 | P2 | isUrlSafe permite IPv4 decimal/hex, octal e IPv6 mapeada | `src/utils/HttpUrl.hpp:122` | nuevo isPublicHttpUrl con allowlist de IPv6 global y rechazo de hosts numericos ambiguos | tests/review_fixtures/http_url_safety.cpp.in |
| RC-010 | P2 | UpdateChecker descarga el .geode desde cualquier host | `src/features/updates/services/UpdateChecker.cpp:317` | exigir https + github.com/githubusercontent; digest queda pendiente | tests/review_guards_regression.py |
| RC-011 | P2 | yt-dlp interpreta una URL que empieza por - como opcion | `src/features/menu-music/services/YtDlpDownloader.cpp:514` | rechazar URLs que empiezan por - y anadir -- antes de la URL | tests/review_guards_regression.py |
| RC-012 | P2 | LevelThumbs usa el ajuste de otro mod como URL sin validar | `src/features/level-thumbs/services/LevelThumbsClient.cpp:129` | isUrlSafe y fallback al API por defecto | tests/review_guards_regression.py |
| RC-013 | P2 | Cabeceras HTTP aceptan CRLF y postBadgeForm no valida origen | `src/utils/HttpClient.cpp:333` | validar nombre de cabecera, rechazar CR/LF y comprobar isTrustedBackendUrl | tests/review_guards_regression.py |
| RC-014 | P2 | Factory reset borra proyectos de usuario y capturas | `src/core/FactoryResetActions.cpp:44` | mover icon-maker, texture-studio, menu-music y capture a factory-reset-backup-* | tests/review_guards_regression.py |
| RC-015 | P2 | Import .paimbicon sin tope de entradas/bytes y con nombres que colisionan | `src/features/icon-maker/services/IconShare.cpp:74` | caps 512 entradas/64 MB, extractTo, dedupe y borrado del slot ante fallo | tests/review_guards_regression.py |
| RC-016 | P2 | Cache de iconos globales no atomica y sin validar PNG/plist | `src/features/global-icon/services/GlobalIconStorage.cpp:26` | writeAtomically + validacion de cabecera | tests/review_guards_regression.py |
| RC-017 | P2 | Rollback de fisica deja animaciones de keyframes huerfanas | `src/features/editor-physics/services/PhysicsTriggerEmitter.cpp:677` | eliminar los grupos creados y refrescar keyframes en abort | revision estatica |
| RC-019 | P2 | IconCopy escribe ids de icono/color sin rango | `src/features/icon-copy/IconCopyStore.cpp:99` | clamp de ids y colores antes de GameManager | tests/review_guards_regression.py |
| RC-023 | P2 | SongCoverCache descarta callbacks de otras canciones y en cooldown | `src/features/menu-music/services/SongCoverCache.cpp:618` | invocar callbacks con false en todos los caminos de descarte | tests/review_guards_regression.py |
| RC-024 | P2 | BeatShader sobrescribe el shader persistido de la capa | `src/features/beat-shaders/services/BeatShaderManager.cpp:401` | guardar/restaurar el shader previo en una clave aparte | tests/review_guards_regression.py |
| RC-025 | P2 | PaimonAudio::activate es void y el manager marca activo aunque falle | `src/features/audio/services/PaimonAudio.cpp:39` | devolver bool y reintentar | tests/review_guards_regression.py |
| RC-029 | P2 | MenuMusicLibrary puede sobrescribir una library.json ilegible | `src/features/menu-music/services/MenuMusicLibrary.cpp:527` | preservar como .corrupt antes del primer save | tests/review_guards_regression.py |
| RC-030 | P2 | MainMenuLayout aplica setFntFile sin validar el archivo | `src/features/main-menu-layout/services/MainMenuLayoutManager.cpp:1028` | fullPathForFilename antes de aplicar | tests/review_guards_regression.py |
| RC-031 | P2 | Layout y presets se guardan con ofstream trunc | `src/features/main-menu-layout/services/MainMenuLayoutManager.cpp:605` | writeAtomically | tests/review_guards_regression.py |
| RC-032 | P2 | ShopImages no se invalida en GL context reload | `src/core/GLContextReload.cpp:54` | clear de ShopImages y callbacks pendientes a nullptr | tests/review_guards_regression.py |
| RC-038 | P2 | Overlay no valida Host (DNS rebinding) | `src/features/twitch-requests/services/StreamOverlayServer.cpp:559` | rechazar Host distinto de localhost/127.0.0.1/[::1] | tests/review_guards_regression.py |
| RC-040 | P2 | onCreateTag permite overlays duplicados y huerfanos | `src/layers/PaimonHubLayer.cpp:1582` | early-return si m_createTagOverlay existe | tests/review_guards_regression.py |
| RC-041 | P2 | refreshForumPosts aplica respuestas fuera de orden | `src/layers/PaimonHubLayer.cpp:1654` | secuencia m_forumRequestSeq | tests/review_guards_regression.py |
| RC-042 | P3 | BanGate confia en cache si el reloj se atrasa | `src/core/BanGate.cpp:143` | expirar si now < checkedAt | tests/review_guards_regression.py |
| RC-048 | P2 | AudioExtractor MF no tiene cota, cancelacion ni watchdog | `src/video/AudioExtractor.cpp:96` | cap 256 MB, deadline 30 s y chequeo de shutdown | tests/review_guards_regression.py |
| RC-049 | P2 | VideoPlayer recrea el decoder en loop si el stream falla | `src/video/VideoPlayer.cpp:587` | detectar EOS sin frame nuevo y parar | tests/review_guards_regression.py |
| RC-050 | P2 | VideoThumbnailSprite permite descargar 2 GB y escribe en main | `src/core/Settings.hpp:244` | cap 64 MB; escritura async pendiente | tests/review_guards_regression.py |
| RC-051 | P2 | AnimatedGIFSprite::createAsync pierde el callback si el worker paro | `src/utils/AnimatedGIFSprite.cpp:1036` | invocar callback(nullptr) fuera del lock | tests/review_guards_regression.py |
| RC-058 | P2 | LevelPage reintenta la galeria sin limite | `src/hooks/LevelPage.cpp:135` | contador de fallos con tope 3 | tests/review_guards_regression.py |
| RC-059 | P2 | ShareCommentLayer tiene ciclos Ref con EmoteButton/FontButton/Autocomplete | `src/hooks/ShareCommentLayer.cpp:105` | WeakRef en todos los callbacks hijos | tests/review_guards_regression.py |
| RC-060 | P2 | updateMaintenance reconstruye la lista desde el tick de la celda | `src/hooks/LevelCell.cpp:2424` | diferir load/setupList a main con Ref y levelID | tests/review_guards_regression.py |
| RC-061 | P2 | MapPackCell puede crear dos carruseles | `src/hooks/MapPackCell.cpp:48` | guard m_carousel | tests/review_guards_regression.py |
| RC-062 | P2 | m_thumbTypeCached no se invalida al sustituir el sprite | `src/hooks/LevelCell.cpp:828` | reset al asignar m_thumbSprite | tests/review_guards_regression.py |
| RC-064 | P2 | LevelInfoLayer descomprime el nivel en main | `src/hooks/LevelInfoLayer.cpp:238` | mover decompress + inspeccion a ThreadTracker | tests/review_guards_regression.py |
| RC-067 | P2 | Invalidacion remota no recarga una celda con sprite montado | `src/hooks/LevelCell.cpp:3162` | flag m_thumbInvalidated y no reutilizar el sprite viejo | tests/review_guards_regression.py |
| RC-069 | P2 | InfoStore guarda con ofstream trunc por cada respuesta | `src/features/info-suite/services/InfoStore.cpp:101` | writeAtomically; debounce pendiente | tests/review_guards_regression.py |
| RC-070 | P2 | SearchHistory crece sin limite | `src/features/search-history/SearchHistory.cpp:95` | tope 100 entradas | tests/review_guards_regression.py |
| RC-071 | P2 | LevelThumbs deduplica inflight ignorando la calidad | `src/features/level-thumbs/services/LevelThumbsClient.cpp:187` | clave (levelID, quality) | tests/review_guards_regression.py |
| RC-072 | P3 | workerLoadFromDisk descarga red con el disco apagado aunque este cancelada | `src/features/thumbnails/services/ThumbnailLoader.cpp:836` | comprobar cancelled antes del branch | tests/review_guards_regression.py |
| RC-074 | P2 | Collab aplica perms y level_settings de cualquier peer | `src/features/collab-editor/CollabManager.cpp:880` | exigir que el emisor sea el host | tests/review_guards_regression.py |
| RC-075 | P2 | m_peerPings crece sin cota fuera del editor | `src/features/collab-editor/CollabManager.cpp:2481` | tope 24 en handlePeerPing | tests/review_guards_regression.py |
| RC-078 | P2 | VersusSession es un static con Refs a capas vivas | `src/features/versus/services/VersusSession.cpp:49` | instancia heap process-lifetime | tests/review_guards_regression.py |
| RC-101 | P2 | ProfileImageService acepta subir a cualquier accountID | `src/features/profiles/services/ProfileImageService.cpp:279` | comparar accountID con GJAccountManager en las cuatro entradas | tests/review_guards_regression.py |
| RC-037 | P2 | Overlay local sirve clientes en serie y stop hace join | `src/features/twitch-requests/services/StreamOverlayServer.cpp:533` | deadline total de 2 s; falta hilo por cliente/join diferido | tests/review_guards_regression.py |
| RC-068 | P2 | UpdateChecker escribe y valida hasta 256 MB en main | `src/features/updates/services/UpdateChecker.cpp:376` | cap reducido a 64 MB; mover write/Unzip a worker pendiente | pendiente |

## Archivos de producción modificados

- `src/hooks/LevelCell.cpp`
- `src/utils/HttpClient.cpp`
- `src/features/mod-previews/hooks/ModPreviewsListener.cpp`
- `src/features/progressbar/services/ProgressBarManager.cpp`
- `src/utils/HttpUrl.hpp`
- `src/features/updates/services/UpdateChecker.cpp`
- `src/features/menu-music/services/YtDlpDownloader.cpp`
- `src/features/level-thumbs/services/LevelThumbsClient.cpp`
- `src/core/FactoryResetActions.cpp`
- `src/features/icon-maker/services/IconShare.cpp`
- `src/features/global-icon/services/GlobalIconStorage.cpp`
- `src/features/editor-physics/services/PhysicsTriggerEmitter.cpp`
- `src/features/icon-copy/IconCopyStore.cpp`
- `src/features/menu-music/services/SongCoverCache.cpp`
- `src/features/beat-shaders/services/BeatShaderManager.cpp`
- `src/features/audio/services/PaimonAudio.cpp`
- `src/features/menu-music/services/MenuMusicLibrary.cpp`
- `src/features/main-menu-layout/services/MainMenuLayoutManager.cpp`
- `src/core/GLContextReload.cpp`
- `src/features/twitch-requests/services/StreamOverlayServer.cpp`
- `src/layers/PaimonHubLayer.cpp`
- `src/core/BanGate.cpp`
- `src/video/AudioExtractor.cpp`
- `src/video/VideoPlayer.cpp`
- `src/core/Settings.hpp`
- `src/utils/AnimatedGIFSprite.cpp`
- `src/hooks/LevelPage.cpp`
- `src/hooks/ShareCommentLayer.cpp`
- `src/hooks/MapPackCell.cpp`
- `src/hooks/LevelInfoLayer.cpp`
- `src/features/info-suite/services/InfoStore.cpp`
- `src/features/search-history/SearchHistory.cpp`
- `src/features/thumbnails/services/ThumbnailLoader.cpp`
- `src/features/collab-editor/CollabManager.cpp`
- `src/features/versus/services/VersusSession.cpp`
- `src/features/profiles/services/ProfileImageService.cpp`

También se modificaron `.gitignore`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `compilar-desde-cero.ps1`, `tests/run_regressions.py`, `tests/rtx_render_regression.py`, `tests/README.md` y `tools/` (escáneres y runner).

## Pruebas añadidas o actualizadas

- `tests/review_fixtures/http_url_safety.cpp.in`: loopback, IPv4 decimal/hex/octal, IPv6 mapeada, ULA/link-local y esquemas no HTTP.
- `tests/review_guards_regression.py`: 17 guardas estáticas para los fixes que dependen del motor o de red.
- `tests/rtx_render_regression.py`: el mínimo de `phi` del shader actual (2.5), en lugar del valor obsoleto 8.0.

## Verificación

| Comprobación | Resultado |
|---|---|
| Compilación clang-cl | PASS (`build-clang/review-completa-2026-10-24/logs/build-final-2.log`); `.geode` empaquetado e instalado por el deploy local |
| Suite portable con ASan/UBSan | **PASS (76 checks, 0 fallos)** |
| Bindings | PASS (3/3) |
| URLs | PASS (`http_url_safety`) |
| Guardas estáticas | PASS (17/17) |
| Compilación MSVC | Bloqueada por CMake 4.4 + Boost/pegtl al regenerar Codegen, no por el diff |
| Archivos `src` modificados | 42 (`changed-files.json`) |

## Límites

- Los fixes de UI y hooks no se probaron dentro de Geometry Dash; se verifican por compilación, revisión estática y pruebas aisladas. El checklist manual está en el informe principal.
- No se modificaron servidores ni se contactaron endpoints de producción.
- Los cambios se hicieron sobre un checkout sin `.git`; la reversión se apoya en el snapshot `snapshots/src-before-review.zip` y en los hashes del inventario.

