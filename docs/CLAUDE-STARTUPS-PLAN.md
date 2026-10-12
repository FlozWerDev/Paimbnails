# Paimbnails: implementación pendiente y candidatura a Claude Startups

Fecha de revisión: 9 de octubre de 2026.

La web presenta el producto existente y una propuesta de integración con Claude. Esta mejora de presentación no garantiza admisión: Anthropic decide cada solicitud y los beneficios disponibles pueden cambiar. Las integraciones con Claude siguen pendientes de implementación.

## 1. Cambios realizados en la página

- [x] Presentar Paimbnails como herramientas de descubrimiento y creación para Geometry Dash.
- [x] Explicar a quién sirve: jugadores que buscan vistas previas y creadores que necesitan edición, importación de imágenes y colaboración.
- [x] Añadir una sección para creadores con funciones existentes: plantillas Wave/Stamp, conversión de imágenes/GIF y editor colaborativo.
- [x] Dar acceso directo a los experimentos interactivos existentes del paper de imagen a objetos.
- [x] Añadir una hoja de ruta con tres propuestas: copiloto del editor, asistencia a moderadores y soporte basado en documentación.
- [x] Identificar esas propuestas como planeadas y aclarar que la versión actual no incluye Claude.
- [x] Presentar el proyecto, al desarrollador público y los contactos existentes, sin inventar una sociedad, una fecha de fundación o un correo operativo.
- [x] Explicar el apoyo por donaciones/VIP y los servicios opcionales para creadores que se podrían explorar.
- [x] Describir las cifras como una muestra del ranking público, con su metodología.
- [x] Añadir estados de carga, datos no disponibles, actualización de cifras y un límite de espera de ocho segundos.
- [x] Añadir navegación hacia las secciones nuevas, acceso por teclado y contenido disponible sin JavaScript.
- [x] Añadir descripción del producto, URL canónica y datos estructurados sin cifras o valoraciones comerciales inventadas.
- [x] Traducir los contenidos nuevos al español, inglés y alemán.

Archivos de la web modificados:

- `/home/fernando/Descargas/paimon-thumbnails-server/src/paimon-thumbnails-server.js`: únicamente la plantilla `homeHtml`, incluidos los estilos y el JavaScript de la página.
- `/home/fernando/Descargas/paimon-thumbnails-server/public/request-assets/papers/site-translations.js`: traducciones adicionales.
- `/home/fernando/Descargas/paimbnails-site-design/home.html`: copia de la página actualizada.

No se implementaron integraciones con Claude, cambios del mod ni endpoints nuevos. No se creó ni envió una candidatura. La publicación del sitio actualizado está pendiente.

## 2. Condiciones publicadas y comprobaciones necesarias

El anuncio del 6 de octubre describe como elegibles para postular a startups fundadas en los últimos cinco años **o** financiadas en los últimos dos. La información publicada con ese lanzamiento también pedía una cuenta de Claude Console, correo empresarial del mismo dominio que el sitio y una descripción breve del producto.

La página actual del programa informa que está sobrepasada la capacidad de las ofertas de un año de Claude Team y US$1.000 de API, y que se están revisando las solicitudes. Confirma que el programa admite startups autofinanciadas. Verificar las condiciones dentro de Console antes de presentar la solicitud.

Los términos publicados mencionan actividad del negocio, inversión/financiación e integración o uso de Claude como factores de evaluación. Las recomendaciones y objetivos de este documento no son requisitos oficiales adicionales.

| Comprobación | Acción pendiente | Evidencia |
| --- | --- | --- |
| Fecha de inicio | Confirmar cuándo comenzó realmente el proyecto o negocio y qué fecha corresponde declarar. | Historial de versiones, documentos y registros reales disponibles. |
| Identidad del solicitante | Determinar quién presenta la solicitud y el nombre real que corresponde usar. | Datos coherentes entre Console, web y documentos. |
| Correo del dominio | Configurar una dirección real en `@flozwer.org`, comprobar recepción y respuesta y mostrarla en la página. | Correo operativo y control del dominio. |
| Claude Console | Crear o verificar la organización que hará la solicitud. | Acceso a la organización y al formulario vigente. |
| Disponibilidad y políticas | Comprobar los países de operación y de los usuarios de la futura integración, y las políticas aplicables de Anthropic. | Alcance del servicio definido y formulario completo. |
| Descripción del producto | Preparar una descripción que distinga lo publicado del trabajo futuro. | Demo, enlaces públicos y uso previsto de Claude. |
| Beneficios disponibles | Confirmar las ofertas vigentes al presentar la solicitud. | Información mostrada por Anthropic en ese momento. |

