# Revisión completa de Paimbnails — 24 de octubre de 2026

Revisión de `src/` en cuatro dimensiones: correctitud y memoria, seguridad y red, rendimiento e hilos, y arquitectura/deuda. El código revisado es el checkout local de `F:\code\Paimbnails-main\Paimbnails-main` (sin `.git`), Geode 5.11.2 y GD 2.2081.

## Resumen

- Inventario: **1.492 archivos** (`736 .cpp`, `754 .hpp/.h`, `2 .mm`), **360.905 líneas**; 295.377 en `.cpp`.
- Cobertura dirigida de los **83 features**, los 10 directorios del núcleo y las fronteras de red/persistencia. No es una lectura línea por línea de las 360k líneas; los archivos de UI gigantes se revisaron por ciclo de vida, callbacks y call sites.
- **101 hallazgos registrados**: 7 P1, 64 P2 y 30 P3. **No se confirmó ningún P0**.
- Estado: **45 corregidos**, 51 diferidos con motivo, 3 abiertos que dependen de una decisión de servidor y 2 parciales.
- Los 130 candidatos de `unwrap()` de los escaneos quedaron triados: **0 sin guarda**. Los 176 del volcado anterior eran en su mayoría falsos positivos por guardas inline y cortocircuitos.
- La suite portable base tenía **1 fallo**: `rtx_render_regression.py` esperaba `phi = max(..., 8.0)` y el shader actual usa `2.5`. Era un test desactualizado; se corrigió el test, no el shader.

El registro completo, con archivo:línea, estado, corrección y test, está en [`findings.json`](../build-clang/review-completa-2026-10-24/findings.json). Las correcciones aplicadas se detallan en [CORRECCIONES_REVISION_COMPLETA_2026-10-24.md](CORRECCIONES_REVISION_COMPLETA_2026-10-24.md).

## Método

1. Inventario con SHA-256 y snapshot comprimido de `src/` antes de editar (`build-clang/review-completa-2026-10-24/snapshots/src-before-review.zip`).
2. Escáneres endurecidos en `tools/`: limpieza de comentarios/strings, `unwrap()`, divisiones, capturas async, rutas de filesystem y entradas de red. Resultados en `build-clang/review-completa-2026-10-24/scans/`.
3. Revisión manual paralela: núcleo/framework, utils/video/blur, hooks globales, red/persistencia y los seis clusters de features. Cada candidato se contrastó leyendo la función completa y sus llamadas; los falsos positivos se descartaron explícitamente.
4. Verificación: compilación con clang-cl, suite portable con ASan/UBSan, bindings, regresión de URLs y guardas estáticas. Los límites se indican al final.

## Cobertura

### Núcleo e infraestructura

| Área | Cobertura | Resultado |
|---|---|---|
| `src/core` (Bootstrap, RuntimeLifecycle, BanGate, ModAuthFlow, Settings, migraciones, módulos) | completa en arranque/apagado, settings y auth | 3 corregidos, 4 P3 diferidos |
| `src/framework` (EventBus, HookInterceptor, PermissionPolicy, SessionState) | completa | 1 P3 (dispatch durante shutdown) |
| `src/platform` | completa | sin hallazgos |
| `src/utils` (113 archivos; foco en HttpClient, WebSocket, AtomicFileWrite, ProjectStorage, GIFDecoder, sprites de vídeo, ThreadPool) | alta | 8 corregidos, varios P3 |
| `src/video` | alta (VideoPlayer, decoders MF/NDK/AVF, audio) | 4 corregidos, 4 diferidos |
| `src/blur`, `src/managers` | alta | sin hallazgos accionables |
| `src/hooks` (46 archivos, 223 `$modify`) | alta; LevelCell/ProfilePage/LevelInfoLayer completos, resto dirigido | 9 corregidos, 4 diferidos |
| `src/layers`, `src/ui` | dirigida a ciclo de vida, popups y settings | 3 corregidos |

### Los 83 features

