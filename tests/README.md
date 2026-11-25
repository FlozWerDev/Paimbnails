# Regresiones

`separate_dual_side_selection` ejecuta el callback real del garaje con IDs de
Geode que incluyen el prefijo del mod. Comprueba los clics P1/P2, la repeticion
de la entrada al pulsar el mismo jugador y el modulo desactivado. Se ejecuta con
`python tests/review_behavior_regression.py --build-dir build-clang --case separate_dual_side_selection`
y usa ASan/UBSan.

`separate_dual_label_animation` ejecuta la transicion real de las etiquetas con
acciones de Cocos simuladas. Comprueba la direccion de salida, el desvanecimiento,
la inversion sin saltos al alternar rapido y que cambiar iconos o paginas no
interrumpa la animacion. Se ejecuta con el mismo comando y
`--case separate_dual_label_animation`, tambien con ASan/UBSan.

`request_verification_regression.cpp` cubre la verificacion de la cuenta desde
Level Requests: modos manual y automatico, propiedad del comentario temporal,
cancelaciones y respuestas tardias, cambio de cuenta, espera acotada,
persistencia del token antes del borrado y reintentos de limpieza. Se incluye
en la suite portable con ASan/UBSan. La fixture `request_verification_transport`
comprueba que GJP2 solo viaje al origen oficial de GD, sin seguir redirecciones,
y que los errores de publicacion no se interpreten como IDs de comentarios.

`request_queue_regression.cpp` cubre las vistas de pendientes y revisados, la
limpieza por cola sin perder pendientes, la copia de IDs sin duplicados, las
acciones por identidad despues de reordenar o eliminar, los limites de entrada
y el formato de carga manual con descripcion y video. Se incluye en la suite
portable y admite ASan/UBSan con `--sanitize`.

La fixture `request_button_hitboxes` ejecuta la logica real del boton principal
de Level Requests y comprueba su texto y color al editar canales, pausar,
reconectar o cambiar a la pagina web. Los controles personalizados tienen
tamano fijo, asi que su area de clic ya no depende del texto. Se ejecuta con
`python tests/review_behavior_regression.py --build-dir build-clang --case
request_button_hitboxes` y usa ASan/UBSan.

El renderizador Python del editor tiene una suite independiente (Pillow y NumPy;
pygame/tkinter opcionales para comprobar el visor):

```powershell
python tests/gd_renderer_regression.py
python tests/gd_renderer_regression.py --level "gemini here" --out build-regressions/gd-renderer
```

La integración lee el nivel local sin modificar el guardado y genera imágenes con
fondo, transparentes y un informe de objetos y texturas. Más detalles en
`tools/gd_renderer/README.md`.

El recorrido imagen → objetos del editor → render nativo se comprueba con el
pipeline C++ real y el mismo serializador que usa el mod:

```powershell
python tests/image_to_editor_visual_regression.py --image "ruta/al/dibujo.jpg" --reference-level "gemini here"
```

Necesita Clang++, Pillow, NumPy y los recursos locales de GD. El nivel de referencia
es opcional y se lee sin modificarlo. Genera un `.gmd`, los objetos `.level`, el
render con sprites reales, una comparativa y métricas en
`build-regressions/image-to-editor/verified/`. `--baseline-level` admite una
exportación anterior junto a su PNG `-grid`. La prueba controla presupuesto,
texturas, transparencias interiores de fuentes opacas y el recorrido de vuelta
del `.gmd`; con una base también exige reducir el error RGB.

`image_to_editor_regression.cpp` añade veintiún casos sintéticos, sin imágenes
privadas ni juego en ejecución: paleta con acentos raros, ejes independientes,
centros GD, conservación de líneas finas, Auto, costuras, marca de agua y entradas
de paleta vacías, superficies inclinadas, remates sin extrapolación y figuras
redondas con pocas piezas incluso junto a fragmentos irregulares del mismo color.
También exige reparar interiores opacos antes de contar el ahorro y comprueba
tramos largos y lados paralelos que quedan parcialmente tapados por otros colores.
Comprueba también que no se conserven piezas sobrantes en la transparencia, que
las capas posteriores respeten interiores anteriores y que la franja de las
texturas triangulares no se cuente como relleno opaco.
`--baseline-level anterior.level --require-fewer-objects` exige mejorar el error
RGB y bajar el conteo de objetos en la prueba visual. La referencia incluye su
PNG `-grid` para conservar la escala.
`--detail-region 170,360,245,460` vuelve a renderizar ese recorte a escala 4 y
genera su comparación ampliada; admite varias regiones.
`--require-clean-interiors` detecta también huecos de color rellenos por un fondo
opaco; exige cero defectos interiores con los objetos y texturas nativos.
`--background auto` prueba la eliminación automática del fondo además del modo
predeterminado `keep`, que lo conserva.
Se ejecuta en la suite portable con sanitizadores. También se
ejecutaron `gif_import_regression.cpp` y `paint_circle_regression.cpp` con ASan/UBSan
para esta revisión. Detalles en `docs/image-to-objects-paint.md`.

