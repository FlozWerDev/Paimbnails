# Auditoría del simulador de físicas — 2026-10-31

## Resultado y alcance

Se revisaron los 24 archivos del módulo [editor-physics](../src/features/editor-physics/), su documentación y sus tres baterías de regresión. El recorrido incluye captura y resolución de objetos, geometría y siluetas, integración, colisiones, reposo, joints, campos, muestreo, vista previa, controles, planificación de IDs, generación de triggers, keyframes, reemplazo y eliminación de salidas. También se revisaron los siete archivos de [menu-physics](../src/features/menu-physics/) y su batería de regresión, así como los bindings y los hooks de colaboración relevantes para la exportación.

Hay defectos demostrados en la física, discrepancias entre la vista previa y la salida reactiva y problemas en las operaciones sobre el nivel. Los fallos de colisión dinámica contra dinámica del modo reactivo y la aproximación rectangular de rampas son limitaciones declaradas; se distinguen de los errores nuevos.

Se documentan **13 hallazgos principales del simulador del editor (5 P1 y 8 P2)** y **2 de la física de menús**, además de inconsistencias menores de controles. Las reproducciones ejecutadas y los hallazgos estáticos se identifican por separado.

Esta entrega agrega documentación y reproductores independientes. **No cambia el código del mod, no corrige todavía estos defectos y no compila ni instala un paquete del mod.**

### Tipo de evidencia

- **Ejecutado:** caso C++ ejecutado contra el código actual, con valores observados.
- **Revisión estática:** recorrido concreto del código y, cuando corresponde, del binding de GD. Se indican pasos manuales para validarlo dentro del juego; no se presenta como una prueba realizada en GD.
- **API:** existe en el motor, pero los controles actuales del laboratorio no exponen directamente esa combinación de propiedades.

P1 indica pérdida de coherencia importante, movimiento incorrecto grave, bloqueo del editor o edición sin recuperación normal. P2 indica comportamiento incorrecto más acotado o una combinación específica.

## Hallazgos prioritarios

### F01 · P1 · Un cuerpo puede dormirse en el aire y permanecer sin soporte

