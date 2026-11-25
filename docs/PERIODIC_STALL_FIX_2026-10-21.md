# Music Menu y pausas periÃƒÂ³dicas: investigaciÃƒÂ³n del 21 de octubre de 2026

## Hallazgos y lÃƒÂ­mites

Se verificÃƒÂ³ por SHA-256 que el paquete instalado en el juego coincidÃƒÂ­a con el entregado anteriormente (`64f85e3c9b4ea18f8e97cd6201ee34a203870a74ab1887c5032e335b736a36da`). No era una copia antigua. La configuraciÃƒÂ³n local tenÃƒÂ­a Discord RPC y los diagnÃƒÂ³sticos de portadas activados, y la generaciÃƒÂ³n pesada de previews del navegador desactivada. Esta ÃƒÂºltima se descartÃƒÂ³ para esta sesiÃƒÂ³n.

La sincronizaciÃƒÂ³n supuestamente asÃƒÂ­ncrona aÃƒÂºn llamaba a `MusicDownloadManager::getDownloadedSongs()` **antes** de crear el worker, incluso antes de comprobar si el contenido habÃƒÂ­a cambiado. El ejecutable local de GD 2.2081 confirma que esa funciÃƒÂ³n (RVA `0x33e5e0`) recorre los objetos de canciones y llama a `isSongDownloaded` (`0x33f250`), que consulta el sistema de archivos a travÃƒÂ©s de FileUtils. La fixture anterior simulaba este getter como una consulta gratuita: no detectaba ese bloqueo.

Discord preparaba el estado y ademÃƒÂ¡s conectaba, escribÃƒÂ­a y drenaba respuestas IPC desde `refreshNow`, en el hilo principal. Los lÃƒÂ­mites de espera eran de 500 ms por escritura y 100 ms por lectura; la reconexiÃƒÂ³n tenÃƒÂ­a un intervalo de 15 segundos. La configuraciÃƒÂ³n observada permite que esa ruta se ejecute. Esto demuestra una fuente de bloqueos periÃƒÂ³dicos posible, pero no prueba que explique todas las pausas observadas.

Cada diagnÃƒÂ³stico de portadas abrÃƒÂ­a, escribÃƒÂ­a y cerraba un archivo de forma sÃƒÂ­ncrona. La apertura del menÃƒÂº emite varios mensajes, aun con el registro general del mod desactivado.

Windows impidiÃƒÂ³ el perfilado ETW y la apertura de los hilos del juego para obtener pilas. No se cambiÃƒÂ³ el proceso ni se midieron FPS o tiempos de apertura dentro del juego actualizado. No se afirma que la reproducciÃƒÂ³n real estÃƒÂ© resuelta a partir de las fixtures.

## Cambios

- La sincronizaciÃƒÂ³n obtiene el conteo y los datos de `m_songObjects`, sin llamar al getter que examina archivos. La comprobaciÃƒÂ³n de existencia permanece en el worker de la ruta asÃƒÂ­ncrona. Las reaperturas con la misma metadata no recorren archivos.
- El hook de `songStateChanged` invalida el snapshot al completar/eliminar descargas, aunque el nÃƒÂºmero de canciones conocidas siga igual. Una generaciÃƒÂ³n evita que un scan antiguo borre una invalidaciÃƒÂ³n recibida mientras trabajaba.
- Un solo worker posee el cliente IPC de Discord. El hilo principal construye el estado de Cocos y publica una copia en un buzÃƒÂ³n que conserva el valor mÃƒÂ¡s reciente. El mutex del buzÃƒÂ³n nunca abarca una operaciÃƒÂ³n IPC. Hay reconexiÃƒÂ³n con el ÃƒÂºltimo estado, seÃƒÂ±ales de cierre y coalescencia de refrescos para evitar una cola de tareas al recuperar un frame lento.
- El contador de generaciÃƒÂ³n de la conexiÃƒÂ³n es atÃƒÂ³mico para su lectura desde la interfaz.
- Los diagnÃƒÂ³sticos de portadas se agrupan y escriben desde un worker, sin retener el mutex de los productores durante el acceso al disco. Se conservan el archivo y la salida de consola configurada.

Se mantienen el diseÃƒÂ±o, los controles, las portadas, la sincronizaciÃƒÂ³n de canciones y Rich Presence. El paquete conserva exactamente el nombre **flozwer.paimbnails2.geode**.

## VerificaciÃƒÂ³n

- CompilaciÃƒÂ³n Release con clang-cl y el SDK Geode 5.11.2: completada.
- La regresiÃƒÂ³n `menumusic_download_sync` ahora rechaza cualquier llamada al getter nativo durante la sincronizaciÃƒÂ³n. TambiÃƒÂ©n cubre una descarga nueva sin cambio de conteo y una invalidaciÃƒÂ³n concurrente con un scan.
- `discord_background_ipc` retiene al peer IPC bloqueado mientras el hilo principal publica 1000 estados y detiene el servicio. Estas operaciones deben terminar en menos de 100 ms antes de desbloquear al peer. TambiÃƒÂ©n comprueba coalescencia, clear, cierre y callbacks pendientes.
- `menumusic_cover_logging` publica 500 mensajes: el archivo no aparece hasta ejecutar el worker, y se conservan todos los mensajes y su orden.
- Suite portable: **72 controles, 0 fallos**; incluye **47 fixtures de comportamiento con ASan/UBSan, 0 fallos**. Suite de bindings: **3 controles, 0 fallos**.
- Los resultados completos de las suites portable, ASan/UBSan y bindings quedan en `build-clang/periodic-stall-*.log`. El JSON de verificaciÃƒÂ³n del paquete registra sus hashes y comprueba que contiene la DLL compilada.

Para verificar el sÃƒÂ­ntoma real hace falta reemplazar el paquete instalado por el nuevo y reiniciar Geometry Dash. Probar apertura en frÃƒÂ­o y reaperturas de Music Menu, navegaciÃƒÂ³n en menÃƒÂºs y un nivel durante al menos 90 segundos, manteniendo Discord activado. Los permisos de esta sesiÃƒÂ³n no permitieron instalar el archivo fuera del workspace ni capturar las pilas del juego.