No inventar financiación, ingresos, socios, clientes, fechas ni vínculos con Anthropic. Tampoco identificar al equipo de moderación como empleados sin confirmar esa relación. La necesidad de constituir una sociedad no quedó establecida por la información pública consultada; comprobar lo que pida el formulario antes de asumirla como condición.

## 3. Prioridad 0: dejar la web verificable y publicarla

- [ ] Revisar las afirmaciones sobre las funciones existentes contra la versión que se distribuye actualmente.
- [ ] Verificar el funcionamiento del editor colaborativo y sus condiciones de acceso.
- [ ] Añadir nombre/contacto del responsable y fecha de inicio cuando estén confirmados.
- [ ] Añadir el correo del dominio una vez que esté funcionando.
- [ ] Grabar una demo breve dentro del juego: aplicar una plantilla, importar una imagen y mostrar el resultado editable. El paper interactivo actual demuestra la conversión, pero no demuestra un copiloto con Claude.
- [ ] Publicar esa demo en una URL accesible y añadirla a la sección para creadores.
- [ ] Publicar la plantilla y las traducciones mediante el mecanismo existente del sitio.
- [ ] Comprobar en producción `/`, `/paimbnails`, descargas, galería, normas, donaciones y papers.
- [ ] Confirmar que los assets nuevos de traducción se actualicen junto con el HTML y que las cachés no sirvan versiones incompatibles.
- [ ] Revisar móvil, teclado y los tres idiomas después de publicar.

La web no debe afirmar que Paimbnails está aceptado en Claude Startups, que está asociado con Anthropic o que dispone de una integración que todavía no existe.

## 4. Prioridad 1: medir uso real

La API actual de mejores miniaturas contiene una clasificación limitada a 100 entradas. Su `total` representa el tamaño de esa clasificación, no todas las miniaturas del servicio. Los autores, valoraciones y votos calculados a partir de esas entradas pertenecen a esa muestra. La página actualizada explicita ese alcance.

Implementar mediciones independientes antes de mostrar totales del proyecto:

- [ ] Contar miniaturas aprobadas y niveles con vistas previas con una fuente que cubra el servicio completo.
- [ ] Contar autores únicos con un identificador estable; no usar solamente nombres visibles si pueden cambiar.
- [ ] Definir usuario activo y creador activo: acción que cuenta, identificador, período y deduplicación.
- [ ] Medir usuarios/creadores activos por semana y mes con la mínima información necesaria.
- [ ] Medir uso de plantillas, importaciones y colaboración, cuando exista una forma apropiada de hacerlo.
- [ ] Separar visitas a la página, descargas, instalaciones y usuarios activos. Cada cifra debe tener su propia definición.
- [ ] Publicar un resumen agregado con período, fecha de actualización, fuente y límites.
- [ ] Añadir una instantánea verificable para visitantes sin JavaScript y fallos de la API. Mostrar su fecha y antigüedad; evitar que parezca información en vivo.
- [ ] Mostrar crecimiento solamente cuando existan períodos comparables.
- [ ] Publicar testimonios con autorización y sin atribuir ahorros no medidos.

Posible contrato futuro para estadísticas públicas: valores agregados, inicio/fin del período, fecha de cálculo, definición de cada métrica y estado de la instantánea. No publicar datos individuales de usuarios para demostrar actividad.

## 5. Prioridad 1: piloto de un copiloto del editor

Objetivo: que un creador pueda describir una operación limitada usando sus propias plantillas y recibir una propuesta editable con vista previa.

Ejemplo: «Decora esta selección con mi plantilla, en azul y violeta, con un máximo de 500 objetos».

### Alcance inicial

- [ ] Elegir dos o tres operaciones existentes que puedan representarse de manera precisa: seleccionar plantilla, ajustar paleta y aplicar a una selección.
- [ ] Documentar qué entradas necesita cada operación y qué combinaciones admite.
- [ ] Crear casos de evaluación a partir de pedidos reales de creadores.
- [ ] Mantener fuera del primer piloto la generación libre de niveles completos y operaciones difíciles de validar.

### Implementación

