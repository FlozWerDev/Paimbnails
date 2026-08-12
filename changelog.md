# <cy>v1.1.0</c>

![](flozwer.paimbnails2/paim_Paimon.png?height=32) **La actualizacion mas grande hasta ahora.** Mas de <cg>25 funciones nuevas</c>, el <cl>Collab Editor</c> abierto para todos y una tanda larga de arreglos.

---

## ![](frame:GJ_hammerIcon_001.png?height=18) <cy>Editor</c>

- <cl>Musica del Editor</c> (**Ctrl+M**): panel chico arriba a la izquierda para escuchar tu musica mientras construyes. Reproduce, pausa, cambia de cancion, busca en la barra, controla el volumen y arrastra el panel donde quieras. Se calla solo al probar el nivel y vuelve donde iba al salir del playtest. Usa la misma biblioteca que la Musica del Menu.
- <cl>Autobuild</c> (boton en la barra o **Ctrl+B**): decoras una zona una vez y la repites donde quieras.
- Modo <cg>Onda</c>: aprende que pieza puede ir al lado de cual y rellena bloques y decoracion de forma coherente. Modo <cg>Sellos</c>: guarda grupos enteros y suelta uno en cada sitio.
- Construye sobre marcadores (bloques **467**, **143** y **146**), sobre la seleccion o rellenando toda el area.
- Conserva colores, HSV, grupos, capas y orden Z del objeto original, e importa solo los canales de color que se usan.
- Trae <cg>Otra semilla</c>, <cg>Deshacer</c> y <cg>Ver nivel</c> para probar resultados sin cerrar el panel.
- Las plantillas son archivos sueltos en la carpeta del mod (se comparten) y acepta librerias **.tblib** de otros autobuilders.
- <cl>Collab Editor</c>: se acaba la beta cerrada, <cg>ya esta disponible para todos</c>.
- <cl>Collab</c>: sincronizacion rehecha para assets de miles de objetos. Envio con confirmacion y reintentos, verificacion automatica con auto-reparacion de desyncs, y las selecciones gigantes ya se sincronizan al moverlas o rotarlas.
- <cl>Collab</c>: invitaciones de amigos con cartel para entrar de una, chat de voz tipo walkie-talkie con boton de **Mic**, y cada colaborador con su cursor personalizado.
- <cr>Se quitaron los 57 modulos de editor tipo BetterEdit</c> (HideUI, Object Search, Reference Image, Level Backups, Grid Control, Editor History, View Panel...) porque provocaban crashes. El color picker (**Ctrl+G**), la rotacion con **Alt + click derecho**, los filtros de Mis Niveles y Autobuild siguen funcionando.

---

## ![](frame:GJ_garageBtn_001.png?height=18) <cy>Iconos y garage</c>

- <cl>Extras del Kit</c>: un solo boton en el garage junta Iconos Copiados, Tienda, Degradados y Kit del P2. No duplica botones: se los pide prestados al garage y se los devuelve al cerrar.
- <cl>Creador de Iconos</c>: arma tus propios iconos. Pintas cada pieza con color plano, degradado lineal o radial, o una imagen tuya, y el resultado se instala solo en More Icons.
- <cl>Creador de Iconos</c>: galeria de proyectos, historial de cambios, paletas, editor de degradados con vista previa en vivo y opcion de compartir.
- <cl>Tienda de Iconos</c>: iconos de la comunidad desde **iconsgallery.pages.dev**, con buscador, filtros y ficha de cada uno.
- <cl>Icon Gradients</c>: degradados de GPU para tus iconos, en garage, partida, menu, perfiles y comentarios. Set aparte para el P2 (o colores volteados), puntos movibles con el teclado y precarga de shaders para evitar tirones.
- <cl>Separate Dual Icons</c>: el jugador 2 con sus propios iconos, colores, estela y efecto de muerte.
- <cl>My Icon Sets</c>: el boton de carpeta de Copied Icons guarda tu kit completo (los 9 gamemodes, colores, glow, estela y muerte) con nombre, para volver a ponerlo cuando quieras. Hasta **100** sets.
- <cl>Copy Icons</c>: cada icono abre su ficha con zoom. Ves si lo tienes desbloqueado, el porcentaje del logro y como se consigue, con el asset de ![](frame:GJ_moonsIcon_001.png?height=13) lunas, ![](frame:GJ_starsIcon_001.png?height=13) estrellas, diamantes, monedas u orbes y la cantidad exacta.
- <cl>Sonidos de muerte propios</c>: metes tus mp3/wav (de a uno o una carpeta entera) y suenan al morir. Al azar o en orden, sin repetir el mismo dos veces, con volumen y tono.

