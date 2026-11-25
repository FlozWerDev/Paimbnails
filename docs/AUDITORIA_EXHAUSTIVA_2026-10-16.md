# Auditoría ampliada de código — 16 de octubre de 2026

**Estado:** revisión de código completada y 18 hallazgos corregidos; validación de componentes aprobada, integración en el juego pendiente. Este informe conserva la descripción de los defectos encontrados antes de corregirlos. Las correcciones y sus pruebas se documentan en [CORRECCIONES_AUDITORIA_2026-10-16.md](CORRECCIONES_AUDITORIA_2026-10-16.md). Amplía la revisión anterior y no sustituye sus correcciones.

## Alcance y método

Se revisa la copia local, sin repositorio `.git`, sin ejecutar Geometry Dash, sin compilar/instalar el mod y sin enviar solicitudes a sus servidores. Se conservan los informes anteriores. Inicialmente se generaron documentación y bancos aislados; después, el usuario pidió corregir los defectos y se modificaron las rutas de producción indicadas en el informe de correcciones. La pasada completa comenzó antes de los cambios y continuó durante ellos; los archivos modificados se verifican de nuevo por separado con los flags del build.

El trabajo combina inventario de todos los archivos propios, búsqueda de patrones, análisis del compilador de las 722 unidades C++ del build Windows, trazado de controles del catálogo de 132 módulos, lectura manual de rutas de riesgo y pruebas aisladas con ASan/UBSan. El análisis Windows no ejecuta ni valida las ramas exclusivas de Android/Apple. Las ramas revisadas manualmente y los dobles de sus APIs se identifican expresamente.

Esto **no equivale a probar manualmente todas las pantallas, cada línea o cada combinación de mods**. La [matriz por función](AUDITORIA_FUNCIONES_2026-10-16.md) distingue trazabilidad del control, evidencia de componentes y pruebas de integración pendientes.

Las reproducciones extraen métodos de producción sin modificarlos y simulan sus dependencias externas. `extracted-methods.json` registra el archivo, línea y hash de cada método. Los resultados demuestran los casos descritos dentro de esos límites; no se presentan como sesiones del juego. Los programas de evidencia terminan correctamente cuando confirman un defecto, por lo que **no son regresiones aprobadas del comportamiento deseado**.

## Hallazgos

### R01 · P1 · Global Icons permite salir de la caché y limpiar archivos ajenos

**Ubicación:** `GlobalIconClient.cpp`, `parseMetaJson`; `GlobalIconStorage.cpp`, `resolveFiles`, `fetchSlot`, `pruneSlotDir`, `ensureIcon` y `ensureIcons`.

Los metadatos remotos se copian directamente a `pngFile`/`plistFile`. `resolveFiles()` concatena esos campos con el directorio de caché sin restringirlos a un nombre de archivo. Un `pngFile` como `../../../victim/downloaded.png` escapa de `global_icons_cache/<cuenta>/cube/`. Si falta, la descarga crea/escribe el destino; después, `pruneSlotDir(files.png.parent_path(), files)` elimina los demás archivos regulares del directorio elegido.

**Impacto:** una respuesta de metadatos con una ruta manipulada puede escribir fuera de la caché y borrar otros archivos de una carpeta accesible al proceso. La validación de URL de descarga no valida el destino local.

**Evidencia:** `global_icon_path_escape_and_prune` usa los métodos reales de resolución y limpieza. Dentro de una carpeta nueva y aislada de la auditoría, conserva el archivo ficticio descargado y elimina otro proyecto ficticio del directorio que queda fuera de la caché. No se tocaron archivos del usuario. El servidor no está incluido: no se afirma que hoy acepte o produzca ese metadato; el cliente carece de la comprobación necesaria.

**Corrección:** derivar nombres locales propios; rechazar rutas absolutas, componentes `.`/`..`, separadores y nombres especiales; comprobar que los destinos resueltos permanezcan dentro del directorio autorizado. La limpieza debe recibir el directorio de caché validado, nunca un padre obtenido de un nombre remoto.

### R02 · P1 · Paimon Animate comparte documentos entre niveles del mismo nombre