- [ ] Crear un servicio autenticado para llamar a Claude. Guardar la clave de Anthropic en el servidor.
- [ ] Definir un contrato de operaciones permitidas con tipos, rangos, identificadores y límites de objetos.
- [ ] Enviar únicamente el contexto necesario: plantillas del usuario, parámetros y datos mínimos de la selección. Confirmar qué contenido acepta compartir el creador.
- [ ] Pedir a Claude una propuesta estructurada compatible con ese contrato.
- [ ] Validar la respuesta en el servidor y nuevamente antes de ejecutarla en el cliente. Rechazar operaciones desconocidas y valores fuera de rango.
- [ ] Resolver la geometría mediante las herramientas existentes de Paimbnails.
- [ ] Mostrar una vista previa con alcance y cantidad de objetos antes de aplicar.
- [ ] Pedir confirmación y conservar la posibilidad de deshacer la edición.
- [ ] Resolver cancelación, errores, respuesta inválida y falta de conexión sin alterar la selección original.
- [ ] Establecer cuotas, límites de tamaño y tiempo, y un presupuesto máximo por usuario y por piloto.
- [ ] Registrar resultados y costos de forma agregada, evitando guardar niveles o pedidos privados por defecto.

### Evaluación y lanzamiento

- [ ] Reclutar inicialmente cinco creadores como objetivo práctico de investigación, no como requisito de Anthropic.
- [ ] Comparar la misma tarea con y sin el asistente.
- [ ] Medir tiempo, propuestas aceptadas, correcciones, errores y costo por tarea terminada.
- [ ] Documentar casos donde el asistente falla o no aporta una mejora.
- [ ] Elegir el modelo mediante pruebas de calidad, latencia y costo con las tarifas vigentes.
- [ ] Definir qué resultados justificarían ampliar el piloto antes de examinar los datos.
- [ ] Grabar una demo real del flujo completo cuando exista.
- [ ] Actualizar la web con el estado y el alcance reales del piloto. Mantener las demás propuestas como planeadas.

Flujo propuesto: interfaz del creador → servicio autenticado → Claude → validación de la propuesta → motor existente → vista previa → confirmación → edición reversible.

## 6. Prioridad 2: asistencia a moderadores

Objetivo: ayudar a detectar posibles incumplimientos de las normas de miniaturas y reducir el tiempo de revisión humana.

- [ ] Preparar ejemplos representativos con decisiones de moderadores y permisos para su uso.
- [ ] Definir las categorías a evaluar a partir de las normas publicadas: interfaz superpuesta, textos y contenido ajeno al nivel, entre otras.
- [ ] Probar la capacidad de Claude con casos ambiguos; una imagen aislada puede no permitir verificar fidelidad a un nivel.
- [ ] Generar una sugerencia explicada para el moderador, con posibilidad de abstenerse.
- [ ] Mantener aprobación, retirada y sanciones bajo decisión humana.
- [ ] Medir falsas alarmas, problemas no detectados, desacuerdos y tiempo de revisión.
- [ ] Documentar qué imágenes se envían a Anthropic y bajo qué condiciones.
- [ ] Integrar el piloto en el centro de verificación cuando los resultados justifiquen hacerlo.

No prometer detección perfecta, aprobación automática o sanciones automáticas como resultado del piloto.

## 7. Prioridad 2: soporte basado en documentación

- [ ] Reunir guías de instalación, preguntas frecuentes, notas de versiones y problemas conocidos.
- [ ] Identificar versión, plataforma y fecha de cada documento.
- [ ] Implementar recuperación de documentación relevante para la consulta antes de responder.
- [ ] Mostrar enlaces a las fuentes utilizadas y aclarar cuando no haya información suficiente.
- [ ] Ofrecer una vía de contacto con el equipo para casos no resueltos.
- [ ] Evaluar con problemas reales de instalación y configuración.
- [ ] Medir consultas resueltas, respuestas incorrectas y derivaciones a soporte.
- [ ] Evitar que una respuesta sugiera descargar ejecutables de fuentes no verificadas o compartir credenciales.

Este piloto puede ejecutarse antes del copiloto si resulta más fácil probar una utilidad concreta con los recursos disponibles.

## 8. Prioridad 1: confianza, privacidad y sostenibilidad

