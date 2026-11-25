# Bloqueo al abrir Menu Music

La revisión anterior mejoró la biblioteca de canciones. El reproductor principal
todavía hacía un recorrido forzado de las canciones descargadas y cargaba la
primera portada de forma síncrona durante `MenuMusicPopup::init`.

## Causas comprobadas

- `syncDownloadedSongs()` utilizaba por defecto `force=true`: consultaba el
  archivo de cada canción en cada apertura. Además, buscaba cada ruta recorriendo
  linealmente la biblioteca, lo que hacía cuadrática la combinación de listas
  grandes.
- Portada, discos y fondos utilizaban `loadBudgeted()`. La primera lectura y
  decodificación de una portada no cacheada se ejecutaba en el hilo de UI.
- Los fondos utilizaban la variante prioritaria del blur, que arranca trabajo
  inmediatamente y permite varias operaciones por actualización.
- Una actualización del estado reconstruía las portadas aunque la lista no
  hubiera cambiado, reiniciando además el carrusel.

La fixture `menumusic_opening` falló con el código anterior porque la construcción
del popup ejecutaba un escaneo bloqueante. Con el cambio solicita trabajo en
segundo plano y conserva las portadas cuando solo cambia el estado.

## Correcciones

La sincronización automática toma una instantánea de los metadatos de GD en el
hilo principal, comprueba archivos en un worker y publica el resultado de nuevo
en el principal. Las aperturas repetidas comparten el trabajo pendiente y una
cantidad de descargas ya sincronizada evita nuevos escaneos. La unión de las
canciones utiliza un índice de rutas; mantiene nombres editados, favoritos,
identidades y el orden de incorporación. La sincronización manual sigue
disponible y utiliza la misma unión.

Si se pulsa Play o All Songs antes de terminar la primera sincronización de una
biblioteca vacía, se conserva esa solicitud y se ejecuta al terminar. Pulsar
Play de nuevo, desactivar el modo o cerrar el popup cancela la solicitud.

`loadBudgetedAsync` comparte una sola lectura y decodificación por ruta entre
todos los consumidores de la portada. Conserva el LRU de 24 texturas, los aciertos
en memoria y la invalidación de solicitudes al vaciar o retirar una entrada.
Las imágenes y los fondos aceptan resultados mediante referencias débiles y
generaciones, por lo que una portada anterior no reemplaza la nueva y un cierre
invalida las entregas pendientes.

El desenfoque usa la cola progresiva existente. Conserva el kernel, intensidad,
encuadre y fundidos, repartiendo el cálculo entre actualizaciones. Las portadas
recibidas tarde conservan el color y la opacidad del estado de pausa. Actualizar
la reproducción con las mismas portadas conserva la posición del carrusel.

No se cambiaron recursos gráficos, controles, filtros, reproducción ni atajos.

## Validación

Cuatro fixtures extraen el código actual de producción y usan ASan/UBSan:

- `menumusic_opening`: ausencia de escaneo bloqueante en el constructor,
  cambio de portadas y conservación del carrusel.
- `menumusic_download_sync`: trabajo diferido, solicitudes repetidas, metadatos
  editados durante la carga, archivos ausentes y unión de 20.000 entradas con
  10.000 rutas distintas sin duplicados.
- `menumusic_shared_texture`: cuatro consumidores comparten una carga,
  aciertos en memoria, reentrada de callbacks, fallo, invalidación y apagado.
- `menumusic_artwork_lifetime`: descarte de imágenes antiguas, cierre durante la
  carga, destrucción del popup, deduplicación y apariencia de pausa tras recibir
  la textura; también cubre la ejecución única de una reproducción pendiente.

La DLL se compila en Release con Clang y el SDK Geode 5.11.2 / GD 2.2081. Los logs
quedan en `build-clang/menu-music-opening-build-final.log`,
`build-clang/menu-music-opening-regressions.log` y
`build-clang/menu-music-opening-portable.log`.

Resultado de esta ejecución: las cuatro fixtures específicas pasaron con
ASan/UBSan y la suite portable terminó con 72 comprobaciones y cero fallos. El
SHA-256 de la DLL extraída del paquete coincide con el de la DLL recién compilada;
la verificación está en `build-clang/menu-music-opening-verification.json`.

El paquete conserva siempre el nombre `build-clang/flozwer.paimbnails2.geode`.
Las comprobaciones de apertura son deterministas y no equivalen a una medición
de FPS o tiempo de apertura dentro de Geometry Dash; esa medición sigue pendiente.