**Ubicación:** `AnimateStore.cpp`, `levelKey`, `pathFor`, `loadDocument`, `saveDocument`; `AnimateSession.cpp`, `attach` y `persist`.

`levelKey()` usa exclusivamente el nombre y un hash del mismo nombre. Dos niveles distintos llamados `Demo` obtienen exactamente el mismo archivo de animaciones aunque tengan IDs diferentes. Renombrar un nivel cambia la clave, sin migración del documento. `saveDocument()` incluso elimina ese archivo compartido cuando el documento no contiene clips.

**Impacto:** clips y grupos de un nivel se cargan sobre otro, el último guardado sustituye al anterior y un cambio de nombre hace desaparecer las animaciones de la vista. Los triggers generados pueden apuntar a grupos de otro nivel.

**Evidencia:** `animate_level_identity` ejecuta `levelKey()` extraído sin cambios: IDs 100 y 200 con el mismo nombre producen la misma clave; renombrar el primero cambia su clave.

**Corrección:** identidad persistente por nivel, incluida una identidad local para niveles sin ID publicado. Definir migración y política explícita de duplicación y renombrado.

### R03 · P1 · La CI ejecuta pruebas que necesitan dependencias antes de obtenerlas

**Ubicación:** `.github/workflows/build.yml`, paso `Run portable regressions`; `tests/run_regressions.py`, `fmt_include_path`, `rapidfuzz_include_path` y compilación de los bancos de Animate/guía.

El workflow ejecuta las regresiones inmediatamente después del checkout y antes de `Build the mod`. El ejecutor busca fmt y RapidFuzz en cachés de builds ya configurados, pero no los descarga ni los declara como requisitos previos. Un checkout limpio no contiene esas dependencias. El build/action que las obtiene todavía no se ha ejecutado.

**Impacto:** la CI puede detenerse antes de compilar o empaquetar el mod. Que las pruebas pasen en esta copia con cachés precargadas no cubre un runner limpio.

**Evidencia:** usando los mismos archivos y opciones de stubs, sin las inclusiones de caché, `editor_animate_regression.cpp` falla con `fmt/format.h file not found` y `guide/harness.cpp` con `rapidfuzz/fuzz.hpp file not found`. Esto reproduce la dependencia faltante; no se lanzó una ejecución remota de GitHub Actions.

**Corrección:** preparar dependencias explícitamente antes de las pruebas, o configurar un build sin instalar el mod y pasar sus rutas al ejecutor. Probar el flujo desde un directorio limpio y sin cachés heredadas.

### R04 · P2 · Quick Hub conserva punteros a una barra destruida con su escena

**Ubicación:** `QuickHubKeybind.cpp`, `HoldState`, `createProgressBar`, `updateProgressBar`, `cleanupProgressBar` y `QuickHubScheduler::onUpdate`.

El estado global almacena `CCNode*` de la barra y su relleno, que pertenecen a la escena. El scheduler vive durante toda la sesión. En escritorio no se guarda ni comprueba la escena que inició el gesto; una transición por ratón u otra acción puede destruirla mientras Ctrl sigue pulsado. El siguiente tick escribe en `progressFill`, o la liberación de Ctrl usa `progressBar`, sin validar su vida útil. No se encontró otra llamada de cancelación durante el cambio de escena.

**Impacto:** posible acceso a memoria liberada al cambiar de escena durante el intervalo de apertura del radial.

**Evidencia:** trazado de propiedad y de todos los llamadores de `abortActiveHold`; `quick_hub_scene_bar_lifetime` ejecuta `updateProgressBar()` sin cambios después de destruir el nodo simulado de la escena y ASan detecta `heap-use-after-free`. El doble demuestra el uso del puntero colgante; falta reproducir el orden completo de transición en Cocos.

**Corrección:** usar referencias débiles para ambos nodos, vincular el gesto a la escena de origen y cancelarlo al salir/cambiar de escena antes de destruir la barra.

### R05 · P2 · Un error permanente del decodificador Android no termina la extracción de audio

**Ubicación:** `AudioExtractorNDK.cpp`, bucle de `extractAudioToPcm`; `ThreadTracker.hpp` y `RuntimeLifecycle.cpp`, espera de hilos al salir.