---

## ![](frame:newMusicIcon_001.png?height=18) <cy>Audio</c>

- <cl>Cancion Dinamica</c>: al darle play la musica <cg>ya no se corta</c>. Se hunde bajo un filtro submarino y sigue sonando mientras se descarga el nivel; solo se apaga cuando el nivel arranca de verdad. Si cierras el popup, sale del agua sola.
- <cl>Cancion Dinamica</c>: panel propio en **Paimon Hub > Audio**. Punto de inicio, que cancion suena si el nivel tiene varias, volumen, fundido y si suena en los niveles oficiales.
- <cl>Cancion Dinamica</c>: el buceo trae 4 estilos (Submarino, Amortiguado, Profundo y Radio) mas modo Personalizado con corte de agudos y graves, reverb y tono, con boton de <cg>Probar</c>.
- <cl>Volumen Dinamico</c>: mide el volumen real de cada cancion (**LUFS**) y evita el salto al pasar de una tranquila a un drop fuerte. Modos Adaptativo, Fijo y Personalizado.
- <cl>Menu Music</c>: buscador de **Newgrounds** dentro del juego. Top 5 semanal, busqueda por nombre o artista, escuchas antes de bajar y la descarga entra directo a tu biblioteca.
- <cl>Menu Music</c>: navegador de la musica del juego <cg>por etiquetas</c>, para buscar por estilo en vez de por ID.
- <cl>Menu Music</c>: 8 efectos de audio (Slow + Reverb, Dreamy, Bass Boost, Nightcore, Underwater, Concert Hall y Lofi) mas <cg>audio espacial</c> con presets, movimiento en orbita o vaiven y un escenario donde mueves de donde viene el sonido.

---

## ![](frame:gj_twitchIcon_001.png?height=18) <cy>Level Requests</c>

- Recibe pedidos de niveles desde tu <cg>pagina web</c> y desde los chats de <cg>Twitch</c>, <cg>YouTube</c>, <cg>Kick</c> y <cg>TikTok</c>. Todas las fuentes caen en la misma cola.
- Comandos configurables (**!req**, **!request**...), limite de cola y overlay para **OBS** en `http://localhost:21680/overlay`.
- Tu pagina es **flozwer.org/request/tu_usuario**, sacada de tu cuenta de Geometry Dash, con botones para copiar el link y abrirlo.
- Para mandarte un nivel hay que registrarse con una cuenta de GD de verdad: Paimon da un codigo, la persona lo comenta en su perfil y el servidor lo comprueba. El nombre que ves en la cola es <cg>una cuenta real</c>.
- Los pedidos de la web traen el recado que escribio la persona y su link de YouTube. Un boton azul abre el mensaje entero junto a quien lo mando, el nivel y su creador.
- La linea de abajo de cada fila (el **@**, la ID y el creador) va en la fuente del juego y mas grande, con el creador en dorado, para leerla mientras transmites.
- La pestana del navegador sale con <cg>el cubo del streamer</c>, dibujado con los sprites del propio juego, asi que varias paginas abiertas se distinguen de un vistazo.
- El boton de la cola se movio del menu principal a la pantalla de busqueda online, junto a los demas filtros. Sigue mostrando el contador.
- <cg>Aviso en pantalla</c> cuando llega un request nuevo, por encima de todo (jugando, en el editor y en el menu). Panel propio con vista previa donde eliges esquina, tamano, duracion, animaciones de entrada y salida, sonido y que datos ensena.

---

## ![](frame:GJ_starsIcon_001.png?height=18) <cy>Cursor</c>