Las fixtures `disk_manifest_nonblocking`, `thumbnail_index_background`,
`thread_tracker_nonblocking` y `websocket_nonblocking` cubren el guardado de
miniaturas de cada 10 segundos, disco/red bloqueados, ediciones concurrentes,
destrucción de captures/TLS, colas acotadas, cancelación y reconexión sin esperar
en el hilo principal. Extraen el código de producción y usan ASan/UBSan.
Ver `docs/MAIN_THREAD_STALL_AUDIT_2026-10-21.md` para los hallazgos y los límites
de validación dentro del juego.

Las cuatro fixtures `menumusic_*` de apertura comprueban la sincronización en
segundo plano, la unión por rutas, una única decodificación compartida de portada,
la invalidación de cargas, la pausa y la conservación del carrusel. Extraen el
código de producción y se ejecutan con ASan/UBSan. Detalles en
`docs/MENU_MUSIC_OPENING_FIX_2026-10-21.md`.

Las ocho fixtures de rendimiento de UI comprueban las pasadas y caché del blur,
popups superpuestos, previews diferidos, borrado de gradientes, búsqueda por
lotes, una biblioteca de 10.000 canciones, caché acotada de portadas y callbacks
de fondos antiguos o destruidos. Extraen las funciones de producción y usan
ASan/UBSan. `visible_rows_regression` verifica además el cálculo del viewport.
Ver `docs/UI_POPUP_PERFORMANCE_2026-10-21.md` para alcance y validación pendiente
dentro del juego.

`pause_lifecycle`, `pause_panel_ownership` y `pause_owner_lifetime` ejecutan las
funciones reales de pausa, reanudacion y zoom con ASan/UBSan. Cubren reanudacion
externa, doble clic, reinicio, acciones rechazadas, cancelacion de capturas,
callbacks antiguos del editor, menus simultaneos, prioridad tactil, cambios de
escena y liberacion del gameplay incluso si un callback conserva el menu.
PauseLayer queda fuera de las animaciones genericas de paneles. Los identificadores
de nodos tambien distinguen objetos nuevos que reutilizan una direccion anterior.

`level_cell_browser_context` comprueba la entrada a Mis niveles subidos mientras
la escena saliente aun muestra Mis niveles locales. Extrae el filtro de contexto
y onEnter reales de LevelCell: cubre resultados con y sin cache, reentrada y
la exclusion de listas locales, listas de niveles y previews sin mejoras.

`editor_song_preview` ejecuta el filtro real de `FMODAudioEngine::playMusic`.
Comprueba que la preescucha de Downloaded Songs en el editor llegue al motor
incluso con estados anteriores de musica dinamica, perfiles, video o suspension,
y que los menus sigan protegiendo el audio activo frente a reinicios.

`level_select_indicators` comprueba que reconstruir los puntos del selector
conserve su visibilidad: permanecen ocultos cuando se usa la barra. La fixture
`page_slider_input` ejecuta los gestos reales de la barra y su navegacion:
toques, arrastre, extremos, agarre del cursor sin saltos, varios dedos,
cancelacion, salida y cambios en la cantidad de niveles.

`transition_scene_lifetime` reproduce el loading bloqueado de **Guardar y jugar**: modela la retencion de `WeakRefPool` de Geode 5.11 y comprueba que los registros de boton, retroceso y Hold no impidan destruir el editor y continuar la carga. Extrae las funciones reales de produccion, verifica expiracion e invalidacion, y comprueba que un cambio de escena siga detectandose cuando el asignador reutiliza una direccion. `native_scene_navigation_regression` cubre el recorrido editor -> loading -> gameplay y su regreso, incluidos los fades nativos.

