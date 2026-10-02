# Dynamic Transition

Abre los layers de navegacion como una app: el boton pulsado se convierte en una
tarjeta que crece hasta llenar la pantalla. El contenido aparece mientras el fondo
se oscurece y se aleja. Al volver, el layer se recoge hacia el punto de apertura.

Se activa de forma predeterminada. El interruptor independiente aparece en los
ajustes de Geode y en Modulos > Global > Motion.

## Configuracion

Abrir **Paimon Hub > Extras > Dynamic Transition**. Tambien hay accesos desde
**Smooth UI** y desde **Configuracion de fondos > Extras**. Los cambios se guardan
al modificarlos, sin reiniciar.

| Apartado | Opciones |
| --- | --- |
| Estilo | 9 presets: Equilibrado, Rapido, Sedoso, Resorte, Cinematico, Hoja, Empujar, Revelar y Minimo. 8 estilos: App, Tarjeta, Deslizar, Zoom, Empujar, Hoja, Fundido y Revelado circular. Origen en boton, centro, abajo, izquierda, derecha o arriba; esquinas, fusion del boton, sombra, oscurecimiento y profundidad del fondo. |
| Ritmo | Duraciones independientes de apertura y regreso; 6 curvas: fluida, resorte, suave (S), lineal, exponencial y enfatizada. Fuerza del rebote y sincronizacion con Smooth UI. |
| Alcance | Regreso animado, Escape/Volver de Android, navegacion originalmente instantanea y aperturas solo desde botones. Fundido corto o cambio instantaneo con movimiento reducido. Calidad Alta, Equilibrada o Rendimiento. |
| Paneles | Estilo independiente o igual al de los layers. Popups del juego, desplegables, pausa y dialogos; controles separados para editor, niveles y popups de otros mods. |

La columna izquierda mantiene una **vista previa** del mismo compositor de la
transicion real y una grafica de la curva elegida, incluido el rebote del resorte.
**Abrir** y **Volver** reproducen una animacion; **Bucle** alterna apertura y regreso;
**Lento** reduce la velocidad al 30%. Cambiar un ajuste reinicia la vista previa.
La pestana Paneles muestra un popup sobre la escena de ejemplo.

**Pantalla completa** captura la pantalla actual y reproduce apertura y regreso
desde ese boton. Tocar durante la pausa inicia el regreso; Escape o Volver cierra
la prueba. Esta prueba y la miniatura permiten revisar el efecto aunque el modulo
este desactivado. **Restaurar** repone los valores predeterminados. La interfaz
esta disponible en espanol e ingles.

Los presets cambian el movimiento y la apariencia, conservando el interruptor,
el alcance, el origen, el estilo de paneles y la calidad. El indicador muestra
Personalizado si los valores ya no coinciden con un preset.

La sincronizacion con Smooth UI aplica su velocidad general y fuerza del
movimiento. Su preset Apagado tambien desactiva Dynamic Transition; los otros
presets lo activan. El interruptor general de Smooth UI controla la sincronizacion;
Dynamic Transition mantiene su propio interruptor.

## Navegacion y compatibilidad

- Se admiten `replaceScene`, `pushScene`, `popSceneWithTransition` y `popScene`.
- Las pantallas de carga, gameplay y editor conservan sus transiciones existentes.
- Las subclases de transiciones ajenas se respetan. En menus, Dynamic Transition
  tiene prioridad sobre el preset global de Scene Transitions; al desactivarlo se
  usa ese preset.
- Se recuerdan hasta 32 aperturas. Las posiciones se guardan como proporciones
  de la pantalla para que el regreso tolere cambios de tamano.
- El boton se captura antes de ejecutar su callback. Los paneles recuerdan ese
  origen antes de descartarlo. Se admite tambien la apertura aplazada al siguiente
  frame; los controles que no abren nada descartan su origen. Las capturas caducan
  a los 1.2 segundos.
- Sin un boton disponible se usa el origen elegido; Solo desde botones permite
  omitir estas aperturas. Un regreso conocido puede usar el origen recordado.
- Escape y Volver de Android marcan el regreso, incluso sin un historial conocido
  o cuando se vuelve a otra instancia del mismo tipo de layer. Enter, Space y
  otros botones conservan su accion y animan los paneles que abran o cierren.
- La flecha, X, cancelar, aceptar y Escape se cubren por la retirada del panel;
  los handlers originales siguen ejecutando sus callbacks.
- Se admiten las familias `FLAlertLayer`, `GJDropDownLayer`, `CCBlockLayer`,
  `DialogLayer`, `SlideInLayer` y el navegador de niveles en modo superpuesto.
  Pausa, resultados, reintento, opciones, perfiles, comentarios y ventanas de
  triggers quedan incluidos por estas familias.
- Los paneles del editor y de gameplay se animan por defecto y pueden desactivarse
  por separado. Esto no reemplaza sus transiciones de entrada o salida de escena.
- Los popups propios de Paimon siguen configurandose en Dynamic Popups. Los
  popups de Geode de otros mods tienen una opcion independiente, apagada por defecto.

