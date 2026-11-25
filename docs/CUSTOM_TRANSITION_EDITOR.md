# Editor de transiciones custom

`Transitions > Custom (Studio) > Open` abre el editor a pantalla completa.
`Stinger / OBS > Open` abre el mismo editor y convierte el medio existente en
una pista, conservando su duracion y punto de corte. Aplicar convierte el
resultado en una transicion Custom.

- **Guias:** el lienzo muestra dos recuadros rotulados Escena A y Escena B.
  Los medios se muestran semitransparentes para que una imagen que ocupa toda
  la pantalla permita ver ambas escenas al editar. Los valores guardados no
  cambian. Los botones Escena A/B eligen el destino de nuevos triggers.
- **Final:** muestra la composicion con su opacidad real. La marca amarilla de
  `ESCENAS A / B` cambia de A a B cuando el corte esta activado. El boton Guias
  permite volver a las dos escenas. Play activa la vista final automaticamente;
  al terminar vuelve a Guias y oculta los objetos cuyas pistas ya terminaron.
- **+ Imagen:** importa imagenes, GIF o video usando el conversor de medios.
  **+ Bloque** crea un rectangulo de color. Selecciona el objeto en el lienzo
  o en su pista para cambiar nombre, posicion, tamano, alpha y medio.
- **Linea de tiempo:** arrastra un bloque para cambiar su inicio, o su borde
  derecho para cambiar la duracion. Arrastra sobre la regla para recorrer el
  tiempo y ver los efectos de inmediato. Las flechas muestran las siguientes
  cuatro pistas.
- **Tiempos exactos:** escribe segundos en `INICIO` y `DURACION` del panel.
  Todos los inicios se miden desde el comienzo de la transicion; los clips
  pueden superponerse. La duracion maxima es de 30 segundos.
- **Triggers:** Move anima posiciones; Alpha permite llegar a opacidad cero;
  Rotate anima grados; Follow Cursor sigue el cursor durante su intervalo y
  despues conserva la ultima posicion observada. Tambien hay Scale, Color,
  Shake, Skew, Color Wash y curvas de easing.
- **Objetos:** cada objeto aparece durante el intervalo de su propia pista.
  Sus triggers no lo hacen aparecer fuera de ese intervalo. Al borrar un
  objeto tambien se borran sus triggers. Duplicar o reordenar pistas conserva
  las referencias a objetos.
- **Proyecto:** permite ajustar duracion y corte, cargar presets e importar o
  exportar JSON. Los archivos multimedia se referencian por su ruta local;
  el JSON no incluye sus bytes.
- **Play / Inicio / Vista real:** reproducen en el lienzo, vuelven al inicio
  o prueban el render real con regreso automatico al editor.
- **Aplicar** devuelve el proyecto a la configuracion. Pulsa **Save** en
  Transitions para guardar la configuracion en disco.

La previsualizacion y la transicion real usan `TransitionEvaluator`. La
regresion `custom_transition_regression` comprueba tiempos absolutos, efectos
simultaneos, vida de objetos, cursor y referencias. La fixture
`custom_transition_storage` comprueba el guardado y la lectura usando las
funciones de produccion, incluidos proyectos anteriores.

`custom_transition_input` ejecuta los callbacks reales de clic, arrastre,
redimensionado y reproduccion con un dispatcher de prueba. Comprueba la
prioridad del lienzo frente a controles y modales, el corte A/B, la posicion
del panel al escalarlo y que los botones fuera del lienzo conserven sus clics.
`custom_transition_preview_regression` comprueba que las guias revelen ambas
escenas sin alterar la composicion usada por el juego. Estas pruebas no
sustituyen la comprobacion visual e interactiva dentro de Geometry Dash.
