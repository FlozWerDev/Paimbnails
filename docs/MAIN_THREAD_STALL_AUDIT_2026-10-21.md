# Bloqueos periódicos e hilo principal — 21/10/2026

## Hallazgo que coincide con los 10 segundos

`ThumbnailLoader::processDownloadedData` guarda el índice de miniaturas como
máximo una vez cada **10 segundos** mientras llegan descargas. Lo ejecuta en el
pool de disco, pero `DiskManifest::flush` retenía `mutex` durante la construcción
del JSON y la escritura atómica del archivo. Ese mismo mutex se necesita para
`contains`, `getEntry`, `upsert` y otras consultas usadas por la interfaz.

Por tanto, pasar la escritura a un worker no evitaba el bloqueo: un frame podía
quedar esperando todo lo que tardase el disco o la serialización del índice.
Es una ruta compatible con el intervalo descrito, no una medición del juego.

Ahora el worker copia el estado bajo un bloqueo breve y hace la serialización
y la escritura **sin el mutex de los lectores**. Otro mutex, exclusivo de los
publicadores, conserva el orden de las escrituras. Las modificaciones que
llegan durante un guardado siguen pendientes; un fallo de escritura tampoco
las descarta. Se retiraron bloqueos externos redundantes de la carga/migración
para mantener el orden de adquisición de estos mutex.

Además, el evento `DataSaved` ya no ejecuta un flush de disco directamente en
el hilo principal: programa uno en segundo plano y agrupa solicitudes repetidas.
El guardado final al cerrar el juego sigue siendo síncrono para no perder datos.

## Segundo hallazgo: caché de blur a resolución física 4x

La instalación actual sí estaba cargada: el log
`Geode 2026-10-21 18.26.49.log` carga `flozwer.paimbnails2 v1.2.0` desde
`C:\Program Files (x86)\Steam\...\geode\mods`, y el DLL descomprimido coincide
byte a byte con `build-clang/flozwer.paimbnails2.dll`. El tirón restante no era
una instalación vieja.

Al revisar la caché de blur se encontró que el juego corre con un
`contentScaleFactor` de **4**. `BlurDiskCache` guardaba el tile a resolución
física completa: un `levelinfo` de 569x320 lógico ocupaba **11.653.140 bytes** y
un `levelcell` de 512x256 ocupaba **8.388.628 bytes**. Además,
`CCRenderTexture::newCCImage` asignaba dos buffers de ese tamaño, hacía
`glReadPixels` y después se copiaba otro buffer RGBA en el hilo principal. En
una apertura de nivel eso son decenas de MB de asignaciones, copias y una
sincronización GPU en main.

La caché RAM de blurs también estaba limitada por número de entradas: 192
texturas a ~11,6 MB podían retener más de **2 GB de VRAM**, con el consiguiente
riesgo de paginación del driver y tirones al recorrer niveles.

Cambios:

- La persistencia baja el tile a **1x resolución de punto** antes de leerlo:
  `levelinfo` pasa a ~0,73 MB y `levelcell` a ~0,52 MB. El sprite conserva el
  tamaño lógico porque se escala al renderizar y al cargar.
- `VERSION` sube a 2. Los archivos viejos (los 267 MB actuales) se descartan al
  arrancar; se regeneran a 1x. El primer arranque puede tardar algo más mientras
  se borran.
- La caché RAM ahora tiene un presupuesto de bytes (256 MB en escritorio,
  64 MB en móvil) además del límite de entradas.
- Se registran avisos si un readback o una subida de blur en main supera 40 ms.
- Se añadió un watchdog de diagnóstico: `[Perf] main-thread frame gap ... ms`
  para huecos de frame de 120 ms o más y `[Perf] gameDidSave took ... ms` para
  guardados lentos. Solo registra; no altera el juego.

## Otras esperas corregidas

| Ruta | Antes | Ahora |
| --- | --- | --- |
| `WebSocketClient::send` | WinHTTP síncrono en el llamador, incluidos los keepalives del feed | FIFO acotada a 128 mensajes / 512 KiB; envíos desde un worker |
| Cierre y reconexión de WebSocket | Cerraba handles y hacía `join()` en el llamador | Invalida callbacks inmediatamente; los workers conservan el estado y cierran la conexión |
| Callbacks de WebSocket | Podían tocar un propietario desde el lector mientras se destruía | Entrega en main con cancelación por conexión; no se entrega trabajo antiguo después de `disconnect()` |
| `ThreadTracker::spawn/cleanup` | Una marca de finalización podía provocar un `join()` mientras los captures aún se destruían | Destruye captures antes de publicar la marca; en Windows comprueba también la terminación real del hilo sin esperar |
| Cierre global | Los lectores podían seguir esperando cuando se unían los hilos | Se cancelan todos los WebSocket antes de `ThreadTracker::shutdown()` |

