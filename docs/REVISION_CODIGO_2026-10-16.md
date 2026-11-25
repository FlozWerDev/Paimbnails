# Revisión de código — 16 de octubre de 2026

> Estado posterior: los ocho hallazgos se abordaron en [las correcciones y su validación](CORRECCIONES_REVISION_2026-10-16.md). Este documento conserva la revisión original como referencia.

La prioridad es corregir la vida útil de las entradas de la caché, invalidar el audio pendiente al cerrar perfiles y proteger los archivos de proyectos durante el guardado. Hay dos problemas reproducidos de forma aislada: acceso a memoria liberada en el manifiesto de miniaturas y pérdida de filas en GIF entrelazados pequeños.

## Alcance y límites

- Inventario y búsqueda de patrones en los **1.450 archivos C/C++ de `src`**, con **353.200 líneas**: 721 `.cpp`, 725 `.hpp`, dos `.h` y dos `.mm`. El recuento incluye los dos encabezados de stb incorporados al proyecto; excluye las dependencias de `build` y el SDK externo.
- Análisis con **clang-tidy en 28 unidades C++**: HTTP, WebSocket, imágenes, vídeo, colaboración, cachés, actualizaciones, descargas, emotes, importación GIF, PauseLayer, PlayLayer, capturas y PaiDraw. Se reutilizaron los argumentos del build existente sin recompilar ni instalar el mod.
- Lectura manual de rutas de llamadas relacionadas con los hallazgos, persistencia de iconos y texturas, coordinación de audio, interpolación de frames, infraestructura de hilos, instaladores y overlay.
- Comprobación sintáctica de **22 archivos Python/JavaScript** de pruebas, herramientas y overlay: sin errores de sintaxis.
- Ejecución de pruebas existentes, cuatro regresiones C++ con **AddressSanitizer y UndefinedBehaviorSanitizer**, y dos reproducciones específicas.

Esto es una revisión estática global con análisis profundo de áreas seleccionadas. No supone haber comprobado manualmente cada línea ni todas las combinaciones de mods. No se ejecutó el mod en Geometry Dash, Android o macOS, ni se midieron FPS. Los sanitizadores se ejecutaron en programas independientes, no dentro del juego.

**No se modificó código de producción ni se reemplazó el `.geode` instalado durante esta revisión.** Las reproducciones, inventario y logs están en `build-clang/code-review/`.

## Hallazgos, por prioridad

### 1. P1 — Puntero a una entrada del manifiesto utilizado después de liberar el bloqueo

