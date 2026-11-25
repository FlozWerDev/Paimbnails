# Texture Live: pintado e interfaz

El runtime ahora recorre las claves reales de la cache de Cocos y conserva todas
las instancias vinculadas a una misma textura. Una carga tardia mediante
`addUIImage` registra directamente su clave. La vinculacion compara la ruta
resuelta y las dimensiones para evitar usar la mascara de otro archivo.
El dibujo comprueba el contador de entradas de cache una vez por frame para
detectar tambien inserciones asincronas. Si cambia, programa un recorrido
posterior, fuera del dibujo. Los nombres y rutas tienen indices de busqueda;
subir una mascara ya no provoca otro recorrido completo de la cache.

La clasificacion de UI agrupa las sombras de un mismo tono en el relleno
principal y permite pintar controles blancos o grises clasificados solo como
brillo. Este ajuste se usa en Texture Live; el clasificador de exportacion
conserva su comportamiento.

El shader combina los roles como una particion ponderada, normaliza el alpha
de las mascaras y conserva el color de vertices en las regiones excluidas.
La intensidad cero conserva el render original. El tinte cromatico de vertices
solo se sustituye en la parte pintada; se conserva su brillo para los estados
de pulsacion y deshabilitado.

El editor tiene controles desplazables, cabecera separada del nombre del slot,
preview adaptado al espacio disponible y acciones Show Original / Show Painted.
Los controles del editor usan fondos independientes de las texturas pintadas.
Reanalyze Textures cancela los resultados pendientes y vuelve a recoger las
fuentes y construir sus mascaras, aunque los parametros no hayan cambiado.
Configuracion incluye acceso directo al estudio; la biblioteca centra sus
tarjetas y separa las acciones del titulo.

## Rendimiento y memoria

Solo se analizan hojas/texturas que se intentan dibujar con un shader compatible.
Una sola tarea procesa una fuente; no empieza la siguiente hasta que termina
la subida de la anterior. Cambiar parametros cancela la generacion vieja sin
abrir trabajadores simultaneos. El atlas decodificado y las mascaras temporales
se destruyen en segundo plano; como maximo queda un resultado empaquetado
pendiente de subida al hilo principal. La primera entrada a una pantalla puede
mostrar colores originales mientras termina su analisis; despues se reutiliza
la mascara.

La cache de mascaras usa un presupuesto orientativo de 32 MiB en movil y 64 MiB
en escritorio. Al superarlo, descarta primero las mascaras sin uso durante
120 frames. Conserva las visibles/recientes aunque superen el presupuesto para
evitar parpadeos y ciclos de reconstruccion. Una tarea diferida revisa tambien
el exceso aunque no se suban nuevas mascaras. Desactivar la funcion libera
mascaras y referencias a texturas originales. No se reduce la resolucion ni
la precision de los roles.

Las 12 hojas UI explicitas de los recursos locales requieren 11,97 MiB de
mascaras RGBA8888 normales, 46,82 MiB en HD y 187,49 MiB en UHD si se generan
todas. Son tamanos calculados desde sus dimensiones, no mediciones de memoria
residente ni una garantia de ahorro concreto por dispositivo.

Los 17 uniforms y los tres shaders compatibles se buscan al crear el programa.
La paleta se envia cuando cambia; el estado de alpha y la escala de mascara
solo se actualizan cuando cambia la textura correspondiente. Intensidad cero
omite el shader. El fragment shader calcula una vez la luminosidad del pixel,
comparte sus factores entre roles y evita calcular roles ausentes y regiones
sin pintar. El editor evita reconstruir textos de estado que no cambiaron.

Se consulta el limite real de tamano de textura de la GPU antes de subir las
mascaras, manteniendo un limite de 4096 por dimension para acotar cada fuente.
La compatibilidad y los tiempos reales en dispositivos modestos todavia
requieren una compilacion y mediciones dentro del juego.

## Validacion sin compilar

```powershell
python tests/live_slot_shader_regression.py --static-only
node tools/audit_settings.mjs
```

La primera prueba comprueba contratos de integracion y ocho casos del modelo
de referencia en CPU: regiones excluidas, intensidad cero, mezcla de roles,
alpha, alpha premultiplicado, color de vertices, detalle y brillo desactivados.
No ejecuta el C++ ni el shader en GPU. Se comprobaron tambien los limites
geometricos del layout para 427x240, 480x320, 568x320, 640x360 y 854x480.
Los contratos de rendimiento comprueban la separacion entre dibujo y trabajo
diferido, indices de rutas/nombres, uniforms cacheados y carga bajo demanda.

La fixture `live_slot_work_budget.cpp.in` extrae las funciones reales de
solicitud, invalidacion y descarte. Cubre solicitudes repetidas, fuentes ocultas,
cancelaciones, generaciones antiguas, liberacion al desactivar, descarte por
antiguedad, proteccion de mascaras visibles y vuelta del contador de frames.
Queda preparada para la suite nativa con ASan/UBSan; no se compilo ni ejecuto
durante esta revision por la indicacion de no compilar.

No se compilo, empaqueto ni instalo el mod para este cambio. Quedan pendientes
la compilacion y la comprobacion visual dentro del juego: aplicar un slot,
abrir menus con botones de varios colores, alternar la comparacion, editar
mascaras, reanalizar sin cambiar parametros e importar/exportar un slot.