- <cl>Efectos de click</c>: nueva pestana con vista previa donde clickeas y mantienes apretado para probar al momento.
- <cg>20 estallidos</c> al hacer click (onda, impacto, corazones, estrellas, confeti, chispas, fuego artificial, burbujas, nieve, tinta, rayos, circulo magico, pixeles, humo, monedas, notas, petalos, flor, galaxia y bola de fuego), otro distinto al soltar.
- <cg>9 efectos</c> mientras mantienes apretado, <cg>8 reacciones</c> del propio cursor y <cg>10 sonidos</c> con volumen y tono.
- <cg>18 presets</c> listos (Amor, Fiesta, Tormenta, Invocacion, Monedas, Volcan, Retro, Cosmos...) y engranaje propio por efecto: cada uno guarda su tamano y velocidad aparte, y los retoques se conservan aunque cambies de preset.
- Funcionan <cg>en telefono</c>: salen donde tocas la pantalla. Ya no hace falta raton ni cursor personalizado, asi que tambien sirven en PC con el cursor normal.
- <cl>Estela del cursor rehecha de cero</c>: 18 efectos, 6 modos de color (un color, degradado, arcoiris, arcoiris largo, al azar y por velocidad) y 30 presets. Las estelas viejas se convierten solas.
- <cl>Tienda de cursores</c>: busca cursores de **rw-designer** y **custom-cursor.com** sin salir del juego, con vista previa e instalacion en un toque.

---

## ![](frame:GJ_infoIcon_001.png?height=18) <cy>Informacion y navegacion</c>

- <cl>Avisos de Miniaturas Nuevas</c>: cuando alguien publica una miniatura, te entra una tarjeta con los datos del nivel (cara de dificultad, estrellas, monedas, likes y descargas) dibujados <cg>encima de la miniatura recien subida</c>, que hace de fondo. Tocarla abre el nivel.
- <cl>Avisos de Miniaturas Nuevas</c>: <cg>feed en vivo</c>. El juego mantiene una conexion abierta con el servidor, asi que la tarjeta entra <cg>en el momento</c> en que alguien publica, no en la siguiente consulta. Si se cae, reconecta sola y la consulta periodica recupera lo que se haya perdido. Al subir tu una miniatura, la tarjeta sale al instante con la propia respuesta del servidor.
- <cl>Avisos de Miniaturas Nuevas</c>: **11 animaciones de entrada** (deslizar, elastica, espiral, desplegar, voltear, caer, zoom...), **9 de salida** y **4 de reposo**, mas sitio en pantalla, tamano, duracion, oscurecido del fondo, zoom lento de la miniatura, barra de tiempo, brillo, sonido y boton de <cg>Preview</c> para verlo al momento.
- <cl>Paimon Info Suite</c>: paquete nuevo de modulos, cada uno se enciende por separado y con interruptor maestro.
- <cg>Info extendida</c>: tus intentos, saltos, mejores runs y graficos sobre el thumbnail, mas todos los campos ocultos del nivel en pestanas, copiables al tocarlos.
- <cg>IDs a la vista</c> en celdas, comentarios y perfiles, con color, opacidad, posicion y opcion de esconderlos hasta que aprietes **Shift**.
- <cg>Saltar de pagina</c> pasando el tope de 999, <cg>busqueda avanzada</c> con presets guardados y <cg>seguimiento de progreso</c> con mapa de calor de muertes.
- Perfiles de usuarios sin cuenta, herramientas de comentarios (ir a una pagina, ver IDs, estimar fecha exacta), relleno de nombres verdes en blanco y enriquecimiento opcional con **GDHistory**.
- Los modulos que pisan a **BetterInfo** se apagan solos si lo tienes instalado.
- <cl>Mensajes redisenados</c>: bandeja estilo GD con iconos de jugador, contador de no leidos, buscador local, respuesta rapida, atajo al perfil y seleccion de leidos para limpiar en masa.
- <cl>For You</c>: motor de recomendaciones nuevo. Aprende de lo que juegas, te gusta y guardas, mezcla varias fuentes y te dice <cg>por que</c> te recomendo cada nivel. Panel de etiquetas para dirigirlo a mano.
- <cl>Leaderboard Layout</c>: eliges que datos se ven en cada fila del ranking y en que orden, con presets listos.
- <cl>Input Scroll</c>: rueda del raton sobre una casilla numerica para subir o bajar el valor. Con el modificador, decimales de a **0.10**.
- <cl>Search History</c>: popup mas compacto (icono de dificultad, fecha y contador). Tocar una entrada busca directo.
- <cl>Quick Hub</c> redisenado: rueda minimalista con <cg>seleccion por sector</c> (basta apuntar hacia la opcion, ya no hay que acertar el icono). La opcion apuntada crece y estrena aro de color, y el centro te dice que vas a abrir.
- <cl>Quick Hub</c>: al hacer <cg>click derecho</c> en un boton del juego se guarda su direccion completa (ruta de node ids, pantalla, receptor del callback, texto, tag y posicion), asi que <cg>vuelve a encontrarlo aunque otros mods muevan la interfaz</c>. Si no estas en su pantalla, el radial <cg>te lleva alli y lo pulsa</c>.
- <cl>Quick Hub</c>: los botones que no se pueden pulsar en la pantalla actual salen apagados y te dicen donde viven.
- <cl>Quick Hub</c>: popup con pestanas <cg>Activos</c> y <cg>Anadir</c>, vista previa real de la rueda, y los botones capturados se pueden <cg>reeditar o borrar</c>. Al capturar eliges nombre, icono, forma (circulo, cuadrado o suelto) y color.

