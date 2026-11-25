# Correcciones de la auditoría del 16 de octubre de 2026

Se aplicaron las correcciones solicitadas sobre los 18 hallazgos de [la auditoría](AUDITORIA_EXHAUSTIVA_2026-10-16.md): 3 P1, 14 P2 y 1 P3. La verificación de código y componentes terminó sin fallos pendientes en los bancos ejecutados. No se inició, enlazó ni instaló el mod ni se contactó a servidores del proyecto.

| Hallazgo | Corrección | Verificación específica |
|---|---|---|
| R01 | Nombres de archivo restringidos, contención de rutas resueltas y rechazo de cuentas/tipos inválidos. La limpieza valida su directorio y no sigue symlinks de entradas. | `global_icon_paths`: traversal, rutas absolutas, ADS/nombres reservados, plist inválido, limpieza permitida y conservación de un archivo ajeno ficticio. |
| R02 | Identidad aleatoria persistente en el diccionario del nivel; documentos propios de cada clon; nombre independiente de la clave. | `animate_identity`: mismo nombre, renombrado, reapertura, clon, copia, identidad no válida y migración. Compilación de hooks reales de serialización. |
| R03 | Regresiones de CI después del build que obtiene las dependencias, condicionadas a su éxito. | Revisión del orden del workflow y ejecución local de la suite con dependencias reales. No se ejecutó una CI remota. |
| R04 | Barras de Quick Hub con referencias débiles en teclado y táctil. El hold de teclado se cancela al cambiar de escena. | `quick_hub_lifetime` con nodos liberados bajo ASan y contrato Android existente. |
| R05 | Error permanente del codec aborta y libera recursos; cancelación por cierre, deadline de progreso, verificación de buffers y límite de PCM. | `android_audio_permanent_error_loop` con APIs NDK simuladas; no dispositivo Android. |
| R06 | Solo se acepta un nivel con el ID solicitado; errores sin identidad esperan al deadline de la generación vigente. | `auto_preview_late_response`: timeout, respuesta antigua, fallo anónimo, respuesta correcta y nuevo timeout. |
| R07 | Generación de sesión e identidad de partida en callbacks de cola, watch, poll, aceptación, veto y ready; reset reprograma el watch. | `versus_late_poll`: cancelación, nueva generación y respuesta válida. Compilación de todos los callbacks cambiados. |
| R08 | Decoder acepta `Revive`, manteniendo rechazo de valores desconocidos y mensajes truncados. | `versus_revive_wire`: round-trip de todos los estados del protocolo. |
| R09 | Cero se respeta; negativos/excesos se limitan y NaN usa fallback. | `collab_voice_zero_volume`: 0, 0,5, negativo, exceso y NaN. |
| R10 | Solo una respuesta HTTP correcta con objeto JSON válido puede persistir etiquetas o ausencia de ellas; la caché v2 descarta el snapshot antiguo potencialmente contaminado. | `level_tags_failed_request_cache`: error HTTP, JSON inválido, array, objeto vacío y cache hit posterior. |
| R11 | Clave con los cuatro campos omitidos y prefijos de longitud para los strings. | `search_identity`: cambia filtros por separado y comprueba identidades distintas. |
| R12 | Interruptor aplicado a botones, apertura de popups y acciones de guardar/usar/borrar. | `preset_toggle` con la búsqueda avanzada abierta y alternancia del control; registro común de los 132 módulos. |
| R13 | Escape delegado al serializer real de matjson y guardado atómico de la cola. | `pending_queue_json`: round-trip de los 32 controles ASCII, comillas, barra y Unicode, enlazando matjson real. |
| R14 | Intersección del rango con 1–9999 antes de iterar. | `editor_animate_regression`: mil millones de IDs inválidos, límites de int, intersección y orden descendente. |
| R15 | Reemplazo atómico de máscaras, fusiones, presets, progreso y slots; importación de textura conserva el formato anterior hasta confirmar el reemplazo. Presets propaga el error, conserva la lista anterior y mantiene el diálogo abierto. | `atomic_file_write_regression`, regresiones de texturas y `preset_save_failure` (fallos de alta, actualización, límite y borrado) y `fusion_texture_failure` (PNG/GIF anterior intacto ante fallo). |
| R16 | Estado de GCE restablecido después de cada imagen, con disposal previo separado. | `gif_decoder_regression`: dos imágenes con GCE solo en la primera, opacidad y demora de la segunda. |
| R17 | El WeakRef se bloquea antes de leer miembros; callback sin captura de `this`. | `comment_popup_lifetime`: respuesta tras liberar el popup, token viejo y respuesta vigente. |
| R18 | Copia del callback al construir el aviso, conservando el fallback. | `beta_warning_fallback`: inicialización fallida, aceptación y aviso descartado. |

