# Discord Rich Presence

La configuracion se divide en Actividad, Privacidad, Textos, Imagenes y Enlaces.
Los cambios se guardan automaticamente. Hay presets Balanced, Detailed,
Minimal y Private, e importacion/exportacion de perfiles JSON. Importar o
restablecer personalizaciones conserva el interruptor de publicacion.

## Actividad y personalizacion

- Juego: normal/practica, platformer, pausa, muerte, nivel completado,
  porcentaje actual del intento, intentos y mejor porcentaje del modo elegido.
- Editor: edicion, playtest, playtest pausado y cantidad de objetos.
- Navegacion: informacion de niveles, perfiles, niveles locales, destacados,
  trending, awarded, busqueda, garage, tiendas, cofres, secretos, Tower,
  gauntlets, desafios, niveles oficiales y otras pantallas conocidas.
- Paimbnails: Hub, ajustes, Texture Live, proyectos/fusion de texturas,
  Icon Maker, importacion de imagenes, editor colaborativo, capturas,
  moderacion, Menu Music y musica de perfiles. PaiDraw distingue dibujar,
  adivinar, sala, lobby, resultados y desconexion/preview offline.
- Textos globales y especificos para juego, editor y menus; hover text,
  claves de assets o URLs de imagen, tipo de actividad y Application ID propio.
- Dos botones configurables: proyecto, Discord, nivel, enlace personalizado
  u oculto. Enlaces adicionales para details, state e imagenes.
- Temporizador de sesion, actividad, nivel o intento. El porcentaje no reinicia
  estos relojes; una nueva sesion/nuevo intento si cambia el reloj pertinente.

Las variables disponibles son `{activity}`, `{screen}`, `{level}`, `{creator}`,
`{level_id}`, `{mode}`, `{progress}`, `{best}`, `{attempts}`, `{objects}`,
`{difficulty}`, `{song}`, `{artist}`, `{profile}`, `{room}` y `{players}`.
Los valores no disponibles o no autorizados se sustituyen por texto vacio.
Las variables desconocidas quedan visibles para corregir errores de escritura.
`{{` introduce una llave literal. Los textos terminan en UTF-8 valido y se
limitan a 128 bytes; las etiquetas de botones a 32. Las URLs solo expanden
`{level_id}`, no se truncan ni se cambia HTTP a HTTPS automaticamente.

La integracion conserva los cuatro tipos admitidos por el transporte RPC y
el limite de dos botones de Discord. Referencias oficiales:
[SET_ACTIVITY](https://docs.discord.com/developers/topics/rpc#set-activity),
[assets, enlaces y botones](https://docs.discord.com/developers/discord-social-sdk/development-guides/setting-rich-presence).

## Privacidad

Cada dato tiene su control: nombres, creador, ID/enlace, progreso, intentos,
mejor porcentaje, dificultad, objetos, perfiles, musica, salas y jugadores.
La identidad de niveles locales/unlisted necesita permiso separado, incluso
con el preset Detailed. No se publican palabras, pistas privadas, contrasenas
ni chats de PaiDraw. Los nuevos datos detallados no se habilitan por defecto.
La migracion conserva un antiguo opt-out de progreso al introducir el control
independiente del mejor porcentaje.

Private Mode tiene prioridad sobre textos, variables, assets personalizados,
Application ID, enlaces, botones y jugadores: publica solo una sesion generica.
Se puede compartir en todas las pantallas, solo jugando, juego/editor o menus.
Las pantallas excluidas borran la presencia. Un cambio de permisos solicita un
borrado prioritario antes de enviar la actividad nueva; este borrado no se
puede perder por reemplazar mensajes pendientes de la bandeja.

Elegir una imagen local inicia su subida publica a Catbox, con fallback a
0x0.st. La interfaz lo explica antes del boton. Los callbacks usan referencias
debiles y generaciones por imagen: cerrar, restablecer, importar o editar el
campo descarta resultados antiguos. Los reintentos ya no se retienen a si
mismos ni fijan el buffer subido indefinidamente.

## Vista previa, conexion y coste

La vista previa de texto usa el mismo constructor que el envio: no inventa
porcentajes ni tiempos de ejemplo. Las imagenes personalizadas tienen un
placeholder; el render de assets debe verificarse en Discord. El estado de
conexion diferencia deshabilitado, esperando al cliente, handshake, conectado
y errores comunicados por Discord. Update confirma que se encolo la
actualizacion, no que Discord ya la haya mostrado.

Solo se capturan snapshots de Cocos en el hilo principal. El worker recibe
valores, combina actualizaciones cada cinco segundos y atiende borrados sin
esa espera. Los snapshots pueden ser cada 2, 5 o 15 segundos. El IPC se revisa
tambien cuando el payload no cambia para detectar respuestas y reinicios.
La deteccion de herramientas visita un numero acotado de nodos y las escenas
en transicion se resuelven hacia el destino.
PaiDraw expone un resumen de actividad sin copiar trazos, chats, palabras,
listas de usuarios, credenciales ni el resto de la sesion. El nombre de sala
solo se copia cuando esta autorizado en Privacidad.

El lector conserva frames parciales, interpreta opcodes, responde PING con
PONG, valida tamanos y lee eventos ERROR como JSON. La conexion y READY se
comparten mediante atomicos; el mensaje de error usa un mutex. Desactivar
envia `activity: null`. En Windows, las operaciones canceladas retienen tanto
OVERLAPPED como el buffer y el evento hasta su finalizacion; la cola esta
acotada a ocho operaciones. En POSIX, la conexion se inicia sin bloquear.
Si no llega READY en cinco segundos, se cierra la conexion y se permite el
reintento. Los fallos del worker no se anuncian como una presencia actualizada;
Update puede volver a iniciarlo.

## Comprobaciones

```powershell
python tests/discord_presence_regression.py
```

No compila ni contacta con Discord. Comprueba contratos de privacidad,
integracion, opciones, preview, transporte y callbacks. Las fixtures nativas
`discord_customization`, `discord_rpc_frames` y `discord_background_ipc`
extraen funciones de produccion para probar formateo, UTF-8, URLs, timers,
overrides, privacidad, JSON, fragmentacion, PING/PONG, errores y bandeja de
mensajes. Se dejan preparadas para la suite con ASan/UBSan.

No se compilo ni instalo el mod en esta revision. Queda pendiente ejecutar
esas fixtures, comprobar el popup dentro del juego y verificar con Discord
abierto/cerrado/reiniciado, niveles normales/practica/platformer, niveles
privados, importaciones y subidas de imagen simultaneas a cambios de pestana.