---

## ![](frame:GJ_sTrendingIcon_001.png?height=18) <cy>Jugabilidad y rendimiento</c>

- <cl>Golden Best</c>: el porcentaje se pone <cy>dorado</c> mientras superas tu record. Tambien en practica, plataformas, start pos y modo test, con color propio si quieres.
- <cl>Rendimiento en Gameplay</c>: panel nuevo que apaga de golpe lo que mas cuesta mientras juegas (efectos de fondo, de gameplay, del jugador, glow, gradientes, shaders, particulas, decoracion, suelo y transiciones), cada cosa por separado.
- <cl>Smooth Level Transitions+</c>: nueva entrada y salida de los niveles con 4 estilos, duracion e intensidad propias y control de que se anima. La salida copia la entrada, tiene su propio estilo o se apaga.
- <cl>Modulos</c>: registro central. Cada funcion del mod tiene un id fijo y un nombre visible, ordenada por donde trabaja, para prenderlas y apagarlas desde un solo sitio.

---

## ![](frame:GJ_colorBtn_001.png?height=18) <cy>Capturas y Texture Studio</c>

- <cl>Explorador de Assets redisenado</c>: vista previa arriba a la izquierda, buscador por ID, contador de objetos ocultos y boton de contraer o expandir todo.
- Las categorias se pliegan tocandolas (antes salian cientos de IDs de golpe), la casilla se pone <co>ambar</c> cuando solo una parte esta oculta, y cada ID trae un boton <cg>Solo</c>.
- <cl>Editar Capas</c>: ahora es un arbol que se pliega, con contador por grupo (3/7), casilla ambar a medias, buscador de rama y vista previa en vivo al lado.
- <cl>Texture Studio Fusion</c>: pegas tus imagenes o GIFs encima de los sprites del juego. Rellenado por color, 3 modos de mezcla, opacidad, escala, giro, espejo y ajuste pixel a pixel, con exportacion animada.
- <cl>GIF a spritesheet</c>: convierte un GIF en un PNG en rejilla mas su JSON de tiempos.

---

## ![](frame:chestIcon_001.png?height=18) <cy>Extras</c>

- <cl>Modly</c>: navegador dentro del juego para los mods de **modly.web.app**, con catalogo, ficha de cada mod, comentarios y perfil del autor.
- <cl>Guia Paimon</c> (Paigorit V1.5): cobertura de todo el mod, frases de problema EN/ES, categorias Editor/Visuales, chips dinamicos y recomendaciones.
- <cl>Guia Paimon</c>: ahora entiende las repreguntas. Si sigues hablando del mismo tema responde sobre el tema anterior en vez de empezar de cero.
- <cl>Gradient Animations</c>: opcion <cg>Custom</c> para armar tu animacion desde cero. Hasta 4 capas con su movimiento, ritmo, fuerza, velocidad y punto de arranque, mas 6 recetas listas (Wobble, Heartbeat, Swirl, Glitch, Liquid y Vortex).
- <cl>Codigo de moderador</c>: nuevo flujo para crearlo o rotarlo verificandolo con un comentario en tu perfil, y boton para copiarlo.

---

## ![](frame:GJ_optionsBtn_001.png?height=18) <cy>Correcciones</c>