## Compatibilidad de datos

Animate guarda su identidad en `paimbnails-animate-id-v2` del diccionario nativo del nivel, sin cambiar su nombre, descripción ni grupos. Un clon recibe su propio documento. Un timeline antiguo basado en nombre se copia automáticamente solo cuando no hay varios niveles locales con ese nombre; el archivo original se conserva como recuperación. Si hay ambigüedad, no se asigna automáticamente a un nivel. Un documento vacío se guarda como tal para evitar que la migración resucite clips eliminados.

Los errores de descarga de GD llevan una respuesta, no un ID de nivel. Auto Preview no atribuye un error tardío a una petición nueva: espera su respuesta identificada o el timeout existente de 20 segundos. Ese camino puede tardar en informar un fallo.

El reemplazo atómico conserva el archivo anterior cuando no se puede completar la escritura/reemplazo. No hace transaccional una operación de varios archivos ni garantiza durabilidad física ante un corte de energía; no se ejecutaron pruebas de corte de energía.

## Límites

Las pruebas de callbacks usan dobles de motor/red y las de archivos solo datos ficticios en directorios de test. Falta integración dentro de Geometry Dash, compatibilidad de hooks con otros mods, dispositivos Android/Apple, renderer GPU y servidor privado. Los resultados no equivalen a probar todas las pantallas del juego.

## Ubicaciones del código corregido

| Hallazgo | Código actual |
|---|---|
| R01 | [GlobalIconStorage.cpp:57](F:/code/Paimbnails-main/Paimbnails-main/src/features/global-icon/services/GlobalIconStorage.cpp:57) |
| R02 | [AnimateStore.cpp:200](F:/code/Paimbnails-main/Paimbnails-main/src/features/editor-animate/services/AnimateStore.cpp:200); [AnimateEditorHooks.cpp:21](F:/code/Paimbnails-main/Paimbnails-main/src/features/editor-animate/hooks/AnimateEditorHooks.cpp:21) |
| R03 | [build.yml:63](F:/code/Paimbnails-main/Paimbnails-main/.github/workflows/build.yml:63) |
| R04 | [QuickHubKeybind.cpp:18](F:/code/Paimbnails-main/Paimbnails-main/src/features/quick-hub/services/QuickHubKeybind.cpp:18); [QuickHubTouchHold.cpp:24](F:/code/Paimbnails-main/Paimbnails-main/src/features/quick-hub/services/QuickHubTouchHold.cpp:24) |
| R05 | [AudioExtractorNDK.cpp:19](F:/code/Paimbnails-main/Paimbnails-main/src/video/AudioExtractorNDK.cpp:19) |
| R06 | [LevelDataProvider.cpp:51](F:/code/Paimbnails-main/Paimbnails-main/src/features/auto-preview/services/LevelDataProvider.cpp:51) |
| R07 | [VersusSession.cpp:194](F:/code/Paimbnails-main/Paimbnails-main/src/features/versus/services/VersusSession.cpp:194) |
| R08 | [VersusNet.cpp:158](F:/code/Paimbnails-main/Paimbnails-main/src/features/versus/services/VersusNet.cpp:158) |
| R09 | [CollabVoice.cpp:57](F:/code/Paimbnails-main/Paimbnails-main/src/features/collab-editor/CollabVoice.cpp:57) |
| R10 | [LevelTagsClient.cpp:266](F:/code/Paimbnails-main/Paimbnails-main/src/features/foryou/services/LevelTagsClient.cpp:266) |
| R11 | [SearchObjectBuilder.hpp:66](F:/code/Paimbnails-main/Paimbnails-main/src/features/info-suite/services/SearchObjectBuilder.hpp:66) |
| R12 | [AdvancedSearchPopup.cpp:151](F:/code/Paimbnails-main/Paimbnails-main/src/features/info-suite/ui/AdvancedSearchPopup.cpp:151) |
| R13 | [PendingQueue.cpp:54](F:/code/Paimbnails-main/Paimbnails-main/src/features/moderation/services/PendingQueue.cpp:54) |
| R14 | [AnimateTypes.cpp:101](F:/code/Paimbnails-main/Paimbnails-main/src/features/editor-animate/AnimateTypes.cpp:101) |
| R15 | [FusionStore.cpp:104](F:/code/Paimbnails-main/Paimbnails-main/src/features/texture-studio/persist/FusionStore.cpp:104); [AdvancedSearch.cpp:170](F:/code/Paimbnails-main/Paimbnails-main/src/features/info-suite/services/AdvancedSearch.cpp:170) |
| R16 | [GIFDecoder.cpp:188](F:/code/Paimbnails-main/Paimbnails-main/src/utils/GIFDecoder.cpp:188) |
| R17 | [CommentBgSettingsPopup.cpp:448](F:/code/Paimbnails-main/Paimbnails-main/src/features/profiles/ui/CommentBgSettingsPopup.cpp:448) |
| R18 | [BetaUploadWarning.cpp:73](F:/code/Paimbnails-main/Paimbnails-main/src/utils/BetaUploadWarning.cpp:73) |