| Cluster | Features | Cobertura | Hallazgos |
|---|---|---|---|
| A. Miniaturas, búsqueda y datos (12) | auto-preview, foryou, info-suite, level-search, level-thumbs, official-slots, search-history, song-search, thumb-alerts, thumb-requests, thumbnails, updates | completa en servicios/persistencia; UI dirigida | 6 corregidos, 4 P3 |
| B. Social, comunidad y perfiles (13) | badges, collab-editor, comment-mentions, community, forum, golden-best, message-notifications, messages-redesign, moderation, profile-music, profile-redesign, profiles, versus | completa en red/estado; UI dirigida | 7 corregidos, 4 diferidos |
| C. Editor y creación (16) | backgrounds, capture, colorful-icons, editor-animate, editor-colorpicker, editor-filters, editor-music, editor-physics, editor-suite, gif-import, global-icon, icon-copy, icon-gradients, icon-maker, paidraw, texture-studio | alta en persistencia/imports; UI dirigida | 5 corregidos, 5 diferidos |
| D. Render, audio y efectos (17) | audio, beat-shaders, custom-hover, death-effects, dynamic-songs, dynamic-volume, frame-interp, gameplay-performance, menu-loop, menu-music, menu-physics, rtx, smooth-scroll, smooth-text, transitions, visuals, volume-scroll | alta en ciclo GL/audio; UI dirigida | 5 corregidos, 6 diferidos |
| E. UI, hub y menús (18) | cursor, custom-slider, emotes, fonts, garage-hub, hidden-paimon, input-scroll, main-menu-layout, mod-previews, onboarding, paimon-shop, pet, progression, progressbar, quick-hub, scorecell, separate-dual, settings-panel | dirigida a popups, listeners y persistencia | 5 corregidos, 5 P3 |
| F. Integraciones y diagnóstico (7) | compat-mods, crash-reports, dev-tools, discord-presence, feedback, guide, twitch-requests | alta en red local/IPC; UI dirigida | 3 corregidos/parciales, 6 P3 |

## Hallazgos P1

### RC-001 — Cast inválido a `PaimonShaderGradient` (corregido)

`src/hooks/LevelCell.cpp:2945` hacía `static_cast<PaimonShaderGradient*>` sobre `m_gradientLayer`, que en el camino de blur es un `CCSprite` plano (`m_gradientIsPSG = false`). `setIntensity`/`setTime`/`setTexSize` escribían `m_intensity`, `m_time` y `m_texSize` fuera del objeto. Se cambió a `typeinfo_cast` y se degrada el flag si no coincide.

Condición: `levelcell-background-type=gradient`, efecto de shader activo y hover; el camino de blur se usa por defecto para GIFs/fondos. Impacto: corrupción de heap silenciosa.

### RC-002 — `cleanTasks` dejaba el coalescing bloqueado (corregido)

`src/utils/HttpClient.cpp:127` desactivaba `m_callbackGate`, pero los callbacks en vuelo salían antes de reponer `m_manifestFetchInFlight` y de limpiar `m_inflightDownloads`/`m_inflightModChecks`. Tras logout o factory reset, `fetchManifest` y las miniaturas podían quedar coalescidos para siempre.

La corrección drena y falla los callbacks pendientes bajo lock, limpia los mapas y repone la bandera. Cubierto por `tests/review_guards_regression.py`.

### RC-003 — UAF en el ribbon de mod-previews (corregido)

`src/features/mod-previews/hooks/ModPreviewsListener.cpp:69` capturaba `this` y `slot` en callbacks de `LazySprite` que el SDK conserva. Si el popup se cerraba antes de las 10 descargas, el ribbon moría y el callback escribía sobre nodos liberados. Se usó `WeakRef<PreviewRibbon>`, se comprueba que el popup siga en escena y se cancelan las cargas en el destructor.

### RC-004 — `setFntFile` por frame en el progress bar (corregido)

`src/features/progressbar/services/ProgressBarManager.cpp:712` comparaba una ruta absoluta resuelta con la relativa, por lo que `setFntFile` se ejecutaba en cada frame del HUD. Ahora compara `lb->getFntFile()` y solo aplica si cambió.

### RC-005 — Colaboración sobre HTTP plano (abierto)

`src/features/collab-editor/CollabTypes.hpp:13` define `http://node.akiomae.xyz:4401`. El cliente envía `Authorization: Bearer` en join/leave/resync/poll y presencia. Un observador de red puede capturar el token de sesión y actuar como el peer.

No se cambió el default a `https://` porque el endpoint actual podría no ofrecer TLS y se rompería la función. **Requiere decisión de servidor**: publicar TLS en el mismo puerto o un `wss` propio y luego exigir `https` en `normalizeBaseUrl` para hosts no loopback.

### RC-006 — Versus confía en `accountId` auto-declarado (abierto)

`src/features/versus/services/VersusClient.cpp:191` envía `accountId` y nombre en `/api/auth`; el único credencial es `X-Mod-Code`, que un jugador normal no tiene. El token emitido se usa para resultado, challenge y reportes. Es un problema de confianza del backend, no corregible solo en cliente. **Requiere prueba de identidad del servidor** (reutilizar el patrón `modauth`).

### RC-007 — Forum envía la autoría en el body (abierto)