- <cl>Cancion Dinamica</c>: ya no se cuela un trozo de musica de menu por encima de la cancion del nivel al entrar.
- <cl>Level Requests</c>: arreglado el descentrado de las caras de dificultad y los brillos de featured, epic, legendary y mythic en la pagina web. Los PNG venian recortados al ras, asi que la pagina perdia el margen que usa el juego. Ahora comparten lienzo y se apilan igual que en el juego, y las dificultades guardan su tamano real entre si.
- <cl>Texture Studio</c>: arreglados los sprites descentrados en los packs exportados. Las hojas guardan algunos sprites girados 90 grados y el exportador los recortaba con el ancho y el alto cambiados. <co>Hay que volver a exportar el pack.</c>
- <cl>Capturadora</c>: arreglada la mini vista previa de Editar Capas y del Explorador de Assets, que salia diminuta en la esquina de un recuadro negro. Ahora llena su recuadro, se refresca sola y respeta si escondiste al Jugador 1 o 2.
- <cl>Capturadora</c>: los avisos de calidad grafica, Low Detail Mode y jugador muerto ahora salen en espanol.
- <cl>Capturadora</c>: el boton de carpeta ya abre la carpeta cuando tu usuario de Windows lleva acentos.
- <cl>Explorador de Assets</c>: ocultar un tipo de objeto ya no recorre el nivel entero por cada click, y abrir el panel en un nivel enorme ya no guarda una copia de todos los objetos.
- <cl>Perfiles</c>: se elimino el cache en disco de getGJUserInfo y ahora se usa el nativo del juego. Corrige el estado de amistad viejo (no aparecer como amigo despues de aceptar).
- <cl>Texturas rotas</c> al cambiar resolucion, pantalla completa o calidad de texturas: el juego recrea el contexto grafico y tira todas las texturas. Ahora el mod suelta las suyas antes y las rearma despues.
- <cl>Memoria</c>: las portadas y thumbnails que mirabas se quedaban en VRAM toda la sesion. Ahora solo se mantienen las ultimas y el resto se libera.
- <cl>Search History</c>: corregido que todas las entradas mostraran "Demon".

---

# <cy>v1.0.9</c>

**Fix Update**
- Se eliminaron varios archivos que provocaban crash.
- Collab Editor agregado en beta cerrada. Estara disponible para todos el 20 de julio de 2026 con v1.1.0.
- Editor: nuevo color picker con Ctrl + G.
- Editor: nueva rotacion con Alt + click derecho.

# <cy>v1.0.8</c>

**Resumen**
- Nuevo Paimon Hub con skin GD.
- Menu Music y Paimon Icons redisenados.
- Texture Studio mejorado con coloreado mas estable.
- Perfil redisenado y mas opciones visuales.
- Correcciones de rendimiento, audio y estabilidad.

# <cy>v1.0.7</c>

**Resumen**
- Busqueda de canciones por nombre.
- Filtros para Mis niveles.
- Menu Physics agregado.
- Nuevos ajustes para estas funciones.

# <cy>v1.0.6</c>

## English

**Fixes and Optimizations**
- Mod optimization (resolved bottlenecks in image, GIF and request loading).
- Bugs fixed.
- Custom cursor improved.
- Pet no longer crashes in the editor.
- Emote system optimized.

**New Features**
- History.
- Texture Studio Beta 1.
- New button system.
- Popups with new animations.
- Profile gradient.
- Video with audio in profile.
- Message notification.
- New Paimbnails UI.
- Editor Music (play your music while creating in the editor).

## Espanol

**Arreglos y Optimizaciones**
- Optimizacion del mod (resolucion de cuellos de botella en carga de imagenes, GIFs y peticiones).
- Bugs arreglados.
- Custom cursor mejorado.
- Pet sin crash en el editor.
- Sistema de emotes optimizado.

**Nuevas Funciones**
- Historial.
- Texture Studio Beta 1.
- Nuevo sistema de botones.
- Popups con animaciones nuevas.
- Degradado de perfil.
- Video con audio en perfil.
- Notificacion de mensajes.
- Nueva UI de Paimbnails.
- Editor Music (pones tu musica mientras creas en el editor).

# <cy>v1.0.5</c>

## English

**Mod Previews**
- New Mod Previews feature: when you open a Geode mod, if its repository has a previews/ folder with preview-1.png ... preview-10.png, Paimbnails shows a thumbnail strip in the Details tab. Click a thumbnail to open a full-screen gallery with prev/next navigation. Toggle it from the Mod Previews setting.
- Based on the original idea and design by Alphalaneous (Mod-Previews), reimplemented natively for Geode v5 (no extra dependencies).