Las fixtures `quick_hub_hold` y `quick_hub_settings` ejecutan los listeners, el temporizador y los ajustes reales de Quick Hub con ASan/UBSan. Cubren toques cortos, modificadores atrasados, repeticion, duraciones, perdida de foco, atajos, teclas y botones del mouse. `local_mod_deploy_regression.py` compila targets de CMake en una carpeta temporal para comprobar la copia de cada paquete y la conservacion de otros mods.

La suite portable comprueba lógica C++, contratos de integración y precargas sin abrir Geometry Dash. Requiere Python 3, Node.js y un compilador C++20. En Windows se admiten Clang-cl/MSVC para las pruebas C++; las dos pruebas nativas de precarga de Node requieren Clang++.

```powershell
python tests/run_regressions.py --suite portable --build-dir build-clang --sanitize
python tests/run_regressions.py --suite bindings --build-dir build-clang --sdk "$env:GEODE_SDK"
```

Con un build configurado y Python detectado por CMake también están disponibles:

```powershell
cmake --build build-clang --target check
ctest --test-dir build-clang --output-on-failure
```

Si Python no está en el PATH, configura `-DPython3_EXECUTABLE=<ruta al intérprete>`. `--cxx` permite elegir el compilador; `CXX` funciona como alternativa. Los resultados y logs quedan en `<build>/regression-results/`. La compilación normal conserva su instalación automática del mod; `check` y CTest solo ejecutan pruebas.

La suite `portable` incluye 31 regresiones C++, con cobertura de compatibilidad, editor, guia conversacional, importacion GIF, generacion de texturas, navegacion, precarga y sprites; tambien prueba en ejecucion los 132 interruptores y comprueba sus ajustes, padres y accesores, ademas de transiciones/live slots/RTX. `--sanitize` activa ASan y, con Clang/GCC, UBSan en las 31 regresiones C++. Las fixtures nativas de las dos precargas ya usan ambos sanitizadores.

La regresión de escritura atómica conserva los casos de archivo bloqueado y destino inválido, y comprueba dos escritores simultáneos con 128 publicaciones cada uno.

La prueba WAV enlaza el código real de escritura y caché y comprueba rutas Unicode, cabeceras, muestras, limpieza y límites de tamaño. Solo simula el logger de Geode y la conversión de rutas a UTF-8; las operaciones con archivos usan la biblioteca estándar del sistema.

`review_behavior_regression.py` añade 26 escenarios de callbacks, identidad persistente, rutas de caché, protocolo Versus, voz, errores NDK, JSON, presets y conservación de texturas ante fallos. Las fixtures extraen las funciones actuales de producción y simulan el motor, la red o el resultado del guardado. Se compilan con ASan/UBSan y no abren el juego. La prueba JSON enlaza matjson real. Requieren Clang++/G++, fmt y las fuentes de matjson/Result descargadas por la configuración del proyecto. Por eso la CI ejecuta las regresiones después del paso que obtiene esas dependencias. Los bancos GIF y Animate también cubren imágenes sin GCE y rangos inválidos grandes.

Los bancos de guía admiten 240 segundos para compilar y 180 para ejecutar; así no agotan el tiempo por la instrumentación de sanitizadores y las plantillas de RapidFuzz. El resto conserva sus límites individuales.

La suite `bindings` usa el build indicado y su repositorio real de bindings; no presupone una carpeta llamada `build-win`. El inventario guardado registra nombres de archivos y hashes, sin rutas particulares de una máquina. Para actualizarlo tras cambiar los bindings:

```powershell
$env:PAIMBNAILS_BUILD_DIR = "build-clang"
python tests/audit_dynamic_transition_bindings.py --write
```

`hook_bindings_regression.py` audita los 442 hooks de todos los `$modify`: exige que cada método declarado en la clase base tenga `GEODE_APPLY_MODIFY_FOR_FUNCTION` en el header generado, que los `$override` existan en la jerarquía y que no haya dos métodos del mismo bloque compartiendo dirección Windows. Los overrides heredados que Geode no genera se migraron a `NodeEvent` (`addOnEnterCallback`, `addOnExitCallback`, `addCleanupCallback`); los 7 restantes son solo `ccTouch*` y `enterPressed`, que necesitan un delegate o un hook de la clase base. El test los lista en `KNOWN_INHERITED` y falla si aparece uno nuevo; el documento `docs/BINDINGS_AUDIT_2026-11-08.md` mantiene la deuda pendiente. `--report` imprime el inventario sin fallar y `--write` regenera `docs/HOOK_BINDINGS_AUDIT.md`.

