# Carga y rendimiento de popups e interfaces

La corrección posterior de la apertura del reproductor principal está en
`MENU_MUSIC_OPENING_FIX_2026-10-21.md`; se incluye en el mismo paquete habitual.

Se hizo una revisión estática de 238 archivos C++ de interfaces, popups y capas,
que cubren 62 familias de funciones. Se buscaron cargas síncronas, lecturas de
disco, construcción de listas, actualizaciones por fotograma y los hooks
compartidos de apertura, cierre, animaciones y desenfoque. El inventario de esta
ejecución está en `build-clang/ui-performance-inventory.json`.

Esta revisión y sus pruebas no sustituyen una medición de FPS dentro de Geometry
Dash. No se dispone de una captura de tiempos ni de una comparación visual en el
juego; no se atribuye una cifra de mejora de FPS a estos cambios.

## Cambios

| Ruta | Coste observado | Corrección |
| --- | --- | --- |
| Desenfoque compartido de popups | El filtro vertical se ejecutaba a resolución de pantalla en cada fotograma, incluso al reutilizar la pasada horizontal. | Las dos pasadas se calculan en los render targets reducidos existentes. Se guarda el resultado y la composición final usa una sola muestra por píxel. Se conservan intensidad, oscuridad, fondo vivo, cadencia de refresco y fundidos. |
| Creación de render targets | La apertura podía asignar buffers con el tamaño de ventana y reasignarlos al encontrar un viewport diferente por DPI o bandas. | La primera visita visible asigna directamente con las dimensiones reales del viewport. |
| Popups superpuestos | Al cerrar el tercero se volvían a mostrar los desenfoques del primero y del segundo. | Se restaura únicamente el superior según z y orden de llegada. El watchdog también restaura el anterior cuando detecta un popup huérfano. |
| Buscador de ajustes | Cada pulsación construía todos los controles de todas las categorías en el mismo fotograma. | Espera de 120 ms tras escribir y construcción de una subcategoría por fotograma. Publicación conjunta de resultados y cancelación al escribir de nuevo, cambiar de categoría o cerrar. |
| Selector de fuentes | Se cargaban los 59 atlas al abrir y se reconstruían al volver a la pestaña. | Una muestra visible por fotograma, conservación de las muestras y trabajo suspendido cuando la lista está quieta y ya está lista. |
| Configuración del cursor | Se construían siete pestañas, incluida la tienda y sus previews, al abrir la galería. | Cada pestaña utiliza su constructor original una sola vez, en su primera visita. |
| Gradientes guardados | Hasta 100 previews con shaders antes de mostrar la ventana. | Pintado diferido, máximo cuatro previews y comprobación de un presupuesto de 2 ms entre previews. El borrado ajusta el índice pendiente y elimina la referencia del mapa. Una operación individual aún puede superar ese presupuesto. |
| Biblioteca de música | Se creaban todas las tarjetas y se decodificaban sus portadas al reconstruir la lista. | Solo se crean las filas visibles y un margen. Las portadas se decodifican en segundo plano y se cancelan al retirar tarjetas. Se conserva el LRU de 24 texturas. |
| Orden y búsqueda de música | Consultas de tamaño de archivo dentro del comparador y reconstrucción por cada letra. | Un tamaño por canción y espera de 120 ms al escribir. Se conservan filtros, orden inverso, vista compacta, acciones y navegación a la canción actual aunque su tarjeta todavía no exista. |
| Vista previa de fondos | Lectura y decodificación de imágenes personalizadas grandes en el hilo principal. | Se usa el cargador asíncrono acotado existente, conservando el límite de 24 MB, shaders y encuadre. Una generación y una referencia débil rechazan resultados antiguos o de nodos destruidos. |
| Estado del overlay | Se consultaba y formateaba el estado en cada fotograma. | Actualización cuatro veces por segundo; el desplazamiento sigue actualizándose por fotograma. |