**Paimon Agent Mode**
- New Agent Mode toggle button under Paimon in the guide chat. Pink (agent:off) means Paimon answers questions and shows you the way; blue (agent:on) means Paimon executes actions for you.
- Visual agent execution with real clicks: in agent mode, when you press Ask, the chat closes and an AgentPilot Paimon spawns on top of the running scene (z=99999, above any popup). She flies in a soft bezier curve through a chain of targets, really clicking buttons along the way - not just opening the final popup.
- Whitelist by prefix: the ClickInvoker only fires CCMenuItem::activate() on nodes whose ID starts with flozwer.paimbnails2/. Anything outside that prefix is rejected silently, so the agent never accidentally interacts with unrelated UI.
- ActionGraph: a small registry of pre-defined click chains for the most common intents. The DSL parser routes prompts like "go to discord" to the matching chain.
- WaitForNode step polls every 0.15 s with a configurable timeout (default 2 s) until a node with the given ID appears in the scene, so the agent waits for the popup to render before flying to the next button.
- AgentDSL parser turns natural-language prompts into a small action plan with five step kinds: Open, WaitForNode, ClickButton, SetSetting and Say. Works in English/Spanish (e.g. "open discord", "go to forum", "enable discord", "set language to spanish").
- SettingsRegistry: a whitelist of mod settings the agent is allowed to modify (about 16 entries). Anything outside the whitelist is refused.
- The Agent Mode state is persisted across sessions.

**Paimon Guide (Paigorit V1)**
- Matching algorithm Paigorit V1 powering the Paimon guide chat. Paimon learns from the real titles of the popups and layers in the mod instead of hand-written keyword lists.
- PopupRegistry: every Paimbnails popup/layer is registered with its real display name, aliases, weight and an open() lambda. Each entry has a weight (1-200) so when several popups match, the most specific one wins.
- Compound keyword matching is bag-of-words: a multi-word display name matches as long as all words appear, regardless of order. Match scoring is weight + bonuses (compound match, exact token, high fuzzy similarity).
- LightLemmatizer module: English/Spanish stopwords are stripped, light suffix-based stemming, and a synonym table of about 50 entries. Reuses the existing rapidfuzz (header-only, MIT) for fuzzy similarity. No new external libraries were added.

**Compatibility**
- Removed incompatibility with thesillydoggo.blur-api. Paimbnails now coexists with mods that depend on Blur API (e.g. QOLMod). Our internal blur stays untouched and continues to work.

**Stability**
- Each phase of on game exit is now wrapped with try/catch via a safeShutdownStep helper. A failure in one shutdown step (audio, video, cache cleanup) no longer aborts the whole exit sequence; every other step still runs, saved data is persisted, and the failure is logged with the step name.
- ThumbnailLoader now releases CCImage instances via release() instead of delete, respecting Cocos2d-x reference counting.
- Build now uses C++23 (Geode 5.6.1 requires it). The previous C++20 setting could cause subtle ABI mismatches.

**Build / Toolchain**
- Project version bumped in CMakeLists.

## Espanol

**Mod Previews**
- Nueva funcion Mod Previews: al abrir un mod de Geode, si su repositorio tiene una carpeta previews/ con preview-1.png ... preview-10.png, Paimbnails muestra una tira de thumbnails en la pestana de Details. Toca un thumbnail para abrir una galeria a pantalla completa con navegacion anterior/siguiente. Se activa desde el ajuste Mod Previews.
- Basado en la idea y diseno original de Alphalaneous (Mod-Previews), reimplementado de forma nativa para Geode v5 (sin dependencias extra).