El bucle solo termina cuando recibe un buffer con EOS. Los códigos negativos distintos de `AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED`, incluidos errores permanentes, se tratan como un reintento. No hay límite de tiempo, de reintentos ni cancelación por cierre. El mutex de extracción permanece tomado durante toda la operación.

**Impacto:** un fallo persistente de MediaCodec puede dejar el trabajador bloqueado indefinidamente, impedir otras extracciones y bloquear la salida si ese trabajador se espera mediante `join()`.

**Evidencia:** `android_audio_permanent_error_loop` ejecuta la función real con dobles NDK que devuelven un error permanente. La función vuelve a consultar 16 veces; solamente un guard externo del banco la detiene en la llamada 17. No se ejecutó en un dispositivo Android.

**Corrección:** distinguir los estados transitorios de los errores, abandonar con limpieza de recursos y añadir deadline/cancelación para la extracción completa.

### R06 · P2 · Auto Preview acepta el nivel de una solicitud anterior

**Ubicación:** `LevelDataProvider.cpp`, `request`, `levelDownloadFinished`, `finish`; `AutoPreviewQueue.cpp`, callback de descarga y `generateFor`.

El token solo protege el temporizador. `levelDownloadFinished()` no compara el ID descargado con `m_levelID`. Si A vence, se pide B y luego llega la respuesta de A, `finish()` entrega A al callback de B. Ese callback renderiza el nivel recibido y lo guarda bajo `chosenId`, que sigue siendo B.

**Impacto:** miniatura automática del nivel equivocado y cancelación prematura de la solicitud correcta.

**Evidencia:** `auto_preview_late_response`: pedir 111, vencer su temporizador, pedir 222 y entregar 111. El callback de 222 recibe 111 y el proveedor deja de estar ocupado.

**Corrección:** comprobar el ID antes de aceptar el resultado y coordinar el delegado compartido para que las respuestas de solicitudes vencidas no satisfagan otra solicitud.

### R07 · P2 · Los callbacks HTTP de Versus sobreviven a cancelar o sustituir una sesión

**Ubicación:** `VersusSession.cpp`, `poll`, `watchTick`, `beginQueue`, `accept`, `ban` y `enterLevel`.

Los temporizadores comparan generaciones, pero varios callbacks de red solo capturan `this`. Cuando una petición ya salió, `reset()`/`cancelQueue()` no impiden que su resultado ejecute `applyLobby()`, reprograme el polling o cambie la fase de una sesión posterior. `endWatch()` tampoco invalida el callback HTTP que ya está en curso.

**Impacto:** un duelo cancelado puede reaparecer, una respuesta vieja puede sustituir el lobby actual y una petición rechazada anterior puede resetear una búsqueda nueva.

**Evidencia:** `versus_late_poll` ejecuta `poll()` sin cambios con el transporte diferido. Tras poner la fase en `Idle` e invalidar la generación como hace el cierre, la respuesta anterior sigue llamando a `applyLobby()`.

**Corrección:** generación de sesión y match ID capturados al iniciar cada petición; comprobarlos dentro de todos los callbacks, además de los timers. Invalidar también las peticiones del watcher al cerrarlo.

### R08 · P2 · El mensaje Revive de Versus se envía pero se rechaza al recibirlo

**Ubicación:** `VersusNet.hpp`, `StateKind`; `VersusNet.cpp`, `VsState::decode`; `VersusSession.cpp`, carta Heart y manejador de `StateKind::Revive`.

`Revive` vale 8 y aparece después de `Spent`, que vale 7. El decoder rechaza cualquier valor mayor que `Spent`. La carta Heart envía `Revive`, pero ese paquete nunca alcanza su manejador.

**Impacto:** el rival conserva `spent=true` después de que Heart devuelve una vida al jugador; el resultado de formatos con límite de intentos puede evaluarse con un estado incorrecto.

**Evidencia:** `versus_revive_wire` ejecuta el struct de codec real: `Spent` pasa y el paquete emitido para `Revive` falla su propio decode.

