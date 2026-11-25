# Crash al recibir recompensas y mostrar un logro

Informe: `2026-10-30_03-04-11.3085024.log`, Geode 5.11.2, Windows x64.

## Evidencia

La respuesta de recompensas ejecuta `RewardsPage::rewardsStatusFinished`, actualiza
estadisticas y termina en `AchievementNotifier::showNextAchievement` y
`AchievementBar::show`. La barra consulta `CCScene::getHighestChildZ`.

En el binario local, `CCArray::count + 0x4` lee `[RAX]`, despues de cargar
`CCArray::data` desde `[RCX + 0x38]`. El informe muestra RCX distinto de cero y
RAX igual a cero: existe un puntero al arreglo, pero su almacenamiento interno
es nulo. Tanto la proteccion de Geode (`getChildrenCount`) como la anterior del
mod (`children->count()`) alcanzan esa lectura. La pila no permite determinar
quien invalido ese almacenamiento.

El codigo nativo tambien guarda `AchievementNotifier::m_nextScene` como puntero
sin retencion y lo copia a la barra antes de mostrarla. Consultar ese destino
sin resolverlo nuevamente puede acceder a una escena que ya dejo de estar viva.
Es una posibilidad cubierta por la correccion, no una causa demostrada por el
informe. Los hooks de rendimiento en la parte inferior de la pila envuelven el
scheduler y el dibujo; su presencia no identifica al responsable.

## Correccion

- `CCSceneSafety.cpp` inspecciona `m_pChildren`, `data` y `arr` antes de contar o
  llamar a `getChildren`, que puede ordenar el arreglo. Los arreglos validos
  siguen usando el calculo original de Z.
- Antes de sacar un aviso de la cola nativa, el notificador obtiene el destino
  desde el director. Durante una transicion usa la escena entrante y reemplaza
  el puntero debil anterior sin desreferenciarlo.
- Si no hay una escena disponible o su almacenamiento no esta listo, conserva
  la cola y reintenta mediante un selector sin pausa. Al disponer de una escena
  valida cancela el selector y continua por la implementacion nativa. No cambia
  el registro de recompensas ni el desbloqueo de logros.

## Verificacion

`tests/scene_safety_regression.cpp` incluye el hook de produccion con stubs del
motor. La prueba de `data == nullptr` fallo antes de la correccion con
`FAIL: CCArray::count dereferenced null data` y pasa despues. Tambien cubre Z
normal, Z negativos, un destino liberado, transiciones pendientes y activas,
escenas ausentes y reintentos sin perdida ni duplicacion de avisos.

El fixture pasa con AddressSanitizer y UndefinedBehaviorSanitizer. Esta prueba
reproduce las condiciones de memoria en un entorno aislado; no reproduce la
sesion completa del juego ni identifica el origen de la invalidacion.

La suite portable termino con 94 comprobaciones aprobadas de 96. Los otros dos
resultados corresponden a archivos ajenos a esta correccion:

- `review_behavior_regression`: agoto el limite global de 240 segundos al
  recompilar la tanda de 82 fixtures.
- `review_guards_regression`: exige literalmente `now < checkedAt` en
  `BanGate.cpp`; el codigo actual usa `now < cache->checkedAt`. Es una
  discrepancia de la comprobacion textual, no evidencia de que falte ese guard.

Los resultados completos estan en `build-clang/regression-results/portable.json`.

Compilacion local: `cmake --build build-clang --target Paimbnails_PACKAGE --parallel 8`,
con `PAIMBNAILS_DEPLOY_LOCAL_MOD=ON`. El paquete se instala en la carpeta `geode/mods`
de Steam. La comprobacion del SHA-256 queda en
`build-clang/achievement-scene-verification.json`.

Queda pendiente probar la misma interaccion de recompensas dentro del juego.