**Paimon Agent Mode**
- Nuevo boton de Agent Mode bajo Paimon en el chat de la guia. Rosa (agent:off) significa que Paimon responde preguntas y te muestra el camino; azul (agent:on) significa que Paimon ejecuta acciones por vos.
- Ejecucion visual del agente con clicks reales: en modo agente, al presionar Ask, el chat se cierra y una Paimon AgentPilot aparece sobre la escena actual (z=99999, encima de cualquier popup). Vuela en una curva bezier suave por una cadena de objetivos, haciendo click de verdad en los botones por el camino, no solo abriendo el popup final.
- Whitelist por prefijo: el ClickInvoker solo dispara CCMenuItem::activate() en nodos cuyo ID empieza con flozwer.paimbnails2/. Todo lo de fuera de ese prefijo se rechaza en silencio, asi el agente nunca interactua por accidente con UI ajena.
- ActionGraph: un pequeno registro de cadenas de click predefinidas para los intents mas comunes. El parser del DSL enruta frases como "andate a discord" a la cadena correspondiente.
- El paso WaitForNode consulta cada 0.15 s con un timeout configurable (2 s por defecto) hasta que un nodo con el ID dado aparece en la escena, asi el agente espera a que el popup se renderice antes de volar al siguiente boton.
- Parser AgentDSL convierte frases en lenguaje natural en un pequeno plan de accion con cinco tipos de paso: Open, WaitForNode, ClickButton, SetSetting y Say. Funciona en ingles/espanol (ej. "abre discord", "andate al foro", "activa discord", "pon el idioma en espanol").
- SettingsRegistry: una whitelist de ajustes del mod que el agente puede modificar (unas 16 entradas). Todo lo de fuera de la whitelist se rechaza.
- El estado de Agent Mode se guarda entre sesiones.

**Guia Paimon (Paigorit V1)**
- Algoritmo de matching Paigorit V1 que mueve el chat de la guia Paimon. Paimon aprende de los titulos reales de los popups y capas del mod en vez de listas de keywords escritas a mano.
- PopupRegistry: cada popup/capa de Paimbnails se registra con su display name real, alias, peso y un lambda open(). Cada entrada tiene un peso (1-200), asi cuando varios popups coinciden, gana el mas especifico.
- El matching compuesto es bag-of-words: un display name de varias palabras coincide mientras aparezcan todas las palabras, sin importar el orden. El scoring es peso + bonuses (match compuesto, token exacto, alta similitud difusa).
- Modulo LightLemmatizer: se quitan las stopwords ingles/espanol, stemming ligero por sufijo, y una tabla de sinonimos de unas 50 entradas. Reutiliza el rapidfuzz existente (header-only, MIT) para la similitud difusa. No se agregaron nuevas librerias externas.

**Compatibilidad**
- Eliminada la incompatibilidad con thesillydoggo.blur-api. Paimbnails ahora coexiste con mods que dependen de Blur API (ej. QOLMod). Nuestro blur interno queda intacto y sigue funcionando.

**Estabilidad**
- Cada fase del cierre del juego ahora se envuelve con try/catch via un helper safeShutdownStep. Un fallo en un paso del apagado (audio, video, limpieza de cache) ya no aborta toda la secuencia de salida; los demas pasos siguen ejecutandose, los datos guardados se persisten y el fallo queda logueado con el nombre del paso.
- ThumbnailLoader ahora libera las instancias de CCImage via release() en vez de delete, respetando el conteo de referencias de Cocos2d-x.
- El build ahora usa C++23 (Geode 5.6.1 lo requiere). El ajuste anterior de C++20 podia causar incompatibilidades sutiles de ABI.

**Build / Toolchain**
- Version del proyecto actualizada en CMakeLists.

# <cy>v1.0.4</c>

## English

**Shaders**
- New dynamic shader system with runtime parameter updates.
- Reworked shader pipeline for improved performance and flexibility.
- Popup blur effect on opened popups.
- Additional shaders bundled and selectable per layer.

**UI / Controls**
- Custom sliders with new visuals and smoother behavior.
- New dynamic input system for more responsive interactions.
- Fast circular menu for quick access to common actions.

**Audio**
- Custom menu music with per-layer overrides.

**Integrations**
- Discord Rich Presence support showing current layer and activity.

**Experimental**
- Paidraw beta: in-app drawing tool (early preview, opt-in).

**Technical**
- Removed ImagePlus dependency (no longer required to install).
- General optimization pass across rendering, audio and asset pipelines.
- Multiple bug fixes.

## Espanol

**Shaders**
- Nuevo sistema de shaders dinamico con actualizacion de parametros en runtime.
- Pipeline de shaders reescrito para mejor rendimiento y flexibilidad.
- Efecto de blur en los popups abiertos.
- Shaders adicionales incluidos y seleccionables por capa.

**UI / Controles**
- Sliders personalizados con nuevos visuales y comportamiento mas suave.
- Nuevo sistema de input dinamico para interacciones mas responsivas.
- Menu circular rapido para acceso rapido a acciones comunes.

**Audio**
- Musica de menu personalizada con overrides por capa.