`src/features/forum/services/ForumApi.cpp:398` incluye `author` en el body; los endpoints de forum no están en la lista de acciones de moderación de `HttpClient::postWithAuth`. Un cliente modificado puede publicar/borrar como otra cuenta si el servidor confía en el body. **Requiere sesión firmada del backend**.

## Hallazgos P2 destacados

La tabla siguiente resume los corregidos de mayor impacto. El resto está en `findings.json`.

| ID | Hallazgo | Estado y corrección |
|---|---|---|
| RC-008 | `cdnUrl` del manifiesto se descargaba sin `isUrlSafe` | Corregido: validación en `parseManifestEntry`, `_cdnBaseUrl` y `fetchViaCdn`. |
| RC-009 | `isUrlSafe` se saltaba con IPv4 decimal/hex, octal e IPv6 mapeada | Corregido: `paimon::net::isPublicHttpUrl` en `HttpUrl.hpp`, con test real. |
| RC-010 | Auto-update descargaba el `.geode` desde cualquier host | Corregido: solo `https` + GitHub/githubusercontent. Digest SHA-256 queda pendiente. |
| RC-011 | Inyección de opciones en `yt-dlp` | Corregido: rechazo de URLs que empiezan por `-` y `--` antes de la URL. |
| RC-012 | URL de LevelThumbs tomada de otro mod sin validar | Corregido: `isUrlSafe` y fallback al API por defecto. |
| RC-013 | Cabeceras con CRLF y `postBadgeForm` sin origen | Corregido: nombre de cabecera válido, sin CR/LF y origen de confianza. |
| RC-014 | Factory reset borraba proyectos y capturas de usuario | Corregido: se mueven a `factory-reset-backup-<epoch>/` antes de limpiar. |
| RC-015 | Import `.paimbicon` sin cotas y con colisiones | Corregido: 512 entradas, 64 MB, extracción a archivo, dedupe y borrado del slot ante fallo. |
| RC-016 | Caché de iconos globales no atómica/truncada | Corregido: escritura atómica y validación PNG/plist. |
| RC-017 | Rollback de física dejaba animaciones huérfanas | Corregido: elimina los grupos de keyframes y refresca. |
| RC-019 | IconCopy aplicaba ids/colores fuera de rango | Corregido: clamp de iconos y colores. |
| RC-023 | SongCoverCache perdía callbacks en cooldown/cambio | Corregido: callbacks con `false` en todos los descartes. |
| RC-024 | BeatShader pisaba el shader persistido de la capa | Corregido: guarda/restaura el shader previo. |
| RC-025 | `PaimonAudio::activate` no informaba fallo | Corregido: devuelve `bool` y el manager reintenta. |
| RC-029 | `library.json` ilegible podía sobrescribirse | Corregido: se preserva como `.corrupt` antes del primer save. |
| RC-030/031 | Layout aplicaba `.fnt` inexistente y guardaba con `trunc` | Corregido: valida `fullPathForFilename` y usa `writeAtomically`. |
| RC-032 | ShopImages sobrevivía al reload de GL | Corregido: `clear()` en `GLContextReload` y callbacks pendientes a `nullptr`. |
| RC-037/038 | Overlay local: clientes lentos y Host no validado | Parcial: deadline de 2 s y Host loopback; falta desacoplar el cliente del hilo `accept`. |
| RC-040/041 | Hub: overlay duplicado y respuestas de forum fuera de orden | Corregido: guard de overlay y secuencia de petición. |
| RC-048 | AudioExtractor MF sin cota/cancelación | Corregido: 256 MB, watchdog 30 s y shutdown. |
| RC-049 | VideoPlayer recreaba el decoder en loop tras error | Corregido: detecta EOS sin frame nuevo y para. |
| RC-050 | Descarga de vídeo permitía 2 GB y escribía en main | Parcial: cap 64 MB; escritura async pendiente. |
| RC-051 | `createAsync` descartaba el callback si el worker paró | Corregido: callback `nullptr` fuera del lock. |
| RC-058/059 | Galería de LevelPage sin tope y ciclo `Ref` en comentarios | Corregido: tope de 3 fallos y `WeakRef` en los tres callbacks hijos. |
| RC-060/061/062 | Reentrancia de LevelCell, doble carrusel y tipo de thumb cacheado | Corregido: diferido a main, guard de carrusel y reset del tipo. |
| RC-064 | LevelInfoLayer descomprimía el nivel en main | Corregido: decodificación e inspección en `ThreadTracker`. |
| RC-067 | Invalidación remota no refrescaba una celda visible | Corregido: flag `m_thumbInvalidated` que impide reutilizar el sprite viejo. |
| RC-069/070 | `InfoStore` con `ofstream trunc` y SearchHistory sin tope | Corregido: escritura atómica y tope de 100 entradas. |
| RC-071/072 | LevelThumbs mezclaba calidades; cancel no cortaba red sin disco | Corregido: clave `(levelID, quality)` y chequeo de `cancelled` temprano. |
| RC-074/075/078 | Collab aceptaba perms de cualquier peer, pings sin cota, VersusSession estático | Corregido: solo host, tope 24 y sesión heap process-lifetime. |