El [inventario de bindings](DYNAMIC_TRANSITION_BINDINGS.md) detalla las 940 clases
revisadas y los 380 candidatos extraidos del `.bro` de Geometry Dash 2.2081 usado
por el build. El [JSON](DYNAMIC_TRANSITION_BINDINGS.json) incluye herencia, metodos,
parametros, lineas y bindings por plataforma. Los controles, objetos del nivel,
texto, particulas y carga conservan sus animaciones nativas.

El efecto anima capturas de ambas escenas y deja el ciclo de vida, entrada y
limpieza en `CCTransitionScene`. Un solo nodo dibuja mallas redondeadas con un
borde suavizado y una sombra gradual, sin visitar las escenas ni usar stencil
durante el movimiento. El stencil se conserva al capturar para respetar los
recortes de los controles originales. El render usa la proyeccion y el framebuffer de
[CCRenderTexture de cocos2d-x](https://github.com/cocos2d/cocos2d-x/blob/cocos2d-x-2.2.3/cocos2dx/misc_nodes/CCRenderTexture.cpp)
con un cambio temporal de matriz; los nodos del layer conservan sus transformaciones.
Las capturas tienen un limite de 2048 pixeles por lado y aproximadamente dos
millones de pixeles por escena. Se conservan solo sus texturas; el framebuffer y
los buffers de captura se liberan al vaciar el pool temporal. Su alpha se sella
para evitar transparencias entre escenas. La captura de destino se aplaza al
siguiente frame y el avance inicial se limita para absorber el coste de captura.
Se omiten el fondo y la sombra cuando la imagen entrante cubre toda la pantalla.
Si falla la captura o cambia el tamano de ventana durante el efecto, la navegacion
se completa.

Los paneles usan capturas antes y despues del cambio sobre la misma escena.
Una imagen de espera evita mostrar el destino antes de iniciar el efecto. Se
agrupan los cambios del mismo frame y se recuerdan hasta 64 paneles mediante
referencias debiles. Los desplegables usan su modo instantaneo nativo mientras
el compositor realiza el movimiento. La apertura de popups evita la elasticidad
nativa duplicada; los hooks de funciones compartidas se instalan una sola vez.
Los toques se bloquean durante el efecto y Escape puede cerrar el panel o volver
durante la apertura. El cierre de escena, la desactivacion, el reset y el apagado
limpian las capturas y el historial. El desmontaje completo de una escena es inmediato.

## Validacion

`python3 tests/dynamic_transition_config_regression.py` comprueba el manifiesto,
lectura/escritura de ajustes, valores de restauracion, enums y accesos al panel
sin compilar el mod. Tambien comprueba que todos los campos de configuracion
se carguen y tengan un control en el popup.

`python3 tests/dynamic_transition_bindings_regression.py` comprueba la disponibilidad
de los hooks en los bindings generados del build, la cobertura de las 11
implementaciones distintas de `show` de popups de Windows, las direcciones
compartidas y la integridad del inventario. No compila ni modifica el build.

`tests/dynamic_transition_motion_regression.cpp` cubre extremos de apertura y
regreso, los 8 estilos, las 6 curvas, los 6 origenes y 5 formatos de pantalla,
incluida la miniatura. Comprueba las capas de escenas y paneles, el alpha de la
fusion del boton, los presets, el resorte, el movimiento reducido, el avance de
frames y las entradas no finitas. No requiere Geometry Dash para probar las
funciones de movimiento; no se ejecuto en esta revision porque requiere compilar.

Antes de publicar, verificar en el juego:

1. Menu > garage, busqueda, configuracion y Paimon Hub: expansion desde el boton.
2. Volver con flecha y con Escape: regreso al origen recordado.
3. Navegacion con escenas apiladas y sin transicion nativa.
4. Cada estilo, curva, preset, calidad y opcion de movimiento reducido en la
   miniatura y a pantalla completa; Abrir, Volver, Bucle y Lento; estilo de paneles.
5. Desactivar el modulo: recuperar el preset de Scene Transitions.
6. Abrir y cerrar opciones, perfiles, comentarios, seleccion de canciones y colores
   con Escape, X, aceptar y cancelar; comprobar los callbacks de confirmacion.
7. Pausar con Escape/Space, reanudar, abrir resultados y reintento; comprobar
   recompensas y regreso al menu. Probar Volver en Android.
8. Abrir ventanas de triggers y colores dentro del editor; verificar cambios,
   cancelaciones, undo y restauracion del foco tras cerrar.
9. Cerrar durante la apertura, encadenar popups y probar cada interruptor del
   apartado Paneles, incluyendo popups de otros mods.
10. Cambiar el tamano de ventana durante la animacion, entrar/salir del nivel o
    editor y restaurar ajustes de fabrica.

La implementacion se reviso sin compilar el mod, siguiendo las instrucciones del
repositorio. La comprobacion visual y la prueba C++ requieren ejecutarse aparte.