**Corrección:** validar todos los valores vigentes del protocolo y añadir round-trip para cada variante, incluyendo clientes anteriores.

### R09 · P2 · El volumen cero de voz colaborativa se convierte en volumen completo

**Ubicación:** `CollabVoice.cpp`, `voiceVolume`, creación del speaker y `onRemoteFrame`; `mod.json`, `collab-voice-volume`.

El ajuste permite 0, pero `voiceVolume()` convierte cualquier valor `<= 0` en 1. El valor se aplica al canal tanto al crearlo como al recibir audio.

**Impacto:** bajar el volumen de voces al mínimo no silencia; reproduce al 100 %.

**Evidencia:** `collab_voice_zero_volume` ejecuta la función real: 0,5 produce 0,5; 0 produce 1.

**Corrección:** respetar el cero y reservar el fallback para valores ausentes/no finitos o fuera de contrato.

### R10 · P2 · Un error de red se persiste como ausencia de etiquetas en For You

**Ubicación:** `LevelTagsClient.cpp`, `fetchTags`, `saveDiskCache` y `loadDiskCache`.

Aunque HTTP falle o el JSON sea inválido, el callback recorre el chunk y añade los IDs omitidos como vectores vacíos. Los marca como dirty y guarda la caché. La siguiente consulta encuentra esos IDs y no vuelve a pedirlos. La caché de disco admite snapshots de hasta 30 días.

**Impacto:** una caída temporal de red puede hacer que las recomendaciones ignoren etiquetas durante el resto de la sesión y tras reiniciar.

**Evidencia:** `level_tags_failed_request_cache` entrega un HTTP fallido al método real. El ID queda en caché como vacío y la segunda llamada no realiza ninguna petición.

**Corrección:** cachear vacíos solo tras una respuesta válida que confirme ausencia; mantener los errores como desconocidos con reintento/backoff. La expiración por entrada evita extender datos antiguos al regrabar otros IDs.

### R11 · P2 · Búsquedas distintas comparten clave de página y refinado

**Ubicación:** `SearchObjectBuilder.hpp`, `searchKey`; `LevelBrowserPaging.cpp`; `AdvancedSearch.cpp`, `armRefine`/`refineFor`; `LevelSearchExtras.cpp`.

La clave omite `m_legendaryFilter`, `m_mythicFilter`, `m_customSongFilter` y `m_searchMode`, aunque se asignan al objeto de búsqueda. Por ello consultas diferentes comparten página recordada y pueden coincidir con un refinado local que pertenece a otra consulta.

**Impacto:** abrir una búsqueda puede saltar a una página de otra o filtrar sus resultados con restricciones ajenas.

**Evidencia:** `search_identity` cambia cada uno de esos cuatro campos y comprueba que `searchKey()` sigue devolviendo el mismo valor.

**Corrección:** incluir todos los campos que afectan la consulta en una serialización inequívoca, excluyendo únicamente la página.

### R12 · P2 · El interruptor Search Presets no se consulta

**Ubicación:** `ModuleCatalog.cpp`, `paimbnails.searchpresets.info`; `AdvancedSearchPopup.cpp`, construcción de botones, `onPresets` y `onSavePreset`; `SearchPresetsPopup.cpp`.

La clave `info-mod-search-presets` existe en el catálogo/manifiesto, pero no aparece en ninguna ruta de ejecución fuera del catálogo. Búsqueda avanzada crea los botones Presets y Guardar y abre sus diálogos incondicionalmente.

**Impacto:** desactivar Search Presets no desactiva la función mientras la búsqueda avanzada siga activa.

**Evidencia:** inventario de referencias de los 132 módulos y lectura de la ruta completa de botones y callbacks. El test del registro solo valida el valor almacenado, no que la función lo respete.

**Corrección:** usar `moduleEnabled("info-mod-search-presets")` para la disponibilidad de los controles y comprobarlo al ejecutar la acción.

### R13 · P2 · Notas con saltos de línea invalidan el JSON de moderación

**Ubicación:** `PendingQueue.cpp`, `escape`, `toJson` y `load`; `ModerationService.cpp`, `submitReport`/rechazo.