Los recursos gráficos, opciones y estilos de animación no se eliminaron ni se
desactivaron. La optimización actúa sobre cuándo se construyen o calculan y sobre
la reutilización del resultado. Las demás familias quedan cubiertas por la
revisión de las rutas comunes y las búsquedas estáticas; no se afirma haber
probado manualmente cada ventana.

## Regresiones

Las fixtures extraen las funciones actuales de producción y se ejecutan con
AddressSanitizer y UndefinedBehaviorSanitizer:

- `popup_blur_passes`: ambas pasadas reducidas, composición de una muestra,
  caché, fondo vivo, fundido, viewport con bandas y error de blit.
- `popup_blur_stack`: tres ventanas, restauración de una sola superficie,
  prioridades z, borrado de una ventana cubierta y superficies huérfanas.
- `popup_preview_loading`: construcción única de las siete pestañas y una
  fuente visible por actualización, sin cargar las que quedan fuera.
- `gradient_preview_batches`: límite de lote, finalización y borrado con
  previews todavía pendientes.
- `settings_search_batches`: espera inicial, un trabajo por actualización,
  cancelación, referencias entre fotogramas, resultados vacíos y cierre.
- `music_library_virtualization`: 10.000 canciones, no más de siete tarjetas
  para el viewport probado, reposo sin reconstrucción, salto al elemento 5.000
  y cancelación de portadas retiradas.
- `async_cover_budget`: 10.000 portadas, límite de 24 y conservación de las
  recientemente usadas.
- `background_preview_loading`: entrega diferida, cambio de selección, error
  de carga y destrucción del nodo con una solicitud pendiente.

`visible_rows_regression` comprueba además los límites de la ventana visible,
overscroll, listas vacías, geometría inválida y recorridos de una lista de
10.000 filas. Se integra en la suite portable.

Se actualizaron dos detalles del banco portable: el stub de `hasSetting` admite
entradas `string_view` como el SDK y `packgen_image_regression` enlaza el escritor
atómico que utiliza su caché.

## Validación y artefactos

- Compilación de la DLL Release con Clang y Geode 5.11.2 / GD 2.2081.
- Suite portable con sanitizadores: 72 comprobaciones, cero fallos.
- Bindings del SDK local: tres comprobaciones, cero fallos.
- Ocho fixtures específicas de UI con ASan/UBSan: todas aprobadas.
- Paquete: `build-clang/flozwer.paimbnails2.geode`. Se conserva siempre el nombre
  habitual del paquete, sin sufijos por optimizaciones o revisiones.
- Logs: `build-clang/ui-performance-compile-background.log`,
  `build-clang/ui-performance-regressions-final.log` y
  `build-clang/ui-performance-bindings.log`. Las fixtures específicas quedan en
  `build-clang/ui-performance-focused-tests.log`.

El build utilizó el grafo Ninja ya configurado, copiado como
`build-clang/ui-performance.ninja`. La regeneración normal de CMake encontró una
escritura de configuración global de Geode CLI fuera del workspace y falló con
acceso denegado. El grafo existente permitió compilar y enlazar el mod con sus
dependencias locales; el empaquetado se hace directamente con Geode CLI dentro
del workspace, sin instalación automática.

## Verificación pendiente dentro del juego

Comparar tiempo de apertura y tiempos de fotograma con la misma resolución,
límite de FPS, ajustes y escena: abrir/cerrar tres popups superpuestos, escribir
rápido en ajustes, recorrer fuentes y gradientes, visitar las siete pestañas del
cursor y recorrer una biblioteca grande de música. Revisar además imágenes
grandes con shader, cierre durante la carga y el encuadre de portadas.

Las pruebas GPU sin ventana del proyecto requieren Linux EGL/GLES; esta
ejecución en Windows comprueba la secuencia de llamadas de renderizado y la
compilación, sin declarar una validación visual del desenfoque.