**Ejecutado.** [updateSleep](../src/features/editor-physics/services/PhysicsSolver.cpp#L1137) solo comprueba velocidad lineal, giro y tiempo. No exige soporte ni considera la aceleración que sigue actuando. Una vez dormido, el integrador lo omite en [step](../src/features/editor-physics/services/PhysicsSolver.cpp#L1333).

Reproducción: caja en `y=100`, gravedad `-10`, sin arrastre, duración de 3 s. El cuerpo se duerme a los `0.508333 s` y termina en `y=98.6868`. Con reposo deshabilitado termina en `y=54.875`. Esta gravedad es alcanzable desde los controles: gravedad del mundo `-100` y escala del cuerpo `0.1`.

También se comprobó un cuerpo sobre una plataforma cinemática que se desplaza a 20 px/s: el cuerpo duerme y queda en `y=19.95` aunque la plataforma ya llegó a `x=79.9999`. Los contactos dormidos se preservan sin volver a validar su geometría en [preserveSleepingContacts](../src/features/editor-physics/services/PhysicsSolver.cpp#L1165), y el umbral de despertar es de 24 px/s. Esta variante usa la API cinemática.

**Corrección propuesta:** condicionar el reposo al equilibrio/soporte, volver a comprobar los contactos de soporte y propagar el despertar cuando el soporte se mueve o desaparece. Cubrir gravedad baja, movimiento lento y soporte cinemático.

### F02 · P1 · Las piezas de un cuerpo compuesto multiplican la corrección de posición

**Ejecutado.** Cada contacto conserva la penetración medida antes de resolver. [separateConstraint](../src/features/editor-physics/services/PhysicsSolver.cpp#L570) desplaza el cuerpo por esa penetración y el bucle de [corrección](../src/features/editor-physics/services/PhysicsSolver.cpp#L1421) repite el desplazamiento por cada fixture, sin actualizar la penetración tras los anteriores.

Reproducción: caja centrada en `y=19`, semialtura 10, sobre un suelo cuya cara superior está en `y=10`; un paso de 1/120 s, rebote y fricción cero. Se duplica la geometría dentro del mismo cuerpo, como puede ocurrir al capturar decoración superpuesta.

| Fixtures idénticos | Y después de un paso |
|---:|---:|
| 1 | 19.6462 |
| 4 | 21.7725 |
| 20 | 33.1125 |
| 100 | 89.8125 |

La misma superficie física pasa de una corrección inferior a un píxel a un salto de unos 71 píxeles. Es una explicación concreta de saltos y expulsiones de cuerpos compuestos.

**Corrección propuesta:** resolver la posición con separaciones actualizadas y un tratamiento coherente de los contactos de un cuerpo compuesto. Añadir casos con piezas coincidentes, pies separados y decoración superpuesta.

### F03 · P1 · El reemplazo puede dejar dos sistemas controlando el mismo objeto

**Revisión estática.** [matchingEmissionIndex](../src/features/editor-physics/services/PhysicsTriggerEmitter.cpp#L425) devuelve un único índice. Al reutilizar objetos de varias emisiones, [emitToEditor](../src/features/editor-physics/services/PhysicsTriggerEmitter.cpp#L878) elimina solo esa emisión. Las demás conservan sus triggers y grupos de control.

Pasos manuales:

1. Compilar una selección dinámica A.
2. Limpiar las capturas y compilar una selección dinámica C independiente.
3. Limpiar las capturas, capturar A y C como una nueva selección y compilar.
4. Comprobar que solo se elimina una salida anterior y que la otra sigue controlando parte de la selección nueva.

Además, el reconocimiento de salidas depende exclusivamente de [g_history](../src/features/editor-physics/services/PhysicsTriggerEmitter.cpp#L63), en memoria. Al salir y volver a entrar al editor no se reconstruye desde el nivel. Recompilar tras reabrir puede añadir otra salida sobre los mismos objetos; `Quitar último` tampoco recupera ese historial anterior.

**Corrección propuesta:** identificar todas las emisiones que controlan fuentes reutilizadas y reemplazarlas como una operación. Persistir una identidad recuperable de cada sistema, o detectar y advertir explícitamente las salidas ya existentes al reabrir.

### F04 · P1 · Compilar y quitar una salida omite el deshacer nativo

**Revisión estática y binding.** La creación de [keyframes](../src/features/editor-physics/services/PhysicsTriggerEmitter.cpp#L747), [triggers de animación](../src/features/editor-physics/services/PhysicsTriggerEmitter.cpp#L792) y [nodos reactivos](../src/features/editor-physics/services/PhysicsTriggerEmitter.cpp#L825) usa `LevelEditorLayer::createObject(..., true)`. En el [binding 2.2091](../build-clang/_deps/bindings-src/bindings/2.2091/GeometryDash.bro#L14632), ese booleano se llama `noUndo`. La [eliminación](../src/features/editor-physics/services/PhysicsTriggerEmitter.cpp#L212) también usa `noUndo=true` y el emisor no registra una transacción alternativa mediante `addToUndoList`.

Las asignaciones de grupos y parents tampoco se incorporan al historial normal. `Quitar último` es una compensación propia, disponible solo mientras exista la emisión en memoria; no sustituye un deshacer/rehacer completo, especialmente después de reemplazar una salida.

Pasos manuales: compilar, cerrar el popup y pulsar deshacer; repetir con reemplazo y con `Quitar último`, y revisar también los grupos de las fuentes.

**Corrección propuesta:** incorporar creación, eliminación, cambios de grupos y parents a una operación de deshacer/rehacer coherente.

### F05 · P1 · El presupuesto de tiempo no evita pasos que bloquean el editor

**Ejecutado y revisión estática.** El presupuesto se comprueba [después de un paso completo](../src/features/editor-physics/services/PhysicsSolver.cpp#L1559). El constructor del mundo, la creación de siluetas, los bucles de fixtures y las iteraciones del solver no tienen puntos de cancelación. La fase reactiva de [simulateWorkspace](../src/features/editor-physics/services/NativePreview.cpp#L201) tampoco comprueba un plazo.

Medición con `/O2`: dos cuerpos de 500 fixtures coincidentes, presupuesto de `0.01 s`. La llamada tardó `0.315835 s` y recién entonces devolvió `exhausted=true`: aproximadamente 32 veces el presupuesto. El laboratorio permite hasta 16 cuerpos de 1000 objetos; por tanto, un presupuesto de 2 s no es una garantía de respuesta del hilo principal.

**Corrección propuesta:** usar un único plazo para toda la operación y comprobarlo en las fases costosas, incluida la preparación. Limitar trabajo/contactos de escenas degeneradas. La creación de geometría que depende de cocos necesita además un recorrido incremental en el hilo principal.

### F06 · P2 · La prevención de atravesamientos ignora el giro

**Ejecutado.** Los [subpasos](../src/features/editor-physics/services/PhysicsSolver.cpp#L1317) se calculan solo con la velocidad lineal. Una barra cuyo centro está quieto puede mover sus extremos muchos píxeles entre comprobaciones.

Reproducción: barra con semitamaño `(100, 1)`, giro de `12.5663706 rad/s` —720 grados/s, permitido por la interfaz—, un obstáculo pequeño sobre el arco de su extremo a unos 3 grados, y un paso de 1/120 s. El extremo atraviesa el obstáculo entre poses, pero el motor informa `0` impactos y termina con ángulo `0.10472`.

**Corrección propuesta:** incluir la velocidad de los extremos, aproximadamente `|v| + |omega| * radio`, en la elección de subpasos, y cubrir el límite de subpasos con detección continua o una limitación explícita.

### F07 · P2 · Péndulo y Explosión esperan al jugador solo en la vista previa

**Ejecutado sobre el modelo y el grafo.** [nativeNeedsPlayer](../src/features/editor-physics/services/NativePreview.cpp#L173) devuelve verdadero para `useAnchor` y `fragmentObjects`. La [simulación reactiva](../src/features/editor-physics/services/NativePreview.cpp#L208) omite por completo esos cuerpos y la interfaz dice que esperan al jugador.

El grafo generado no tiene esa dependencia:

- [Explosión](../src/features/editor-physics/PhysicsNative.cpp#L367) crea Advanced Follow activados por posición, con velocidad inicial radial de 65 unidades para fuerza 1. La prueba encuentra dos follows automáticos y cero nodos que dependan del jugador.
- [Péndulo](../src/features/editor-physics/PhysicsNative.cpp#L389) crea un follow hacia un ancla fija, con aceleración 15 para fuerza 1, también sin dependencia del jugador.

El error comprobado es la clasificación y la omisión del modelo de previsualización. La trayectoria exacta de Advanced Follow todavía requiere validación en GD.

**Corrección propuesta:** simular el ancla y los fragmentos, o mostrar expresamente que esa previsualización no está implementada. Reservar “espera al jugador” para comportamientos que realmente lo requieran. Corregir la afirmación equivalente de la [documentación nativa](./EDITOR_PHYSICS_NATIVE.md).

### F08 · P2 · La vista previa no gira los sensores con el cuerpo reactivo

**Revisión estática.** Los [sensores del grafo](../src/features/editor-physics/PhysicsNative.cpp#L450) pertenecen al grupo raíz del cuerpo y los [Rotate](../src/features/editor-physics/PhysicsNative.cpp#L572) apuntan a ese mismo grupo. En cambio, la vista previa calcula los sensores una vez y [solo les suma una traslación](../src/features/editor-physics/services/NativePreview.cpp#L145), aunque luego actualiza el ángulo visual del cuerpo.

Un cuerpo rectangular girando conserva sensores en sus lados originales durante la previsualización. El grafo transforma los objetos del grupo. Las caras y los momentos de contacto pueden diferir incluso usando los mismos presets.

Pasos manuales: cuerpo alargado en `Triggers`, giro inicial, pared fija cercana; comparar los sensores exportados y el momento de rebote con la vista previa. Repetir con Rebotador, que usa orientación por dirección.

**Corrección propuesta:** transformar los sensores conforme a la rotación y al pivote reales del grupo, y unificar la geometría usada para exportar y previsualizar.

### F09 · P2 · Los materiales por objeto no llegan a la salida reactiva

**Ejecutado sobre el grafo.** La interfaz permite [editar fricción y rebote por objeto](../src/features/editor-physics/ui/PhysicsBodyPopup.cpp#L299) y [resolve](../src/features/editor-physics/services/PhysicsWorkspace.cpp#L241) los copia a los fixtures. El solver horneado sí los usa.

El [perfil reactivo](../src/features/editor-physics/PhysicsNative.cpp#L168) solo lee la fricción del cuerpo, y los [rebotes exportados](../src/features/editor-physics/PhysicsNative.cpp#L500) solo leen su restitución. Cambiar ambos materiales de un fixture a 1 deja iguales todos los parámetros de movimiento del grafo en la reproducción. Los materiales de las superficies fijas tampoco se mezclan en el rebote reactivo.

**Corrección propuesta:** traducir las propiedades admitidas o deshabilitar/explicar esas opciones en el backend que no puede representarlas. No presentar el ajuste como aplicado cuando se ignora.

### F10 · P2 · Recompilar puede fallar por el décimo grupo que añadió la propia salida

**Revisión estática.** Al reemplazar una salida, se descarta un [grupo preferido asignado anteriormente](../src/features/editor-physics/services/PhysicsTriggerEmitter.cpp#L512), se asigna el nuevo grupo a las fuentes [antes de eliminar la salida anterior](../src/features/editor-physics/services/PhysicsTriggerEmitter.cpp#L665), y solo al final se retiran los grupos viejos.

Pasos manuales: usar una fuente que ya pertenezca a nueve grupos compartidos con otros objetos, de manera que ninguno sea un grupo exacto reutilizable. La primera compilación agrega el décimo. La segunda intenta añadir un undécimo y falla aunque al retirar la asignación anterior volvería a haber espacio.

**Corrección propuesta:** planificar y aplicar el reemplazo con una transacción que permita liberar/reutilizar las asignaciones de la emisión anterior y restaurarlas si falla la generación.

### F11 · P2 · Ser parent de otro grupo impide registrar el parent del grupo nuevo

**Revisión estática.** [makeGroupParent](../src/features/editor-physics/services/PhysicsTriggerEmitter.cpp#L177) retorna si el objeto tiene `m_hasGroupParent`, sin comprobar si es parent del grupo que se está generando. La [selección reactiva](../src/features/editor-physics/services/PhysicsTriggerEmitter.cpp#L885) incluso prefiere un objeto que ya tenga esa marca, pero después el registro se omite por la misma condición.

El estado de parent es por grupo: un objeto puede tener varios parent groups y el propio rollback contempla esa situación. La marca global no basta para decidir si el nuevo grupo ya tiene parent. Esto rompe la garantía del emisor de usar un pivote determinado en cuerpos compuestos; el desplazamiento exacto resultante debe comprobarse en GD.

Pasos manuales: cuerpo compuesto cuyos objetos ya son parents de otros grupos; exportar a un grupo nuevo y revisar `tryGetGroupParent` y el giro.

**Corrección propuesta:** consultar y registrar la relación del grupo concreto, preservando las relaciones anteriores.

### F12 · P2 · Dejar todos los cuerpos fijos bloquea su edición al reabrir el laboratorio

**Revisión estática.** [resolve](../src/features/editor-physics/services/PhysicsWorkspace.cpp#L291) rechaza un workspace sin dinámicos. La [apertura del popup](../src/features/editor-physics/ui/PhysicsPopup.cpp#L217) usa esa misma operación para construir los cuerpos que deben dibujarse y seleccionarse, aun cuando no se pidió simular.

Pasos manuales: capturar únicamente A, cambiar A a fijo desde sus ajustes, cerrar ambos popups y reabrir el laboratorio. No se construye la vista del cuerpo; sin selección ni foco no se llega a sus ajustes para devolverlo a dinámico. Recapturarlo pierde las propiedades configuradas.

Además, el [botón B fijo/dinámico](../src/features/editor-physics/ui/PhysicsPopup.cpp#L438) borra la resolución y reconstruye una vista vacía en lugar de resolver los cuerpos de nuevo.

**Corrección propuesta:** separar la resolución para dibujar/editar de la validación necesaria para ejecutar una simulación.

## Defectos adicionales de la API del motor

### F13 · P2 · fixedRotation no impide un giro inicial

**Ejecutado; API.** [La construcción del estado](../src/features/editor-physics/services/PhysicsSolver.cpp#L1246) conserva `angularVelocity`, mientras `fixedRotation` solo pone a cero la inercia inversa. El integrador sigue sumando esa velocidad al ángulo.

Con `fixedRotation=true`, velocidad angular inicial de 1 rad/s, gravedad y arrastre cero, el ángulo cambia de 0 a `0.999999` en un segundo. La interfaz actual no expone directamente `fixedRotation`.

**Corrección propuesta:** anular el giro inicial y mantener la restricción durante la integración.

## Hallazgos en la física de menús

### M01 · P2 · Un botón dormido sigue flotando cuando se arrastra su soporte

**Ejecutado con los stubs existentes.** En [el integrador del menú](../src/features/menu-physics/services/MenuPhysicsWorld.cpp#L145) se omite el cuerpo dormido. [updateSleep](../src/features/menu-physics/services/MenuPhysicsWorld.cpp#L381) retorna inmediatamente si ya duerme; por tanto, no invalida el reposo al perder soporte.

Dos botones apilados se estabilizan con el superior en `y=53.9908`. Se arrastra el inferior fuera de la pila y se simulan dos segundos más. El superior permanece en `y=53.9908`, dormido, en lugar de caer al suelo.

### M02 · P2 · Cancelar un toque puede activar el botón

**Revisión estática.** [ccTouchCancelled](../src/features/menu-physics/services/MenuPhysicsNode.cpp#L263) llama a `ccTouchEnded`. Ese método [activa el elemento](../src/features/menu-physics/services/MenuPhysicsNode.cpp#L253) cuando no se superó el umbral de movimiento. Una cancelación del sistema, cambio de ventana o interrupción de la interacción puede ejecutar la acción del botón aunque no hubo una liberación válida.

**Corrección propuesta:** al cancelar, liberar el arrastre y limpiar el estado sin activar el elemento.

## Inconsistencias de controles que también conviene corregir

- [toggleMotion](../src/features/editor-physics/services/PhysicsWorkspace.cpp#L302) fuerza la escala de gravedad de B a cero al convertirlo en dinámico. El botón B conserva ese cero; los controles del engranaje y de la barra lateral luego lo fuerzan a uno si es no positivo. La misma operación tiene resultados distintos y puede borrar una gravedad invertida configurada.
- Los tres [controles de lanzamiento](../src/features/editor-physics/ui/PhysicsBodyPopup.cpp#L285) comparten `customLaunch`. Modificar solo Y o el giro convierte los otros valores heredados del laboratorio en los ceros guardados por defecto. Ejemplo: con X global 500, ajustar únicamente el giro elimina también esa velocidad X. Conviene conservar los valores efectivos al iniciar un override o separar las tres propiedades.
- [resetOverrides](../src/features/editor-physics/ui/PhysicsBodyPopup.cpp#L357) conserva la escala de gravedad y todos los ajustes nativos. La ayuda de `Volver al lab` dice que descarta los cambios del cuerpo sin explicar esa excepción.

## Limitaciones reproducidas y verificaciones pendientes en GD

No se contabiliza como un defecto nuevo la ausencia de colisión continua del backend reactivo: la [documentación](./EDITOR_PHYSICS_NATIVE.md) ya delimita ese alcance. Se reprodujo, no obstante, una caja reactiva que parte de `y=200` a `-1500 px/s` sobre un suelo de semialtura 1: termina en `y=-440.75` tras un segundo, aun habiendo registrado tres contactos. Para escenarios que necesiten contacto fiable se requiere el backend horneado o una mejora del modelo nativo.

El [suelo dibujado en la vista previa](../src/features/editor-physics/ui/PhysicsPopup.cpp#L712) es visual; no se incorpora una superficie de colisión en la resolución. Si se captura solo una caja, puede atravesar `y=0`. Conviene explicarlo en la interfaz o hacer coincidir el suelo visible con el físico.

Quedan pendientes pruebas dentro de Geometry Dash 2.2091 de:

| Área | Comprobación necesaria |
|---|---|
| Advanced Follow | Unidades, fricción, modos de follow/edit, orden de activación y orientación por dirección |
| Geometría capturada | Rampas rotadas y volteadas, OBB, escalas no uniformes, objetos extendidos y cambios de sprite |
| Arte | Texto, contadores, glow, detalle, variantes del mismo ID y objetos con datos personalizados |
| Keyframes | Pivote de grupos existentes, vueltas acumuladas, lectura tras guardar y comienzo desde Start Pos |
| Exportación | Deshacer/rehacer, 9–10 grupos, mezcla de sistemas anteriores, reapertura y eliminación parcial manual |
| Colaboración | Permisos por capa, fuentes eliminadas remotamente y sincronización del reemplazo |
| Carga grande | Captura de muchas siluetas, cuerpos muy extensos y escenas cercanas al máximo permitido |

Una batería portable no ejecuta el runtime de GD. No se afirma que estas integraciones hayan pasado una prueba dentro del juego.

## Pruebas realizadas y reproductores

Las cuatro baterías existentes compilaron con Clang 23, advertencias `/W4`, y terminaron con código 0. También se ejecutaron las cuatro con optimización, AddressSanitizer y UndefinedBehaviorSanitizer: código 0 y sin diagnósticos de los sanitizadores.

- [Solver del editor](../tests/editor_physics_regression.cpp).
- [Grafo nativo](../tests/editor_physics_native_regression.cpp).
- [Vista previa mixta](../tests/editor_physics_preview_regression.cpp).
- [Física del menú](../tests/menu_physics_regression.cpp).

Se añadieron [reproductores del editor](../tests/diagnostics/physics_audit.cpp), [reproductor del menú](../tests/diagnostics/menu_physics_audit.cpp) y un [ejecutor independiente](../tests/diagnostics/run_physics_audit.py). No están registrados en la batería habitual: describen comportamientos correctos que el código actual todavía incumple.

Desde la raíz del proyecto:

```powershell
python tests/diagnostics/run_physics_audit.py --measure-budget
python tests/diagnostics/run_physics_audit.py --sanitize
```

El primer comando usa optimización y muestra el tiempo de la escena costosa. El segundo instrumenta los reproductores con AddressSanitizer y UndefinedBehaviorSanitizer. Un código 1 significa que quedan defectos reproducidos; un código 2 indica un problema al compilar o ejecutar. El tiempo de la prueba de rendimiento se informa y no se usa como aserción dependiente de la máquina.

Resultado de los nuevos casos: **8 comprobaciones incumplidas del editor y 1 del menú**. Algunas corresponden a manifestaciones de una misma causa, por lo que no equivalen a nueve causas independientes. La prueba de suelo reactivo se imprime como limitación y no suma un fallo.

Los reproductores instrumentados no produjeron diagnósticos ASan/UBSan; los resultados funcionales siguieron mostrando los defectos. El ejecutor configura códigos de salida diferentes para fallos de los sanitizadores, que se reportan como error de ejecución y no como una simple comprobación funcional incumplida.

## Orden recomendado de corrección

1. Reposo y despertar; corrección de posición de cuerpos compuestos.
2. Transacción de exportación: todas las emisiones afectadas, grupos, parents, deshacer/rehacer y reapertura.
3. Presupuesto de trabajo que abarque preparación, solver y modelo reactivo.
4. Coherencia de la vista previa reactiva: presets autónomos, sensores rotados y materiales admitidos.
5. Giro rápido, defectos de la API y recuperación de estados desde la interfaz.
6. Verificación dentro de GD, guardado/reapertura y colaboración; convertir cada reproductor resuelto en una regresión normal.