## Resultados finales

| Comprobación | Resultado |
|---|---|
| Inventario inicial | 1723 archivos de código/scripts/shaders; 381383 líneas. Dentro de `src`: 1453 archivos y 353386 líneas. |
| Análisis con flags Windows | 722/722 unidades C++; 0 errores de compilación o timeouts. |
| Archivos C++ modificados | 24/24 comprobados de nuevo con sus flags reales; evidencia posterior al último cambio de cada archivo. |
| Suite portable con ASan/UBSan | 56/56 PASS, incluidos 23 bancos C++ compilados y ejecutados. |
| Nuevas fixtures de comportamiento | 16/16 PASS con ASan/UBSan; funciones actuales extraídas de producción. |
| Bindings/SDK | 3/3 PASS. |
| Opcionales configurados | 5 PASS, 2 SKIP (servidor privado y EGL/GLES de Linux). |
| Overlay web | 133 combinaciones de estilo/layout, 11 animaciones, recursos oficiales, actualizaciones y privacidad aprobados con Edge/Playwright. |
| Contratos y sintaxis | 132 entradas de catálogo; 255 settings; sin discrepancias de tipos en los accesos literales propios ni nombres `$modify` duplicados. Scripts Python parseados; JavaScript verificado durante la auditoría. |
| Casos adicionales durante la auditoría | 4096 round-trips Base64; 10000 mutaciones/truncamientos GIF acotados a 1×1; 10000 vueltas del ring de video; bordes/NaN del filtro de scroll sin fallos de sanitizadores. |

La primera suite portable produjo un timeout de 60 s en guía; el banco aislado pasó 443/443 casos y 9/9 multi-tema. Se ajustó el límite de los bancos de guía a 180 s y la suite completa terminó sin fallos. Un intento de bindings contra la carpeta de resultados falló por ausencia de `CMakeCache.txt`; la repetición contra `build-clang` produjo los 3 PASS indicados. La primera suite opcional omitió dependencias sin configurar; al localizar las instalaciones existentes se obtuvieron los 5 PASS.

Tras los 56 PASS de la suite se completó la protección de importación de texturas de fusión y se repitió el banco ampliado de 16 fixtures y la compilación de los archivos afectados. La semilla final de identidad de Animate también se verificó de nuevo con su fixture y compilación. No se suman esas repeticiones como pruebas independientes.

La pasada del compilador conserva advertencias de padding, redondeo entero, dependencias, listeners deliberadamente permanentes y rutas hipotéticas de punteros. Se contrastaron las señales de mayor riesgo con la inicialización y las precondiciones reales. No se interpreta cada aviso como un bug ni la ausencia de avisos como aprobación funcional. Los diagnósticos completos permanecen en los artefactos.

Evidencia local:

- [Suite portable](../build-clang/exhaustive-review-2026-10-16/fixes/regression-results/portable.json).
- [16 fixtures actuales](../build-clang/exhaustive-review-2026-10-16/fixes/review-behavior/results.json).
- [Bindings](../build-clang/regression-results/bindings.json).
- [Opcionales](../build-clang/exhaustive-review-2026-10-16/optional-configured/regression-results/optional.json).
- [24 archivos modificados](../build-clang/exhaustive-review-2026-10-16/changed-compile-results.json).
- [722 unidades y sus logs](../build-clang/exhaustive-review-2026-10-16/clang-tidy-results.json).
- [Índice de diagnósticos](../build-clang/exhaustive-review-2026-10-16/diagnostics-index.json).
- [Hashes y alcance final](../build-clang/exhaustive-review-2026-10-16/final-verification.json).

Repetir con un build configurado y sus dependencias disponibles:

```powershell
python tests/run_regressions.py --suite portable --build-dir build-clang --sanitize
python tests/run_regressions.py --suite bindings --build-dir build-clang --sdk "$env:GEODE_SDK"
python tests/review_behavior_regression.py --build-dir build-clang
```