- [ ] Identificar los datos que ya procesa el sitio/mod y los que procesaría cada integración con Claude.
- [ ] Publicar una política de privacidad que describa el funcionamiento real: responsable, datos, finalidad, proveedores, conservación, contacto y solicitudes sobre datos.
- [ ] Explicar antes del envío qué contenido del creador se compartirá con el proveedor de IA.
- [ ] Implementar las opciones de control y eliminación que se prometan en esa política.
- [ ] Revisar las condiciones de uso, las reglas de contenido y la autorización necesaria sobre imágenes y niveles enviados.
- [ ] Documentar acceso y protección de la futura integración con IA, incluyendo las condiciones aplicables a menores si forman parte del público.
- [ ] Confirmar las condiciones del VIP existente y describirlas con claridad.
- [ ] Calcular costos reales de almacenamiento, tráfico, soporte e IA.
- [ ] Entrevistar a creadores sobre servicios opcionales que pagarían: asistencia con IA, colaboración alojada u otras necesidades confirmadas.
- [ ] Validar interés o pagos reales antes de publicar precios, clientes o ingresos.
- [ ] Definir cuotas y condiciones para que el servicio pueda sostenerse después de cualquier crédito promocional.

Los servicios de pago son una hipótesis a validar. El mod sigue presentado como gratuito; no se implementaron cobros ni suscripciones nuevas.

## 9. Preparar la candidatura

- [ ] Verificar las condiciones vigentes y los beneficios disponibles en Claude Console.
- [ ] Completar la identidad, el correo, el dominio y la fecha con datos reales.
- [ ] Preparar una descripción breve del problema, del cliente y del producto.
- [ ] Explicar el uso actual de Claude, si existe, por separado del uso planeado. Usar Claude para desarrollo y usar su API en el producto son actividades diferentes.
- [ ] Adjuntar enlaces a la web, versiones públicas, documentación y demo real.
- [ ] Incluir métricas con período y metodología; omitir las que todavía no se midan.
- [ ] Explicar el piloto, las decisiones técnicas principales y los resultados de evaluación disponibles.
- [ ] Describir para qué se usaría el apoyo de Anthropic y cómo se sostendría el servicio después.
- [ ] Revisar consistencia entre web, formulario y evidencias.
- [ ] Guardar copia de la solicitud y de la respuesta. Si se pide información adicional, responder con evidencia verificable.

Plantilla para adaptar con datos confirmados:

> Paimbnails develops level discovery and creator tools for Geometry Dash. Our published product includes community thumbnails, reusable decoration templates, image-to-object conversion and collaborative editing. We are exploring a Claude-powered assistant that translates creator requests into validated operations using our existing tools, with previews and user confirmation. [Add verified usage, project start date and actual pilot status.] We seek support to evaluate task quality, creator time saved and inference costs, and to develop a sustainable optional service for creators.

Eliminar los corchetes antes de presentar el texto. Cambiar «exploring» por una descripción del piloto implementado solamente cuando corresponda.

## 10. Orden recomendado

1. Publicar la página, confirmar identidad/correo/fecha y grabar la demo del producto existente.
2. Definir y medir actividad real, y conversar con creadores sobre tareas repetitivas.
3. Implementar y evaluar un piloto acotado con Claude, incluyendo controles de datos y costos.
4. Añadir resultados verificables a la web y preparar la candidatura.
5. Ampliar a moderación y soporte solamente cuando el trabajo anterior y sus resultados lo permitan.

## 11. Validación de la página

- [x] Sintaxis del archivo del servidor y del diccionario de traducciones.
- [x] Confirmación de que el código fuera de `homeHtml` no cambió.
- [x] Renderizado y cambios de idioma en español, inglés y alemán.
- [x] Presentación en escritorio, tableta y móvil: anchos de 360, 390, 768, 1024 y 1440 píxeles en los tres idiomas.
- [x] Métricas con una muestra conocida, sin interpretar `total` como todas las subidas.
- [x] Ranking vacío, API fallida, respuesta inválida y tiempo de espera agotado.
- [x] Actualización de cifras y estados traducidos.
- [x] Navegación interna, enlace para saltar al contenido con teclado, contenido sin JavaScript y carga del índice de papers.
- [ ] Publicación y comprobaciones en flozwer.org.

Las comprobaciones locales usan datos controlados para validar el comportamiento. No representan cifras reales de producción ni resultados de los pilotos propuestos.

## Fuentes oficiales

- [Programa Claude Startups y estado actual de las ofertas](https://claude.com/programs/startups).
- [Anuncio de ampliación del 6 de octubre de 2026](https://claude.com/resources/articles/were-expanding-the-claude-startups-program-to-help-founders-build).
- [Términos públicos y factores de evaluación](https://www.anthropic.com/startup-program-official-terms).
- [Formulario en Claude Console](https://platform.claude.com/offers/startups-application).

Las condiciones deben comprobarse de nuevo al postular. Las secciones técnicas, las prioridades y los tamaños propuestos para el piloto son decisiones de implementación, no promesas de aceptación ni requisitos impuestos por Anthropic.