No se desactivaron avisos, Discord, música, miniaturas ni efectos para ocultar
el problema. Cocos/GL y las operaciones de interfaz permanecen en main.

## Alcance de la revisión

Se rastrearon en `src/` temporizadores, tareas diferidas, tickers globales,
`sleep/wait/join`, guardados, red síncrona y mutex compartidos con los workers.
La revisión manual priorizó arranque/cierre, cachés de miniaturas, Discord,
avisos/feed, música, fondos/vídeos, notificaciones, cursor, mascota, barra de
progreso, volumen, transiciones y previews automáticos.

Los HTTP de 10 segundos son timeouts de peticiones asíncronas, no un temporizador
que deba dormir el hilo principal. El TTL de 10 segundos de los vídeos tampoco
es un tick global de destrucción. Las correcciones anteriores de Discord IPC y
escaneo de canciones ya estaban presentes y se conservaron.

Esto no certifica que cada función del mod sea libre de trabajo pesado. Siguen
existiendo rutas costosas bajo petición, como la generación experimental de
previews de niveles y la importación/captura multimedia.

La poda del índice también se ajustó: bajo el mutex solo se validan las entradas
y se renombran los archivos a nombres de cuarentena `.tmp`; el borrado real
ocurre después de soltar el mutex. Si una descarga reemplazó la misma entrada
entre el cálculo y la poda, la comprobación de clave, tamaño y último acceso
descarta el plan antiguo, de modo que nunca se borra una miniatura nueva.

## Pruebas añadidas

Las fixtures extraen el código de producción actual y usan ASan/UBSan:

- `disk_manifest_nonblocking`: retiene el disco bloqueado y exige que consultas
  y mutaciones del índice terminen antes de liberarlo. Comprueba el snapshot,
  la conservación de una edición concurrente, el reintento de una escritura
  fallida y que la poda elimine archivos obsoletos sin borrar una descarga que
  reemplazó la misma entrada después de calcularse el plan.
- `thumbnail_index_background`: main no escribe, las solicitudes se agrupan,
  se recupera el servicio tras rechazar un worker y se conserva el flush final.
- `thread_tracker_nonblocking`: retiene destructores de captures y, en Windows,
  destructores TLS. `cleanup()` debe volver antes de desbloquearlos.
- `websocket_nonblocking`: retiene un envío y comprueba que encolar, desconectar
  y reconectar no esperan a la red; comprueba límites, FIFO, callbacks en main,
  cancelación de entregas antiguas, cierre remoto y cancelación de un handshake
  que aún no publicó su handle.

Comando focalizado (Python 3 y Clang++ en PATH):

```powershell
python tests/review_behavior_regression.py --build-dir build-clang `
  --case disk_manifest_nonblocking --case thumbnail_index_background `
  --case thread_tracker_nonblocking --case websocket_nonblocking
```

## Validación y límites

- Las cuatro regresiones nuevas pasaron con ASan/UBSan.
- Suite portable: **72 controles, 0 fallos**. Incluye **51 fixtures de
  comportamiento con ASan/UBSan, 0 fallos**. El WebSocket también pasó 30
  ejecuciones consecutivas con el envío y el handshake retenidos por la prueba.
- Suite de bindings/compatibilidad del SDK: **3 controles, 0 fallos**.
- Los seis `.cpp` afectados o que integran los cambios pasaron la comprobación
  de compilación `/Zs` con los argumentos del build real y el SDK Geode 5.11.2:
  `Bootstrap`, `RuntimeLifecycle`, `DiskManifest`, `ThumbnailCache`,
  `ThumbFeedSocket` y `WebSocketClient`.
- La configuración del paquete completo se desbloqueó al corregir el perfil de
  Geode a la instalación real de Steam. El CLI sigue avisando que la versión
  instalada `0.4.2-alpha` de `kampwski.level_tags` no cumple
  `>=0.4.0-alpha`: usa las reglas de prerelease de Rust semver, que solo aceptan
  una prerelease si el comparador tiene la misma terna `major.minor.patch`. No
  se alteró `mod.json`; el CLI resolvió la dependencia desde el índice y la
  configuración terminó correctamente.