**Integraciones**
- Soporte de Discord Rich Presence mostrando la capa y actividad actual.

**Experimental**
- Paidraw beta: herramienta de dibujo dentro del juego (preview temprano, opcional).

**Tecnico**
- Eliminada la dependencia de ImagePlus (ya no es necesario instalarla).
- Pase general de optimizacion en los pipelines de render, audio y assets.
- Multiples bugs arreglados.

# <cy>v1.0.1</c>

## English

**Thumbnails**
- Thumbnail previews in level cells (LevelBrowserLayer, LevelSearchLayer).
- Realtime search preview with configurable debounce.
- Local thumbnail viewer popup with gallery mode and configurable transitions.
- Concurrent download limit setting to control network usage.

**Custom Backgrounds**
- Per-layer background configuration (menu, search, gauntlet, level select, profile).
- Support for static images, GIF animations and video files (MP4).
- Video backgrounds with GPU-accelerated YUV decoding via Media Foundation (Windows), AVFoundation (macOS/iOS) and software fallback (Android/other).
- Background blur shaders (Kawase, Paimon blur) with configurable intensity.
- Shared video player cache to avoid redundant decoder instances across layers.
- Dark mode overlay and adaptive color modes for backgrounds.

**Dynamic Song System**
- Per-layer music configuration with custom paths, song IDs, speed and filter.
- Smooth audio handoff between video background audio and dynamic songs.

**Pet Companion**
- Animated pet sprite that follows the cursor on supported layers.
- Idle, sleep and reaction states tied to game events (level complete, death, practice exit).

**Badges**
- Custom badge icons alongside player names in comments and profiles.
- Adaptive font scaling for long comments.

**Emotes**
- Emote system for comments with CDN-backed asset delivery.

**Accessibility / UI**
- Transparent list mode for level browsers.
- Compact list mode.
- Button scale animation on hover for registered Paimbnails UI elements.

**Technical**
- Geode node-ids integration for cross-mod compatibility.
- Runtime lifecycle manager for ordered shutdown (audio, video, texture caches).
- Cloudflare Worker backend with CDN fallback for read-only API endpoints.
- API key authentication with optional mod-code for moderator features.

## Espanol

**Thumbnails**
- Previews de thumbnail en las celdas de nivel (LevelBrowserLayer, LevelSearchLayer).
- Preview de busqueda en tiempo real con debounce configurable.
- Popup visor de thumbnails local con modo galeria y transiciones configurables.
- Ajuste de limite de descargas concurrentes para controlar el uso de red.

**Fondos Personalizados**
- Configuracion de fondo por capa (menu, busqueda, gauntlet, seleccion de nivel, perfil).
- Soporte para imagenes estaticas, animaciones GIF y archivos de video (MP4).
- Fondos de video con decodificacion YUV acelerada por GPU via Media Foundation (Windows), AVFoundation (macOS/iOS) y fallback por software (Android/otros).
- Shaders de blur de fondo (Kawase, Paimon blur) con intensidad configurable.
- Cache compartido del reproductor de video para evitar instancias de decoder redundantes entre capas.
- Overlay de modo oscuro y modos de color adaptativos para fondos.

**Sistema de Cancion Dinamica**
- Configuracion de musica por capa con rutas propias, IDs de cancion, velocidad y filtro.
- Transicion de audio suave entre el audio del video de fondo y las canciones dinamicas.

**Pet Companion**
- Sprite de mascota animado que sigue el cursor en las capas soportadas.
- Estados de idle, sleep y reaccion ligados a eventos del juego (nivel completado, muerte, salida de practica).

**Badges**
- Iconos de badge personalizados junto a los nombres de jugador en comentarios y perfiles.
- Escalado de fuente adaptativo para comentarios largos.

**Emotes**
- Sistema de emotes para comentarios con entrega de assets via CDN.

**Accesibilidad / UI**
- Modo de lista transparente para los navegadores de niveles.
- Modo de lista compacta.
- Animacion de escala de boton al pasar el cursor sobre elementos de UI registrados de Paimbnails.

**Tecnico**
- Integracion con geode node-ids para compatibilidad entre mods.
- Manager de ciclo de vida en runtime para un apagado ordenado (audio, video, caches de textura).
- Backend en Cloudflare Worker con fallback de CDN para endpoints de API de solo lectura.
- Autenticacion por API key con mod-code opcional para funciones de moderador.
