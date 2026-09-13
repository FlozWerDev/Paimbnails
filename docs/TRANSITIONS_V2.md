# Transiciones v2

## Alcance

El motor de comandos se reemplazo por una transicion derivada de `CCTransitionScene`.
Se mantienen los identificadores de configuracion y los presets nativos existentes.
El nuevo tipo `stinger` permite importar imagen, GIF o video, preparar spritesheets
y elegir el punto de corte entre las escenas.

Referencia de comportamiento: [stinger de OBS](https://github.com/obsproject/obs-studio/blob/master/plugins/obs-transitions/transition-stinger.c).
OBS separa la duracion del medio del punto de cambio y reproduce el overlay sin loop.
Este sistema adopta esa separacion. No reutiliza codigo de OBS ni su motor multimedia.
OBS tambien implementa [track matte](https://obsproject.com/kb/track-matte-stinger-transitions):
es una composicion por mascara, diferente del corte temporal implementado aqui.

## Componentes y responsabilidades

| Componente | Responsabilidad |
| --- | --- |
| `TransitionHook` | Seleccion y adaptacion de navegacion; deja pasar clases ajenas, navegacion instantanea y transiciones en curso. |
| `TransitionManager` | Configuracion compatible, validacion, presets y precarga de los medios seleccionados. |
| `TransitionTimeline` | Compilacion de secuencias/grupos, delays, tiempos absolutos y seleccion de frame. No depende de Geode. |
| `TransitionMedia` | Cola de importacion, decodificacion, empaquetado, cache persistente, subida GPU y referencias de medios. |
| `CustomTransitionScene` | Ciclo de vida nativo y composicion visual. Comandos sobre capturas; stinger sobre escenas originales. |
| `StingerConfigPopup` | Importacion, duracion, corte porcentual con lectura en milisegundos y preview local. |
| `CustomTransitionEditorPopup` | Comandos heredados, medios preparados y grupos paralelos; edita una copia y devuelve cambios por callback. |

### Integracion con GD

Se revisaron los bindings locales de GD **2.2081**:

- `build-win/_deps/bindings-src/bindings/2.2081/Cocos2d.bro`: `CCDirector::replaceScene`,
  `pushScene`, `popSceneWithTransition` y `CCTransitionScene::{initWithDuration,onEnter,onExit,cleanup,draw,finish}`.
- `build-win/_deps/bindings-src/bindings/2.2081/GeometryDash.bro`: `GameManager::returnToLastScene`.
- Headers del SDK: `CCTransition.h`, `CCRenderTexture.h`, `CCTexture2D.h`, `CCSprite.h`,
  `CCLayer.h`, `CCNode.h`, `CCGeometry.h` y `geode::Ref`/`WeakRef`.

El motor no extrae hijos de escenas, no restaura transformaciones de nodos de GD,
no cambia el dispatcher manualmente y no escribe `m_pRunningScene`.
`CCTransitionScene` conserva la entrada/salida, referencias, finalizacion y cleanup
nativos. Los comandos se aplican a contenedores privados con capturas renderizadas
una vez, centrados para que escala, giro y desplazamiento compartan coordenadas.
Los contenedores `CCLayerRGBA` no dibujan un segundo fondo: se evita blanquear la
imagen al reducir su opacidad. Un fondo negro independiente cubre el area expuesta.
Las capturas congelan la imagen durante los comandos; no pausan la simulacion de GD.
El stinger visita la escena correspondiente al tiempo del corte y dibuja su overlay encima.

La identificacion de transiciones reemplazables compara tipos dinamicos exactos:
una subclase de otro mod no coincide aunque incluya `CCTransition` en su nombre.
Ese filtro se aplica antes de los efectos de entrada/salida de nivel.
Para `popSceneWithTransition`, se crea y retiene la transicion antes de llamar al
`popScene` nativo. No se modifica manualmente la pila ni el puntero de escena actual.
`popScene` sin transicion conserva su funcionamiento original.

Smooth+ mantiene prioridad para entrada sin perfil de nivel explicito y para salida
cuando esta habilitado. Un perfil de entrada de nivel explicito tiene prioridad sobre
Smooth+. La compatibilidad efectiva con otros mods requiere comprobarla dentro de GD.

## Importacion y reproduccion

1. El selector acepta imagenes y videos. El trabajo de disco/decodificacion corre en
   una cola con un solo worker; no utiliza OpenGL en ese hilo.
2. GIF conserva sus frames compuestos y delays. Video usa el decodificador existente
   con un limite de duracion exclusivo para este consumidor; los demas importadores
   conservan su politica. Se usan timestamps acumulados para evitar redondear 60fps a 50fps.
3. Los frames se empaquetan en PNG paginados de hasta 2048 por lado, con un pixel
   extruido alrededor de cada celda. El redimensionado filtra pixeles ya premultiplicados
   con libyuv para evitar bordes de color en zonas transparentes. Se utiliza el blend de
   Cocos correspondiente, incluida la atenuacion RGB cuando cambia la opacidad.
4. Se escribe `animation.pttransition` solamente despues de completar todas las paginas.
   Contiene version, dimensiones y delays. Al cargar se validan dimensiones, numero de
   frames, tiempos, geometria exacta de las paginas y presupuesto RGBA.
5. La clave del cache combina ruta, tamano, fecha de modificacion y version del formato.
   Los nuevos perfiles guardan la ruta del manifiesto; el archivo original se puede
   mover despues de una importacion correcta.
6. La subida de texturas ocurre en el hilo principal al preparar el recurso. Solo un
   resultado decodificado puede esperar su subida; la cola no acumula animaciones RGBA.
7. Durante la transicion se selecciona el frame por tiempo absoluto y se cambia la
   pagina/rectangulo cuando corresponde. No hay decodificacion ni subida por fotograma.
   La reproduccion termina en el ultimo frame, sin loop.

Limites actuales:

- Fuente de hasta 96 MiB; duracion maxima 30 segundos.
- GIF hasta 240 frames, sujeto a un presupuesto de decodificacion de 96 MiB.
  Se rechazan GIF truncados por el limite, sin guardar una animacion parcial.
- Imagen/GIF hasta 1024 pixels por lado al empaquetar; fuentes raster hasta 4096.
- Video hasta 120 muestras repartidas a lo largo de su duracion, hasta 512 pixels de lado.
  La conversion rechaza duraciones/PTS invalidos y tiene un plazo de 45 segundos.
  Los clips largos reducen su frecuencia de muestreo. No se presenta como video original sin perdida.
- Hojas hasta 96 MiB RGBA por medio; cache GPU hasta 192 MiB. Los recursos retenidos por
  escenas, editores o perfiles no se expulsan. Se rechaza una preparacion que exceda el
  presupuesto mientras esos recursos sigan en uso.
- Estos son limites por recurso/cache, no una promesa de consumo total del proceso:
  decodificador, buffers de importacion y capturas tambien consumen memoria.

Si un medio no esta preparado o falla, la navegacion usa fade y solicita la preparacion
para la siguiente transicion. Un archivo faltante no bloquea todas las transiciones
custom de la sesion. Las configuraciones seleccionadas se precargan al cargar/guardar.

## Uso

En Transitions, elegir **Stinger (OBS)** y abrir el boton de configuracion:

- **Importar** prepara imagen, GIF o video.
- **Corte -5%/+5%** elige cuando cambia la escena. La ventana muestra tambien milisegundos.
- **Duracion -0.1s/+0.1s** ajusta la velocidad total; **Original** recupera la duracion del medio.
- **Preview** reproduce el mismo timing sobre dos fondos sin salir de la ventana.
- **Guardar** aplica al perfil en edicion; guardar tambien la ventana principal de Transitions.

El medio se estira al area de la escena. Elegir el corte cuando el overlay cubra el
contenido que se desea ocultar. Una imagen fija permanece durante toda la transicion;
para una entrada/salida progresiva usar un GIF o los comandos de imagen/opacidad.

En el editor de comandos, `Image` importa tambien GIF/video a hojas.
`Spawn` ejecuta los siguientes N comandos en paralelo (N se ajusta con From +/-).
Los delays son relativos al inicio del comando o del grupo. El comando posterior al
grupo comienza cuando termina su miembro mas largo. Los grupos anidados se ignoran;
se recomienda evitar tracks simultaneos que escriban la misma propiedad del mismo
objetivo, donde prevalece el ultimo comando de la lista. Maximo 256 comandos/30 segundos.

El tiempo absoluto evita perder el remanente de un frame al cruzar un comando.
Shake es determinista y vuelve a su posicion de origen. La preview completa hace
push/pop y devuelve el editor con sus cambios; no reconstruye MenuLayer.
Las rutas serializadas UTF-8 se reconstruyen como tales en Windows. Las lecturas
acotan la asignacion incluso si otro programa agranda el archivo despues de seleccionarlo.
Los callbacks usan referencias debiles y copias de configuracion, y la edicion
estructural se bloquea durante una importacion para no asignar el resultado a otro comando.

## Diferencias respecto de OBS

No hay track matte, mezcla de audio ni reproduccion de audio del archivo.
El decodificador actual entrega video YUV sin canal alpha: los videos son opacos.
GIF/PNG si conservan transparencia. No se garantiza que un WebM/MOV transparente de
OBS funcione igual: requiere soporte de alpha en el decodificador y otra composicion.
Los codecs disponibles dependen del backend y de la plataforma.
Los presets nativos y sus aproximaciones historicas siguen disponibles; este cambio
no convierte cada preset en un efecto equivalente de OBS.

## Verificacion

Revision manual de codigo, firmas de bindings, ownership, temporizacion, alpha,
validacion de manifiestos, rutas de fallback y callbacks. `git diff --check` sin errores.
No se completo compilacion ni se ejecuto GD, por indicacion del usuario. No se afirma
validacion visual, rendimiento medido ni compatibilidad probada con otros mods.

Para la futura comprobacion en GD: reemplazo/push/pop, preview y retorno al editor,
entrada/salida de nivel con Smooth+, perfiles global/nivel, GIF con delays variables,
alpha sobre fondos claros/oscuros, cambio de pagina, reinicio con originales movidos,
archivo ausente/corrupto, cierre del popup mientras importa, navegacion repetida y
transiciones de otros mods. Comprobar ademas que onEnter/onExit y cleanup ocurren una
sola vez y que el input se recupera al finalizar/interrumpir.