El escape manual solo protege comillas y barras invertidas. Inserta literalmente saltos de línea, tabulaciones y demás caracteres de control dentro de strings JSON. `load()` ignora el archivo completo si el parse falla.

**Impacto:** un reporte o motivo con esos caracteres puede hacer ilegible toda la cola local al reiniciar. No implica pérdida de los reportes que ya se hayan guardado en el servidor.

**Evidencia:** `pending_queue_json_control_chars` ejecuta `toJson()` y sus helpers reales sobre una nota con newline/tab. El parser JSON estándar rechaza el resultado con `Invalid control character`.

**Corrección:** construir el documento con matjson y su serializer; no mantener un escape JSON parcial.

### R14 · P2 · Importar un rango inválido grande puede congelar Paimon Animate

**Ubicación:** `AnimateTypes.cpp`, `parseGroupInput`; `AnimatePopups.cpp`, `importWithPrompt`.

El bucle recorre todos los enteros del rango. El límite de 512 solo cuenta IDs aceptados; los IDs fuera de 1–9999 no lo consumen. Una entrada permitida por el diálogo como `1000000000-2000000000` recorre mil millones de valores inútiles en el hilo de interfaz.

**Impacto:** congelamiento prolongado al confirmar una entrada inválida; el límite de caracteres del input no lo evita.

**Evidencia:** `animate_large_invalid_range` comprueba primero un rango normal y luego llama a la función real con el rango indicado. El banco termina externamente a los 3 segundos; el código permite demostrar el número de iteraciones sin esperar a que finalice.

**Corrección:** intersectar los extremos con el intervalo de grupos válido, rechazar rangos completamente externos y limitar el trabajo independientemente del número de resultados aceptados.

### R15 · P2 · Persistencia de datos propios todavía sobrescribe archivos definitivos

**Ubicación:** `ManualOverrideStore.cpp` y `FusionStore.cpp`, `save`; `OfficialSlotStore.cpp`, `save`; `AdvancedSearch.cpp`, `savePresets`; `ProgressTracker.cpp`, `save`.

Los JSON de proyectos y ciertos índices ya usan `AtomicFileWrite`, pero las máscaras editadas/fusiones se guardan con `writeBinary(path, ...)` directamente. También se truncan los archivos definitivos de presets y progreso, y la lista de Official Slots usa `writeString()` directo. Una interrupción o escritura parcial destruye el contenido previo antes de que el guardado termine. Parte de estas APIs no devuelve el fallo a la UI.

**Impacto:** pérdida de ediciones manuales, fusiones, lista de slots, presets o estadísticas ante un fallo de guardado. La corrección anterior de `project.json` no protege los archivos de máscaras de los que depende.

**Evidencia:** trazado estático de escrituras y lectores, más la regresión existente de reemplazo atómico. No se provocó un fallo de disco sobre datos reales ni se afirma haber reproducido un corte de energía.

**Corrección:** temporales únicos y reemplazo atómico para los datos irrecuperables, propagación del resultado y recuperación de índices. En operaciones de varios archivos, definir un commit/versionado coherente; atomicidad de cada archivo no hace atómico todo el conjunto.

### R16 · P2 · El decodificador GIF conserva el control del fotograma anterior

**Ubicación:** `src/utils/GIFDecoder.cpp`, `decode`, estado `currentDelay`, `hasTransparency`, `transparentIndex` y `disposalMethod`.

Los campos del bloque Graphic Control Extension (GCE) se inicializan antes del bucle de imágenes y se actualizan al encontrar otro GCE. Tras decodificar una imagen no se restablecen. Si la siguiente imagen no lleva su propio GCE, hereda transparencia, demora y disposal de la anterior.

**Impacto:** un GIF válido puede mostrar píxeles transparentes donde debe mostrar color, repetir pausas y aplicar una eliminación de fotograma ajena. Afecta a las funciones que consumen este decodificador, incluida la importación GIF del editor.