## Diferidos y deuda

Los 51 diferidos están clasificados en `findings.json`. Los grupos principales:

- **Decisión de servidor**: colaboración TLS, versus auth y autoría del foro.
- **Cambios de arquitectura**: consolidar `HttpClient`/`HttpTransport`, cota de `m_cdnBaseURL` con lock/shared_ptr, cola de aplicación remota de collab, scheduler de RTX, caché de galería de cursores, hitos de render de UI.
- **Rendimiento en main thread**: descarga/escritura de actualización, decodificación síncrona de `AnimatedGIFSprite::create`, init de cursor/pet, redibujado del editor de layout, `EmotePicker`, `HoverRuntime`.
- **Privacidad**: scrub de crash logs, outbox de feedback con capturas, tokens persistidos en claro.
- **Persistencia**: snapshots de `LocalBasePack`, identidad de Animate, slots fantasma en `saveIndex`, `schemaVersion` futuro, caché GIF no atómica, WAV parcial.
- **Concurrencia/lifetime**: `VideoAudioTrack*` en fade, `shutdownWorker` sin timeout real, `GJLevelScoreCell` con nodos crudos, hook global de `removeFromParentAndCleanup`.

## Verificación

| Comprobación | Resultado |
|---|---|
| Suite portable base | 75 checks, 1 fallo (RTX desactualizado) |
| Suite portable final con ASan/UBSan | **76 checks, 0 fallos** |
| Bindings | 3/3 PASS |
| Escaneos endurecidos | 6 escáneres + 9 legacy; salidas en `scans/` |
| `unwrap()` | 130 candidatos, 0 sin guarda |
| Regresión de URLs | `http_url_safety` PASS (loopback, IPv4 decimal/hex/octal, IPv6 mapeada) |
| Guardas estáticas | 17/17 PASS |
| Compilación clang-cl | PASS (log en `logs/build-final-2.log`) |
| Empaquetado `.geode` | PASS (`flozwer.paimbnails2.geode`, instalado por la configuración local de deploy) |
| Compilación MSVC | Bloqueada por la regeneración de Codegen con CMake 4.4 (Boost/pegtl), no por el diff |
| Archivos `src` modificados | 42, con hashes antes/después en `changed-files.json` |

Comandos usados:

```powershell
python tests/run_regressions.py --suite portable --build-dir build-clang --sanitize
python tests/run_regressions.py --suite bindings --build-dir build-clang --sdk "$env:GEODE_SDK"
python tests/review_behavior_regression.py --build-dir build-clang
python tests/review_guards_regression.py
cmake --build build-clang --target Paimbnails_PACKAGE --parallel 16
```

## Límites

- No se abrió Geometry Dash ni se probaron Android/Apple/GPU real. Los sanitizadores cubren bancos aislados, no el proceso del juego.
- No se contactaron servidores de producción. Las pruebas de red usan stubs; las de URL son puramente locales.
- No se revisó cada línea de los 360k; se cubrieron todos los módulos con foco en las rutas de riesgo.
- Los escáneres de división (925 candidatos tras afinar el filtro; 949 antes) y de red (651) siguen siendo generadores heurísticos. Se revisaron las rutas de riesgo y los divisores de contenedores/config, pero no se agotó cada candidato individual; los que quedan son en su mayoría geometría/constantes o están guardados fuera de la ventana del escáner.
- El checkout no tiene `.git`: no hay atribución por commit. La evidencia son hashes, snapshots y logs locales.

## Checklist manual pendiente

1. Arranque y cierre del juego; abrir/cerrar el hub y el editor.
2. Subir/reemplazar una miniatura con la celda visible en el browser (RC-067).
3. Abrir/cerrar 20 popups de comentarios y comprobar que no quedan capas retenidas (RC-059).
4. Importar un `.paimbicon` válido y uno con nombres duplicados (RC-015).
5. Factory reset y comprobar la carpeta `factory-reset-backup-*` (RC-014).
6. Reproducir vídeo truncado en bucle y confirmar que no reinicia el decoder por frame (RC-049).
7. Cambiar el shader de una capa con beat-shaders y desactivarlo (RC-024).
8. Abrir el overlay de OBS con `Host` normal y con un host ajeno (RC-038).
