# Loading detenido al guardar y jugar

El recorrido revisado es editor -> GJGameLoadingLayer -> PlayLayer, junto con
su regreso al editor, las transiciones globales y personalizadas, los paneles
dinamicos y el watchdog de entrada/salida del nivel.

## Causa y correccion

En GD 2.2081, el destructor de GJBaseGameLayer avisa a GJGameLoadingLayer de
que puede iniciar loadLevel. Conservar la escena saliente conserva el editor
y deja la pantalla Loading esperando ese aviso.

Los registros de boton, retroceso y Hold guardaban WeakRef de la escena.
WeakRefPool de Geode 5.11 retiene el objeto administrado; borrar la referencia
mediante asignacion de movimiento o asignarla a nullptr no equivale a destruir
el handle. El pool puede quedar como unico propietario sin otro handle que
vuelva a comprobarlo. La regresion reprodujo ese bloqueo antes del cambio.

SceneIdentity conserva solo la direccion y el ID de objeto de Cocos. Compara
ambos con una escena actual valida sin retener ni acceder a la escena anterior.
Esto tambien distingue escenas nuevas que reutilizan la misma direccion.
Las protecciones que conservan la navegacion nativa del editor y loading siguen
siendo las responsables de sus callbacks y del cambio de escena.

## Verificacion

- La fixture transition_scene_lifetime ejecuta las funciones reales de boton,
  retroceso y Hold con un modelo del pool de referencias: el editor se destruye
  y se desbloquea el siguiente nivel en los cuatro casos comprobados.
- Pasan las regresiones de Hold, sus ajustes y la vida de sus barras.
- Pasan navegacion nativa editor/loading/gameplay y movimiento de transiciones,
  con ASan y UBSan.
- Pasan 26 ajustes persistentes y la auditoria de 23 hooks, 11 implementaciones
  nativas de show y 380 candidatos de interfaz. Se corrigieron dos supuestos
  antiguos de las pruebas: el nombre del setter de defaults y las rutas del build.
- La DLL de Windows compila con el SDK y los bindings configurados.

El analisis estatico de ocho archivos no encontro nuevos avisos en las funciones
modificadas. Senalo un posible m_pInScene nulo en finishTransition de
LevelEntryEffects.cpp. Su inicializacion rechaza destinos nulos y el flujo
normal mantiene ambas escenas; no se reprodujo ese estado durante esta revision.

La ejecucion visual dentro de Geometry Dash sigue pendiente: guardar y jugar,
volver al editor y repetir el recorrido. La sesion permite generar el paquete
en el proyecto, pero no escribir en la carpeta de mods de Steam.