## Conversion con GPU (Windows)

`cmake --build build-clang --target check-gif-compute` compara la conversion real
por CPU y DirectCompute. Requiere una GPU con D3D11, Clang y FXC del Windows SDK.
Comprueba colores de una animacion, geometria final de pintura, cobertura opaca,
transparencia, rotaciones, skew y fallback de cargas pequenas o sellos nativos.
Informa tiempos frios/calientes por separado; la mejora de una etapa no equivale
a la mejora del importador completo. Los logs quedan en `gif-compute-tests/`.

Para ejecutar las mismas pruebas con ASan/UBSan:

```powershell
python tests/run_gif_compute_regression.py --build-dir build-clang --cxx "C:/Program Files/LLVM/bin/clang-cl.exe" --sanitize
```

Los kernels `.hlsl` se compilan durante el build del mod, sin compilacion de
shaders en la primera importacion. Las tareas grandes usan GPU automaticamente;
las pequenas, los sellos y los dispositivos incompatibles conservan la ruta CPU.
El trazado usa como maximo cuatro hilos y deja al menos la mitad de los hilos
logicos disponibles cuando el equipo tiene dos o mas.

## Integración opcional

```powershell
python tests/run_regressions.py --suite optional --build-dir build-clang
```

Esta suite informa `SKIP` con una razón cuando falta un requisito; no los presenta como pruebas aprobadas:

- `thumbnail_upload_routing`: necesita el servidor privado en `cloudflare-server/`.
- `stream_overlay`: necesita Playwright y Chromium instalados. Admite `PLAYWRIGHT_MODULE` y `CHROMIUM_PATH` para instalaciones existentes.
- `packgen_official_assets` y `ui_sprite_audit`: necesitan `Resources` del juego en `PAIMBNAILS_GD_RESOURCES`.
- Pruebas GPU: necesitan Linux con EGL/GLES para renderizado sin ventana.

El workflow compila con el SDK del manifiesto (`sdk: given`) y ejecuta después las suites portable y de bindings en cada plataforma, usando el build configurado y las dependencias descargadas por la acción oficial. Los mocks de Geode de las pruebas independientes no sustituyen las pruebas manuales dentro del juego.

Las nuevas regresiones cubren IDs de proyecto independientes del nombre/importación, confinamiento de rutas, alias de mayúsculas en Windows, selecciones antiguas inválidas, errores de escritura del índice, referencias de imágenes importadas, guardados fallidos al navegar, revisiones de compilación, cierre de callbacks, generaciones y presupuesto de miniaturas, navegación de galerías y liberación/reentrada de `RetainedLazyTextureLoad`.

La compilación de iconos también se prueba con fallos por calidad, ediciones concurrentes, fallos parciales de índice y publicación/restauración de directorios reales. Se comprueba que la salida anterior permanezca intacta, que los temporales se limpien y que una restauración bloqueada conserve la copia anterior. Las vistas previas automáticas cubren rutas Unicode, 128 guardados entre dos hilos, archivos bloqueados y renovación de la caché sin liberar texturas desde un worker. La fixture enlaza el escritor atómico real.

La resolución de fmt, RapidFuzz, matjson y Result usa primero las fuentes elegidas en `CMakeCache.txt`; también admite `CPM_SOURCE_CACHE`, la caché `cpm-cache` de la acción oficial y `_deps`. Esto evita tomar otra revisión de una caché restaurada que contenga varios hashes. La suite portable falla con una razón si faltan sus dependencias: no convierte esa situación en una aprobación.

Las regresiones de pausas de Music Menu rechazan el getter nativo `getDownloadedSongs()` (que consulta archivos antes de devolver la lista), cubren invalidaciones de descargas con el mismo conteo y verifican un buz?n de Discord con el peer bloqueado: publicar estados y detener el servicio no esperan al IPC. El registro de portadas tambi?n comprueba que 500 mensajes se escriban desde el worker y conserven su orden.