**Evidencia:** `media_edge_checks.cpp` compila el archivo de producción completo con ASan/UBSan. Genera un GIF de dos imágenes de 1×1: la primera declara el índice 1 transparente y 500 ms; la segunda usa ese índice sin GCE. El mod produce alfa 0 y 500 ms en la segunda. Pillow, leyendo exactamente el mismo archivo `gce-scope.gif`, produce alfa 255 y ninguna duración declarada. El resultado y el archivo quedan conservados para repetir la comparación. Esta reproducción confirma transparencia y demora; el arrastre de disposal se observa en el código, sin una reproducción visual adicional.

**Corrección:** restablecer el estado de control por imagen después de consumirlo, conservando por separado los datos de disposal necesarios para componer la imagen siguiente. Añadir fixtures de imágenes sin GCE entre otras con GCE.

### R17 · P2 · El fondo de comentarios accede al popup antes de comprobar su vida

**Ubicación:** `src/features/profiles/ui/CommentBgSettingsPopup.cpp`, `fetchThumbnailsForLevel`.

El callback captura un `WeakRef` y también `this`, pero lee `m_thumbRequestToken` antes de ejecutar `weakSelf.lock()`. Cerrar el popup durante la petición puede liberar el objeto antes de que llegue la respuesta. Tener un WeakRef no protege una lectura que ocurre antes de consultarlo.

**Impacto:** acceso a memoria liberada y posible cierre del juego al recibir la lista de miniaturas después de cerrar la configuración del fondo de comentarios.

**Evidencia y corrección:** lectura de la cadena `ThumbnailAPI`→`ThumbnailTransportClient` y del destructor del popup, más `comment_popup_lifetime`, que ejecuta el callback de producción después de liberar el popup bajo ASan. Ahora primero se bloquea el WeakRef y todas las lecturas pasan por la referencia obtenida; también se descartan respuestas con token anterior.

### R18 · P3 · El fallback del aviso beta pierde la acción pendiente

**Ubicación:** `src/utils/BetaUploadWarning.cpp`, `showBetaUploadWarningIfNeeded`.

Se movía `onProceed` hacia `BetaUploadWarningPopup::create()` y se intentaba reutilizar si la inicialización del popup fallaba. El callback ya había sido consumido, de modo que la operación no continuaba en ese camino de recuperación.

**Impacto:** la acción de subida puede no ejecutarse si falla la creación del aviso. No se afirma que ese fallo de inicialización sea frecuente.

**Evidencia y corrección:** `beta_warning_fallback` modela una inicialización fallida que consume el parámetro, una creación correcta y el aviso previamente descartado. Pasar una copia conserva la acción del fallback y cada camino la ejecuta una sola vez.

## Verificación y alcance pendiente

La pasada completa analizó 722/722 unidades C++ sin errores de compilación ni timeouts; se verificaron de nuevo los 24 C++ modificados. La suite portable final dio 56/56 PASS; el banco ampliado dio 16/16 y los bindings 3/3. Los hashes y el alcance exacto están en `final-verification.json`. La primera ejecución opcional omitió assets y navegador por falta de configuración; se localizaron los recursos oficiales, Playwright incluido con el runtime y Edge, y se repitió: **5 PASS, 2 SKIP**, sin errores. Los SKIP restantes corresponden al servidor privado ausente y al renderer EGL/GLES de Linux. El banco del overlay cubrió 133 combinaciones de estilo/layout, 11 animaciones, actualizaciones, privacidad, escape de texto y carga de recursos oficiales; no ejecuta el servidor C++ dentro de Geode.

Cuando vuelva a estar disponible el juego, priorizar:

1. Cambios de escena mientras se mantiene Ctrl y se muestra la barra de Quick Hub.
2. Cancelar/reabrir colas y lobbies con respuestas de red retrasadas; Heart tras agotar intentos.
3. Dos niveles locales del mismo nombre, duplicación y renombrado con animaciones guardadas.
4. Miniaturas sin caché, timeout, cambio de pantalla y llegada tardía del nivel anterior.
5. Voz a volumen 0 y reconexión; error persistente de extracción en Android y cierre.
6. Guardado/reapertura de máscaras, fusiones, presets, progreso y slots; error de escritura visible.

No se considera ninguna pantalla ni plataforma aprobada por el mero hecho de que el análisis estático no reporte avisos.