- `cmake --build build-clang --target Paimbnails_PACKAGE` completó el paquete
  con Clang/Ninja y lo copió automáticamente a
  `C:/Program Files (x86)/Steam/steamapps/common/Geometry Dash/geode/mods`.
  El `.geode` del build y el desplegado coinciden byte a byte (mismo SHA-256)
  y tienen un tamaño de 16.139.654 bytes
  (SHA-256 `2EF1B4C1F6B756369A12D1D814373C1F629A6378CB9C41230D853046A606FC0B`).
  Un segundo build volvió a empaquetar y a copiar el archivo, también con
  hashes idénticos entre origen y destino.
- Después del cambio de blur, la suite portable volvió a pasar completa
  (0 fallos) y las cuatro regresiones focalizadas siguen pasando.
- `tests/local_mod_deploy_regression.py` pasó: despliegue automático, builds
  repetidos, rutas personalizadas con espacios, migración de la ruta antigua y
  opt-out explícito.
- Falta la prueba dentro del juego: no había un proceso `GeometryDash` activo.
  Hay que probar menús y un nivel durante al menos 90 segundos, incluyendo
  descargas de miniaturas en segundo plano, para confirmar que los tirones de
  cada 10 segundos desaparecieron.

Los logs de esta revisión usan el prefijo `build-clang/main-thread-stall-`;
los resultados de compilación sintáctica están en
`build-clang/main-thread-syntax/results.json`.

## Segunda pasada: miniaturas 5120x2880 y diagnóstico de frame pacing

La prueba anterior no explicaba los tirones porque el watchdog solo registraba
huecos de main thread de 120 ms o más. Se encontró una causa concreta en la
caché de miniaturas:

- El guard de `decodeImageData` rechazaba cualquier fuente mayor de 4096 px y
  caía a `CCImage`. Una miniatura `5120x2880` de la caché real (7,26 MB) se
  decodificaba completa y se subía a la GPU en main: unos 59 MB de textura.
- El manifiesto guardaba `768x432` (el tamaño de la textura reducida) aunque el
  archivo real fuera `5120x2880` o `1920x1080`, lo que ocultaba el problema.
- La compactación no existía: el archivo grande se volvía a leer y decodificar
  en cada visita.

Correcciones aplicadas:

- `stbi_info_from_memory` primero; se aceptan fuentes de hasta 8192 px y 24 MP.
  Se decodifican con stb y se reducen **siempre** antes de llegar a main.
- El fallback `CCImage` ya no se entrega como imagen completa: se convierte a
  RGBA en el worker y se reduce al tamaño de caché; si supera el límite, se
  rechaza en vez de subir una textura enorme.
- El manifiesto registra las dimensiones reales del archivo
  (`originalWidth`/`originalHeight`), no las de la textura reducida.
- Compactación perezosa: una imagen de caché mayor de 1920 px (1024 en móvil)
  se reduce y se reescribe como PNG en un worker, como máximo una cada 5 s. El
  manifiesto se actualiza con el nuevo tamaño.
- `[Perf]` ahora publica un resumen cada 10 s con frames, media, máximo y
  huecos de más de 50/100 ms, además de los huecos individuales de 120 ms. Esto
  permitirá distinguir un tirón puntual de una pérdida sostenida de FPS.

La compactación comprueba tamaño y `last_write_time` antes y después de
codificar: si una descarga o la poda reemplazó el archivo mientras el trabajo
estaba en cola, no lo sobrescribe con bytes antiguos.

Build desplegado: `16.139.524` bytes, SHA-256
`45D14A05AD864693216DE5C45EAE54E195B497F4DB23B3D1F2F65A8A5706FDF5`,
idéntico en `build-clang` y en `geode/mods`.

Pendiente: abrir Geometry Dash con el mod, recorrer menús y un nivel durante
90 s con descargas de miniaturas y revisar el resumen `[Perf]` del log. Si los
tirones siguen, el siguiente paso es atribuir el tiempo a los subsistemas
visuales (blur de popups/levelinfo, transiciones, auto-preview, shaders de
fondo) en lugar de seguir midiendo solo disco.
