# v1.0.9

**Fix Update**
- Guia Paimon (Paigorit V1.5): cobertura de todo el mod (collab, editor history/filters/color/rotate, song search, mentions, menu physics, smooth UI, settings panel...), frases de problema EN/ES, categorias Editor/Visuales, chips dinamicos y recomendaciones.
- Se eliminaron varios archivos que provocaban crash.
- Editor History: compact undo panel for Color / Groups / Layers (last change per category).
- Collab Editor agregado en beta cerrada. Estara disponible para todos el 20 de julio de 2026 con v1.1.0.
- Collab Editor: sincronizacion rehecha para ser confiable con assets de miles de objetos. Envio con confirmacion y reintentos (ya no se pierden objetos), verificacion automatica de estado con auto-reparacion de desyncs, y las selecciones gigantes ahora si se sincronizan al moverlas o rotarlas.
- Editor: nuevo color picker con Ctrl + G.
- Editor: nueva rotacion con Alt + click derecho.
- Search History: popup redisenado mas compacto (icono de dificultad, fecha y contador). Tocar una entrada ahora busca directamente, y se corrigio que todas las entradas mostraran "Demon".
- Perfiles: se elimino el cache en disco de getGJUserInfo; ahora se usa el cache nativo del juego. Corrige que el perfil mostrara un estado de amistad viejo (ej. no aparecer como amigo despues de aceptar la solicitud).

# v1.0.8

**Resumen**
- Nuevo Paimon Hub con skin GD.
- Menu Music y Paimon Icons redisenados.
- Texture Studio mejorado con coloreado mas estable.
- Perfil redisenado y mas opciones visuales.
- Correcciones de rendimiento, audio y estabilidad.

# v1.0.7

**Resumen**
- Busqueda de canciones por nombre.
- Filtros para Mis niveles.
- Menu Physics agregado.
- Nuevos ajustes para estas funciones.

# v1.0.6

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

# v1.0.5

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

# v1.0.4

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

# v1.0.1

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