Ubicaciones: [DiskManifest.cpp:179](../src/features/thumbnails/services/DiskManifest.cpp#L179), [ThumbnailCache.cpp:415](../src/features/thumbnails/services/ThumbnailCache.cpp#L415), [ThumbnailLoader.cpp:861](../src/features/thumbnails/services/ThumbnailLoader.cpp#L861).

`DiskManifest::getEntry()` bloquea el mutex para buscar una entrada, pero devuelve un puntero al valor del `unordered_map`. El bloqueo termina al retornar. `ThumbnailCache::getDiskEntry()` copia ese valor **después** de que el bloqueo ya haya terminado. Otro hilo puede actualizar o eliminar la misma entrada en ese intervalo. Hay consumidores en los pools de disco/CPU y borrados al invalidar o limpiar la caché.

**Impacto:** lectura concurrente de objetos `std::string` modificados o destruidos; posible cierre inesperado al cargar miniaturas mientras se invalida o limpia la caché.

**Verificación:** `manifest_lifetime_repro.cpp` usa cinco métodos de producción copiados sin cambios y fuerza la eliminación entre la devolución del puntero y su copia. AddressSanitizer detecta **`heap-use-after-free`**. El resultado demuestra el intervalo de vida inseguro; no reproduce una sesión completa del juego.

**Corrección propuesta:** devolver `std::optional<DiskManifestEntry>` copiando dentro del bloqueo. Como alternativa inmediata, mantener `m_manifest.mutex` bloqueado durante la búsqueda y la copia, usando `getEntryLocked()`. Evitar exponer punteros internos fuera del bloqueo.

### 2. P2 — Audio de perfil pendiente puede comenzar después de cerrar el perfil

Ubicaciones: [ProfilePage.cpp:2610](../src/hooks/ProfilePage.cpp#L2610), [ProfileMusicManager.cpp:788](../src/features/profile-music/services/ProfileMusicManager.cpp#L788), [ProfileMusicManager.cpp:874](../src/features/profile-music/services/ProfileMusicManager.cpp#L874), [ProfileMusicManager.cpp:909](../src/features/profile-music/services/ProfileMusicManager.cpp#L909).

`cleanupProfileAudio()` solo ejecuta `forceStop()` si el gestor ya está reproduciendo, pausado o haciendo fade-out. Durante la consulta de configuración, descarga o extracción de audio todavía no tiene por qué cumplirse ninguna de esas condiciones. El cierre limpia el contexto del coordinador, pero deja `m_currentProfileID` con el ID solicitado. Los callbacks pendientes aceptan el resultado si ese ID coincide y pueden iniciar la música. `playAudioFile()` obtiene el token de sesión cuando llega el resultado, en lugar de verificar el token con el que comenzó la solicitud.

**Impacto:** al abrir un perfil sin audio cacheado y cerrarlo rápidamente, una respuesta tardía puede sustituir la música del menú u otro contexto. Comparar solo el account ID tampoco distingue dos aperturas sucesivas del mismo perfil.

**Verificación:** trazado estático del cierre y de las tres rutas asíncronas. Falta reproducirlo dentro del juego con una respuesta retrasada.

**Corrección propuesta:** invalidar siempre las solicitudes de reproducción de la sesión al cerrar, aunque todavía no haya audio activo. Capturar una generación de solicitud y el token original; verificar ambos antes de guardar metadatos de esa solicitud o reclamar el canal. No detener un canal perteneciente a otro contexto para cancelar una carga pendiente.

### 3. P2 — El guardado sobrescribe directamente los proyectos e índices

Ubicaciones: [IconProjectStore.cpp:221](../src/features/icon-maker/persist/IconProjectStore.cpp#L221), [IconProjectStore.cpp:93](../src/features/icon-maker/persist/IconProjectStore.cpp#L93), [SlotStore.cpp:213](../src/features/texture-studio/persist/SlotStore.cpp#L213), [SlotStore.cpp:101](../src/features/texture-studio/persist/SlotStore.cpp#L101).

Estos guardados usan `geode::utils::file::writeString()` sobre el archivo definitivo. Se comprobó en el SDK local que esa función escribe directamente: en Windows abre con `CREATE_ALWAYS` y en POSIX con `O_TRUNC`. El archivo anterior se trunca antes de que termine la escritura. Que la función devuelva error no recupera la versión anterior.

Además, `IconProjectStore::saveProject()` registra como advertencia un fallo al guardar el índice y termina devolviendo éxito. `SlotStore::saveSlot()` ya devuelve un error en ese caso, por lo que el comportamiento no es consistente.

**Impacto:** una escritura incompleta o una interrupción durante el guardado puede dejar ilegible un proyecto o su índice. Un error del índice de iconos puede pasar inadvertido para la interfaz.

**Verificación:** inspección de los puntos de guardado y de la implementación local del SDK. No se provocaron fallos de disco sobre datos del usuario.

**Corrección propuesta:** reutilizar [AtomicFileWrite.cpp](../src/utils/AtomicFileWrite.cpp), que ya crea temporales únicos y reemplaza el destino. Propagar los errores de persistencia y definir una recuperación si el proyecto se guarda pero el índice falla. El reemplazo atómico reduce este riesgo; por sí solo no garantiza durabilidad frente a cualquier fallo de alimentación.

### 4. P2 — GIF entrelazados de pocas filas se decodifican incompletos

Ubicación: [GIFDecoder.cpp:398](../src/utils/GIFDecoder.cpp#L398).

El salto a la siguiente pasada del entrelazado avanza `currentPass` una sola vez. En una imagen de altura 2, después de la fila 0 se selecciona la pasada que empieza en la fila 4. La siguiente iteración termina porque esa fila está fuera del frame; nunca llega a la pasada que empieza en la fila 1. El problema también afecta alturas 3 y 4, incluidas subimágenes pequeñas dentro de un GIF mayor.

**Impacto:** filas negras o incorrectas en GIF válidos.

**Verificación:** `gif_interlace_repro.cpp` incluye directamente el `GIFDecoder.cpp` de producción; solo sustituye el logger de Geode. La misma imagen válida de 1×2 píxeles rojos produce canales rojos `255, 255` sin entrelazado y `255, 0` con entrelazado. Se ejecutó con ASan y UBSan.

**Corrección propuesta:** saltar todas las pasadas cuyo inicio exceda la altura, o recorrer las cuatro pasadas explícitamente. Añadir fixtures pequeños con filas de distinto color, además de frames normales e interlazados.

### 5. P2 — El límite agregado de memoria de miniaturas se omite

Ubicaciones: [ThumbnailCache.cpp:158](../src/features/thumbnails/services/ThumbnailCache.cpp#L158), [ThumbnailCache.hpp:147](../src/features/thumbnails/services/ThumbnailCache.hpp#L147), [Settings.hpp:443](../src/core/Settings.hpp#L443).

`evictRamLocked()` retorna si la caché principal cabe en su límite individual **antes de calcular** la suma de miniaturas, URLs y GIF. Por ello no aplica `AGGREGATE_RAM_CAP` en ese caso. El límite individual es de 200 MiB en escritorio y 100 MiB en móvil, superior al agregado declarado de 150 y 80 MiB respectivamente. Por ejemplo, 160 MiB de miniaturas no fijadas en escritorio, con un número de entradas permitido, supera el agregado y aun así pasa el retorno temprano.

**Impacto:** el tope agregado declarado no limita realmente la memoria, incluso sin contar las otras dos cachés. La presión puede ser mayor en móvil.

**Verificación:** lectura de las condiciones y constantes; contraejemplo directo de sus valores. No se midió la memoria real dentro del juego ni se deduplicaron texturas compartidas entre cachés.

**Corrección propuesta:** calcular el presupuesto efectivo antes de decidir si se necesita expulsión. Mantener el presupuesto coherente al crecer las cachés de URLs/GIF y definir cómo se contabilizan recursos compartidos y niveles fijados.

### 6. P2 — Las regresiones no están integradas en la compilación o CI y varias están desactualizadas

Ubicaciones: [CMakeLists.txt](../CMakeLists.txt), [build.yml](../.github/workflows/build.yml), [geode_sdk_compat_regression.py:60](../tests/geode_sdk_compat_regression.py#L60), [audit_dynamic_transition_bindings.py:45](../tests/audit_dynamic_transition_bindings.py#L45), [emote_preload_regression.mjs:145](../tests/emote_preload_regression.mjs#L145), [main_level_prefetch_regression.mjs:164](../tests/main_level_prefetch_regression.mjs#L164).

No hay un objetivo de pruebas/CTest ni un paso que ejecute estas regresiones en el workflow de compilación. Compilar y generar el `.geode` puede pasar aunque las regresiones fallen.

Problemas reproducidos en esta copia:

- Las auditorías general del SDK y de transiciones esperan `build-win`, aunque el build activo es `build-clang`.
- La regresión de emotes intenta extraer `schedulePrefetchEmotes()`, que ya no existe en `PreloadActions.cpp`.
- La regresión de precarga compila al seleccionar Clang mediante `CXX`, pero después intenta ejecutar `regression` sin el sufijo `.exe` que genera el compilador en Windows.
- La prueba de rutas de subida requiere `cloudflare-server/paimon-thumbnails-server.js`, que no está en esta copia y está excluido del repositorio.
- La prueba visual del overlay requiere Playwright, no instalado en este entorno. Las pruebas GPU esperan bibliotecas EGL/GLES con nombres de Linux. Esto es una dependencia de plataforma, no evidencia de un bug gráfico.

**Corrección propuesta:** crear un punto de entrada común para las pruebas portables, detectar el build activo y separar comprobaciones de fuente, lógica C++ y GPU por plataforma. Incluir las pruebas portables en CI y declarar los requisitos de las demás. Evitar que una prueba dependa de extraer funciones por su texto exacto cuando puede probar una unidad de lógica reutilizable.

### 7. P3 — PaiDraw consulta un vector después de moverlo

Ubicaciones: [PaiDrawManager.cpp:725](../src/features/paidraw/PaiDrawManager.cpp#L725), [PaiDrawManager.cpp:745](../src/features/paidraw/PaiDrawManager.cpp#L745), [PaiDrawUI.cpp:2088](../src/features/paidraw/PaiDrawUI.cpp#L2088).

El snapshot mueve `strokes` a `m_state.recentStrokes` y después usa `strokes.size()` para decidir qué `StrokeEvent` publicar. Ese vector ya no contiene necesariamente los trazos; no es correcto depender de su contenido después del movimiento. clang-tidy lo señala tanto con `bugprone-use-after-move` como con el analizador de movimientos.

**Impacto acotado:** la publicación incremental no funciona como pretende el código. La interfaz actual también reconstruye todo el canvas al recibir los eventos de sala/ronda, lo que compensa visualmente esta ruta; no se afirma que todos los dibujos desaparezcan.

**Corrección propuesta:** determinar los trazos nuevos antes del movimiento y emitirlos fuera del mutex. Definir una sola estrategia de actualización del canvas para evitar reconstruirlo completo por duplicado y, a la vez, añadir de nuevo trazos mediante eventos.

### 8. P3 — La caducidad del disco solo se evalúa cuando se excede la cuota

Ubicación: [ThumbnailCache.cpp:441](../src/features/thumbnails/services/ThumbnailCache.cpp#L441).

`evictDiskIfNeeded(maxBytes, maxAge)` retorna si el total cabe en la cuota. La lógica de `DiskManifest::computePrune()` sí considera la edad, pero no se ejecuta en ese caso. El llamador pasa una edad de 30 días, que por tanto no actúa como caducidad independiente.

**Impacto:** archivos antiguos pueden conservarse indefinidamente mientras no se alcance el límite de tamaño. Es un problema si `maxAge` pretende ser una edad máxima; si la intención es limpiar por edad solo bajo presión, conviene explicitarlo en el contrato y el nombre.

**Corrección propuesta:** decidir y documentar esa política. Si la edad es un límite independiente, evaluar periódicamente las entradas vencidas aunque no se exceda la cuota, conservando las excepciones de niveles fijados.

## Mejoras de estructura y mantenimiento

1. **Dividir responsabilidades en las áreas que se modifiquen.** `GifPaintVectorizer.cpp` tiene 4.288 líneas, `LevelCell.cpp` 3.625, `LocalThumbnailViewPopup.cpp` 3.072 y `HttpClient.cpp` 2.916. Separar cálculo puro, transporte/persistencia y estado de UI permite probar comportamientos sin simular Geode completo. El tamaño orienta dónde revisar; no demuestra por sí solo un defecto.
2. **Introducir advertencias del compilador y análisis estático de forma gradual.** El proyecto no configura `/W4` ni `-Wall -Wextra -Wpedantic` para su propio código. Activarlos por objetivos o módulos, filtrando dependencias y bindings, daría visibilidad a código muerto y movimientos sospechosos sin convertir advertencias externas en bloqueos indiscriminados.
3. **Extender las soluciones que ya existen.** Usar `AtomicFileWrite` en persistencia crítica, generaciones para resultados asíncronos y el patrón de detección de build de `pause_layer_bindings_regression.py`. Evitar duplicar estas soluciones en cada popup.
4. **Respetar la propiedad de Cocos.** `geode::Ref`, `WeakRef`, autorelease y punteros prestados tienen funciones distintas. Sustituir indiscriminadamente nodos por `shared_ptr` no sería una mejora; conviene explicitar quién posee cada recurso y en qué hilo se destruye.

## Validación realizada

| Comprobación | Resultado |
| --- | --- |
| Capturas: `capture_operation_regression.cpp` | Pasa con ASan + UBSan |
| Navegación: `native_scene_navigation_regression.cpp` | Pasa con ASan + UBSan y stubs de pruebas |
| Movimiento de transiciones: `dynamic_transition_motion_regression.cpp` | Pasa con ASan + UBSan |
| Física del menú: `menu_physics_regression.cpp` | Pasa con ASan + UBSan y stubs de pruebas |
| Bindings de pausa | Pasa: 57 hooks en 19 archivos |
| Configuración de transiciones | Pasa |
| Live slots | Pasa el modo `--static-only`; GPU no comprobada |
| RTX | Pasa el modo `--static-only`; GPU no comprobada |
| Crash compatibility, teclado iOS y contexto Android de Quick Hub | Pasan sus comprobaciones de fuente en Node; no son ejecuciones en dispositivos |
| Sintaxis Python/JavaScript | Pasan 22 archivos |
| clang-tidy | 28 unidades analizadas sin errores de análisis; 31 advertencias brutas en el lote de 27 y ninguna en la prueba inicial de `RgbaScaler.cpp` |
| Reproducción de vida útil del manifiesto | Confirma `heap-use-after-free` con ASan; salida de fallo esperada |
| Reproducción de GIF entrelazado | Confirma una fila incorrecta; salida de fallo esperada |

Los diagnósticos del analizador se revisaron antes de convertirlos en hallazgos. Se descartó como no alcanzable la división por cero señalada en `GifImportPipeline.cpp:1641`: el tamaño de bloque de esa lambda solo se invoca con 2, 3 o 4. Tampoco se clasificaron como bugs del mod los avisos de padding, los bitmasks del STL ni los listeners del SDK retenidos intencionalmente. El aviso de `ThumbnailCache.cpp:501` consulta miembros escalares tras mover una estructura y no se equiparó al problema de PaiDraw.

Orden recomendado de corrección: **vida útil del manifiesto → cancelación de audio pendiente → guardado de proyectos → GIF entrelazados → presupuesto de memoria → pruebas/CI → limpieza de las rutas restantes**. Cada cambio debería llevar una comprobación de su comportamiento y una prueba manual de integración cuando dependa del motor.
