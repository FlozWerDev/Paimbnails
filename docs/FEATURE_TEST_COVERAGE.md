# Cobertura verificada del catálogo de funciones

Fecha del inventario: 2026-10-16.

No se probaron las 135 funciones una por una. La fuente canónica `src/core/modules/ModuleCatalog.cpp` contiene **132 entradas** (módulos/interruptores). No encontré 3 entradas adicionales en ese catálogo que completen 135; puede que ese total cuente subfunciones o controles internos. `mod.json` tiene 255 claves de configuración, que tampoco equivalen a módulos.

La suite portable registrada más reciente dio **55 comprobaciones PASS** (23 regresiones C++ compiladas y ejecutadas, más comprobaciones Python/Node); la suite de bindings dio **3 PASS**; la última suite opcional dio **5 PASS y 2 SKIP**. CTest dio 1/1. Estos resultados son de bancos/componentes, no 135 pruebas funcionales independientes.

“Componente” significa que se ejecutó una parte de la implementación asociada; no garantiza toda la interfaz ni todos los casos dentro de Geometry Dash. “Parcial” indica una comprobación estática o de una ruta auxiliar. “SKIP” indica que hay un test relacionado pero no se pudo ejecutar por un requisito ausente. “Sin prueba específica” quiere decir que no se encontró una regresión directa en la suite revisada; no afirma que la función esté rota. Las 132 entradas sí pasan por `module_registry_regression`, que ejercita el interruptor, el almacenamiento simulado, los valores por defecto y la jerarquía de padres. “Sin prueba específica” se refiere al efecto propio de esa función o a su interfaz, no al cableado común del interruptor.

| # | Función | ID | Cobertura encontrada | Pruebas relacionadas |
|---:|---|---|---|---|
| 1 | Smooth UI | `paimbnails.smoothui.global` | Sin prueba específica | — |
| 2 | Dynamic Popups | `paimbnails.dynamicpopups.global` | Sin prueba específica | — |
| 3 | Dynamic Transition | `paimbnails.dynamictransition.global` | Componente | dynamic_transition_motion_regression; dynamic_transition_config_regression; dynamic_transition_bindings_regression (movimiento, configuración y bindings) |
| 4 | Popup Blur | `paimbnails.popupblur.global` | Sin prueba específica | — |
| 5 | Smooth Scroll | `paimbnails.smoothscroll.global` | Sin prueba específica | — |
| 6 | Smooth Text Input | `paimbnails.smoothtext.global` | Sin prueba específica | — |
| 7 | Custom Cursor | `paimbnails.cursor.global` | Sin prueba específica | — |
| 8 | Cursor Transitions | `paimbnails.cursortransition.global` | Sin prueba específica | — |
| 9 | Cursor Click Effects | `paimbnails.cursorclick.global` | Sin prueba específica | — |
| 10 | Custom Hover | `paimbnails.customhover.global` | Sin prueba específica | — |
| 11 | Custom Slider Thumb | `paimbnails.customslider.global` | SKIP | tint_preview_shader_regression: omitida; requiere Linux EGL/GLES |
| 12 | Paimon Icons | `paimbnails.paimonicons.global` | Sin prueba específica | — |
| 13 | Scene Transitions | `paimbnails.transitions.global` | Parcial | native_scene_navigation_regression (rutas nativas de carga/editor; no cubre todos los efectos configurables) |
| 14 | Volume Scroll | `paimbnails.volumescroll.global` | Parcial | ios_keyboard_dispatcher_regression (compatibilidad del dispatcher; no prueba el cambio de volumen) |
| 15 | Dynamic Volume | `paimbnails.dynamicvolume.global` | Sin prueba específica | — |
| 16 | Safe Drop | `paimbnails.safedrop.global` | Sin prueba específica | — |
| 17 | Custom Backgrounds | `paimbnails.backgrounds.global` | Sin prueba específica | — |
| 18 | Beat Shaders | `paimbnails.beatshaders.global` | Sin prueba específica | — |
| 19 | Paimon RTX | `paimbnails.rtx.global` | Parcial | rtx_render_regression --static-only (contratos/cálculos; render GPU omitido) |
| 20 | Quick Hub | `paimbnails.quickhub.global` | Parcial | quick_hub_android_context_regression; ios_keyboard_dispatcher_regression (contratos de contexto/teclado, no flujo radial completo) |
| 21 | Separate Dual Icons | `paimbnails.separatedual.global` | Sin prueba específica | — |
| 22 | Icon Gradients | `paimbnails.icongradients.global` | Sin prueba específica | — |
| 23 | Gradient Animations | `paimbnails.gradientanimation.global` | Sin prueba específica | — |
| 24 | Settings Panel | `paimbnails.settingspanel.menu` | Sin prueba específica | — |
| 25 | Menu Music Player | `paimbnails.menumusic.menu` | Sin prueba específica | — |
| 26 | Menu Music Effects | `paimbnails.musiceffects.menu` | Sin prueba específica | — |
| 27 | Spatial Audio | `paimbnails.spatialaudio.menu` | Sin prueba específica | — |
| 28 | Menu Loop Control | `paimbnails.menuloop.menu` | Componente | menu_loop_scan_regression (escaneo de pistas y estado relacionado) |
| 29 | Menu Loop Shuffle | `paimbnails.menuloopshuffle.menu` | Sin prueba específica | — |
| 30 | Menu Physics | `paimbnails.menuphysics.menu` | Componente | menu_physics_regression (simulación de la física del menú) |
| 31 | Layout Editor | `paimbnails.menulayout.menu` | Sin prueba específica | — |
| 32 | Paimon Pet | `paimbnails.pet.menu` | Sin prueba específica | — |
| 33 | Paimon Guide | `paimbnails.guide.menu` | Componente | guide_matcher_regression; guide_conversation_regression (motor de temas y conversación) |
| 34 | Hidden Paimon | `paimbnails.hiddenpaimon.menu` | Sin prueba específica | — |
| 35 | PaiDraw | `paimbnails.paidraw.menu` | Sin prueba específica | — |
| 36 | Icon Maker | `paimbnails.iconmaker.menu` | Parcial | crash_compat_regression (solo vida útil segura del flujo IconShare) |
| 37 | Texture Studio | `paimbnails.texturestudio.menu` | Parcial | packgen_image_regression; packgen_paint_regression; ui_sprite_catalog; live_slot_shader_regression --static-only (componentes de generación, pintado, catálogo y cableado) |
| 38 | Dev Tools | `paimbnails.devtools.menu` | Sin prueba específica | — |
| 39 | Community Hub | `paimbnails.community.menu` | Sin prueba específica | — |
| 40 | Forum | `paimbnails.forum.menu` | Sin prueba específica | — |
| 41 | Versus | `paimbnails.versus.menu` | Sin prueba específica | — |
| 42 | Versus HUD | `paimbnails.versus.hud` | Sin prueba específica | — |
| 43 | Versus Cards | `paimbnails.versus.cards` | Sin prueba específica | — |
| 44 | Level Requests | `paimbnails.twitchrequests.menu` | Sin prueba específica | — |
| 45 | Web Requests | `paimbnails.webrequests.menu` | Sin prueba específica | — |
| 46 | OBS Stream Overlay | `paimbnails.streamoverlay.menu` | SKIP | stream_overlay: omitida; requiere Playwright y Chromium |
| 47 | Mod Previews | `paimbnails.modpreviews.menu` | Sin prueba específica | — |
| 48 | Level Thumbnails | `paimbnails.thumbnails.browser` | Componente | disk_manifest_regression; cache_budget_regression; crash_compat_regression (manifiesto, cuota y ciclo de vida de caché) |
| 49 | Hover Effects | `paimbnails.hovereffects.browser` | Sin prueba específica | — |
| 50 | Compact Mode | `paimbnails.compactlist.browser` | Sin prueba específica | — |
| 51 | Auto Previews | `paimbnails.autopreview.browser` | Sin prueba específica | — |
| 52 | LevelThumbs Fallback | `paimbnails.levelthumbs.browser` | Sin prueba específica | — |
| 53 | Leaderboard Cells | `paimbnails.leaderboardcells.browser` | Sin prueba específica | — |
| 54 | Score Gradient | `paimbnails.scoregradient.browser` | Sin prueba específica | — |
| 55 | Realtime Search | `paimbnails.searchpreview.browser` | Sin prueba específica | — |
| 56 | Quick Search | `paimbnails.quicksearch.browser` | Sin prueba específica | — |
| 57 | Search History | `paimbnails.searchhistory.browser` | Sin prueba específica | — |
| 58 | Incognito Mode | `paimbnails.incognito.browser` | Sin prueba específica | — |
| 59 | For You | `paimbnails.foryou.browser` | Sin prueba específica | — |
| 60 | My Levels Filters | `paimbnails.levelfilters.browser` | Sin prueba específica | — |
| 61 | Level Info Background | `paimbnails.levelbackground.level` | Sin prueba específica | — |
| 62 | Dynamic Song | `paimbnails.dynamicsong.level` | Sin prueba específica | — |
| 63 | Image Object Warning | `paimbnails.imagewarning.level` | Sin prueba específica | — |
| 64 | Official Slots | `paimbnails.officialslots.level` | Sin prueba específica | — |
| 65 | Info Suite | `paimbnails.infosuite.info` | Parcial | ios_keyboard_dispatcher_regression (compatibilidad de dos popups/servicios; no valida el interruptor maestro) |
| 66 | Extended Level Info | `paimbnails.extendedinfo.info` | Sin prueba específica | — |
| 67 | Visible IDs | `paimbnails.visibleids.info` | Parcial | ios_keyboard_dispatcher_regression (contrato del evento de teclado de IdBadge) |
| 68 | Jump To Page | `paimbnails.jumptopage.info` | Sin prueba específica | — |
| 69 | Advanced Search | `paimbnails.advancedsearch.info` | Parcial | ios_keyboard_dispatcher_regression (compatibilidad de entrada/teclado) |
| 70 | Search Presets | `paimbnails.searchpresets.info` | Sin prueba específica | — |
| 71 | Progress Tracking | `paimbnails.progresstracking.info` | Sin prueba específica | — |
| 72 | Death Heatmap | `paimbnails.deathheatmap.info` | Sin prueba específica | — |
| 73 | Unregistered Profiles | `paimbnails.unregprofiles.info` | Sin prueba específica | — |
| 74 | Comment Tools | `paimbnails.commenttools.info` | Sin prueba específica | — |
| 75 | Fix Green Usernames | `paimbnails.greenusers.info` | Sin prueba específica | — |
| 76 | GDHistory Enrichment | `paimbnails.gdhistory.info` | Sin prueba específica | — |
| 77 | Performance Mode | `paimbnails.performance.gameplay` | Sin prueba específica | — |
| 78 | Native Performance Mode | `paimbnails.nativeperf.gameplay` | Sin prueba específica | — |
| 79 | Disable Glow | `paimbnails.glowcut.gameplay` | Sin prueba específica | — |
| 80 | Disable Background Effects | `paimbnails.bgeffects.gameplay` | Sin prueba específica | — |
| 81 | Disable Gameplay Effects | `paimbnails.gameeffects.gameplay` | Sin prueba específica | — |
| 82 | Disable Player Effects | `paimbnails.playereffects.gameplay` | Sin prueba específica | — |
| 83 | Pause Dynamic Volume | `paimbnails.dynamicvolume.gameplay` | Sin prueba específica | — |
| 84 | Disable Paimbnails Effects | `paimbnails.modvisuals.gameplay` | Sin prueba específica | — |
| 85 | Disable Auto Preview | `paimbnails.autopreview.gameplay` | Sin prueba específica | — |
| 86 | Disable Level Transitions | `paimbnails.perftransitions.gameplay` | Sin prueba específica | — |
| 87 | Hide Background | `paimbnails.backgroundcut.gameplay` | Sin prueba específica | — |
| 88 | Hide Ground | `paimbnails.groundcut.gameplay` | Sin prueba específica | — |
| 89 | Hide Decoration | `paimbnails.decocut.gameplay` | Sin prueba específica | — |
| 90 | Disable Gradients | `paimbnails.gradientcut.gameplay` | Sin prueba específica | — |
| 91 | Disable Shaders | `paimbnails.shadercut.gameplay` | Sin prueba específica | — |
| 92 | Disable Particles | `paimbnails.particlecut.gameplay` | Sin prueba específica | — |
| 93 | Disable Flashes and Waves | `paimbnails.levelcut.gameplay` | Sin prueba específica | — |
| 94 | Thumbnail Capture | `paimbnails.capture.gameplay` | Componente | capture_operation_regression; crash_compat_regression (estado de captura y callback) |
| 95 | Custom Progress Bar | `paimbnails.progressbar.gameplay` | Sin prueba específica | — |
| 96 | Golden Best | `paimbnails.goldenbest.gameplay` | Sin prueba específica | — |
| 97 | Death Effects | `paimbnails.deatheffects.gameplay` | SKIP | death_animation_shader_regression: omitida; requiere Linux EGL/GLES |
| 98 | Level Entry Effects | `paimbnails.levelentry.gameplay` | Sin prueba específica | — |
| 99 | Pause Zoom | `paimbnails.pausezoom.gameplay` | Sin prueba específica | — |
| 100 | Frame Interpolation | `paimbnails.frameinterp.gameplay` | Sin prueba específica | — |
| 101 | Profile Redesign | `paimbnails.profileredesign.profile` | Sin prueba específica | — |
| 102 | Profile Music | `paimbnails.profilemusic.profile` | Componente | profile_playback_regression (cancelación y cambio de solicitudes de reproducción) |
| 103 | Paimon Profiles | `paimbnails.paimonprofiles.profile` | Sin prueba específica | — |
| 104 | Global Icons | `paimbnails.globalicons.profile` | Sin prueba específica | — |
| 105 | Role Badges | `paimbnails.badges.profile` | Sin prueba específica | — |
| 106 | Profile Levels | `paimbnails.progression.profile` | Sin prueba específica | — |
| 107 | Copy Icons | `paimbnails.iconcopy.profile` | Sin prueba específica | — |
| 108 | Emotes | `paimbnails.emotes.social` | Componente | emote_preload_regression (precarga y caché de emotes) |
| 109 | Comment Fonts | `paimbnails.fonts.social` | Sin prueba específica | — |
| 110 | Comment Mentions | `paimbnails.mentions.social` | Sin prueba específica | — |
| 111 | Messages Redesign | `paimbnails.messagesredesign.social` | Sin prueba específica | — |
| 112 | Message Notifications | `paimbnails.messagenotifs.social` | Sin prueba específica | — |
| 113 | New Thumbnail Alerts | `paimbnails.thumbalerts.social` | Sin prueba específica | — |
| 114 | Thumbnail Requests | `paimbnails.thumbrequests.social` | SKIP | thumbnail_upload_routing: omitida; falta el código privado cloudflare-server |
| 115 | Colorpicker | `paimbnails.colorpicker.editor` | Sin prueba específica | — |
| 116 | GIF to Objects | `paimbnails.gifimport.editor` | Componente | gif_decoder_regression; gif_import_regression (incluye vectorización de imagen/GIF) |
| 117 | Render Import | `paimbnails.gifrender.editor` | Componente | gif_import_regression (pases de render y vectorización); paint_circle_regression (modos pintura/círculos) |
| 118 | Physics Lab | `paimbnails.physics.editor` | Componente | editor_physics_regression; editor_physics_native_regression; editor_physics_preview_regression (solver, IDs nativos y preview) |
| 119 | Paimon Animate | `paimbnails.animate.editor` | Componente | editor_animate_regression (planificación de animación y grupos de frames) |
| 120 | Editor Music | `paimbnails.editormusic.editor` | Sin prueba específica | — |
| 121 | Collab Editor | `paimbnails.collab.editor` | Parcial | crash_compat_regression (contratos de teardown y hooks; no colaboración en red) |
| 122 | Collab Cursors | `paimbnails.collabcursors.editor` | Sin prueba específica | — |
| 123 | Input Scroll | `paimbnails.inputscroll.editor` | Sin prueba específica | — |
| 124 | Song Search | `paimbnails.songsearch.editor` | Sin prueba específica | — |
| 125 | Auto Update | `paimbnails.autoupdate.system` | Parcial | crash_compat_regression (solo cancelación de tareas del UpdateChecker durante cierre) |
| 126 | Disk Cache | `paimbnails.diskcache.system` | Componente | disk_manifest_regression; cache_budget_regression (índice, expiración y cuota) |
| 127 | RobTop Response Cache | `paimbnails.robtopcache.system` | Sin prueba específica | — |
| 128 | Discord Rich Presence | `paimbnails.discordrpc.system` | Sin prueba específica | — |
| 129 | Moderation Tools | `paimbnails.moderation.system` | SKIP | thumbnail_upload_routing: omitida; depende del servidor privado |
| 130 | Secure Mod Code | `paimbnails.modauth.system` | SKIP | thumbnail_upload_routing: omitida; depende del servidor privado |
| 131 | Debug Logs | `paimbnails.debuglogs.system` | Sin prueba específica | — |
| 132 | Crash Reports | `paimbnails.crashreports.system` | Sin prueba específica | — |

Conteo de la matriz: **132** entradas; **13 Componente**, **11 Parcial**, **6 SKIP**, **102 Sin prueba específica**.

## Correcciones y hallazgos

- `GifPaintVectorizer.cpp`: el modo de dibujo podía emitir círculos demasiado pequeños para verse y redondear uniones muy juntas. Se filtran tapas menores de 1,25 px y se limita el redondeo en uniones finas; pasan las regresiones de importación GIF y círculos/pintura.
- `UiSpriteCatalog.cpp`: `storeItemIcon_001.png` se clasificaba como sprite de interfaz tintable aunque es contenido. Se movió al grupo de contenido; pasa `ui_sprite_catalog`.
- `PhysicsNative.hpp` y `PhysicsTriggerEmitter.cpp`: el regresor nativo esperaba el símbolo obsoleto `nativeTriggerInfo`. Se centralizó el mapeo de IDs nativos y se actualizó el regresor; pasa `editor_physics_native_regression` y compiló `PhysicsTriggerEmitter.cpp` con los flags del build.
- Se arreglaron fallos del propio banco: referencia ambigua a `kPi`, campos `priority` obsoletos en fixtures de guía y rutas rígidas de compilador/RapidFuzz en los scripts `.bat`.
- `tests/run_regressions.py` ahora registra 23 bancos C++ portables, una prueba de ejecución de interruptores y una auditoría de integridad de las 132 entradas del catálogo; `tests/README.md` documenta requisitos y omisiones opcionales.
- Los recursos oficiales produjeron cuatro avisos no fatales de `sourceSize` en sprites pequeños (`spider_07_04_*` y `emptyGlow`); los seis casos de extracción/reempaquetado pasaron. Corregí el comentario del regresor, que decía un número de avisos distinto al encontrado en los assets actuales.

## Pruebas ejecutadas

La suite portable registró 55 PASS: compilación y ejecución de estos 23 bancos C++: `capture_operation`, `native_scene_navigation`, `dynamic_transition_motion`, `menu_physics`, `disk_manifest`, `gif_decoder`, `profile_playback`, `cache_budget`, `atomic_file_write`, `audio_wav`, `editor_animate`, `editor_physics`, `editor_physics_native`, `editor_physics_preview`, `module_registry`, `menu_loop_scan`, `gif_import`, `packgen_image`, `packgen_paint`, `paint_circle`, `ui_sprite_catalog`, `guide_matcher` y `guide_conversation`; además de `dynamic_transition_config`, `live_slot_shader --static-only`, `rtx_render --static-only`, `module_catalog_regression`, `crash_compat`, `ios_keyboard_dispatcher`, `quick_hub_android_context`, `main_level_prefetch` y `emote_preload`.

`module_registry_regression` recorre en ejecución los 132 interruptores: lectura y escritura para Backing Setting/Saved/Custom, valores por defecto, invalidación de caché y bloqueo por padres. `module_catalog_regression` valida las mismas 132 entradas: unicidad de IDs, padres existentes sin ciclos, coincidencia de los 10 accesores personalizados y claves booleanas de ajustes. Ambas pruebas validan el cableado del registro; no ejecutan los efectos o pantallas de cada función.

La suite de bindings dio 3/3 PASS: `pause_layer_bindings`, `geode_sdk_compat` y `dynamic_transition_bindings`. CTest dio 1/1 PASS. También pasaron los runners independientes de guía (443/443 casos de matcher, 9/9 multi-topic y 31/31 de conversación). La suite opcional dio 7 comprobaciones: 5 PASS y 2 SKIP. Pasaron `packgen_official_assets` (6/6 casos internos), `ui_sprite_audit` (194 plists, 57 familias y 886 PNG sueltos) y `stream_overlay` con Edge (133 combinaciones estilo/diseño, 11 animaciones, fuentes/sprites GD, actualizaciones, privacidad y galería). Solo se omitieron el routing de uploads (falta el servidor privado) y los shaders GPU (el fixture requiere Linux EGL/GLES).

## Límites de esta ronda

- Geometry Dash estaba abierto y el log muestra `flozwer.paimbnails2` v1.2.0. El paquete instalado era el de las 03:23; después recompilé el DLL completo y empaqueté el nuevo `.geode` sin instalarlo ni interrumpir la sesión. Geode 5.11.2 advierte que GD 2.2081 es más nuevo que su versión objetivo, desactiva su interfaz y registra incompatibilidades. Esa carga antigua no cuenta como prueba interactiva fiable de los cambios actuales.
- No se recorrieron manualmente las pantallas del juego; por eso no se certificó el comportamiento interactivo de cada módulo.
- El DLL completo compiló y enlazó con el target `flozwer.paimbnails2.dll`; Geode CLI empaquetó el `.geode` nuevo correctamente, sin instalarlo en el juego abierto.
- La matriz registra pruebas de componentes y del cableado del catálogo, no cada interacción de todas las funciones. Solo quedan bloqueadas la integración con el servidor privado y la ejecución GPU en Linux.
- Para validar las 135 funciones que mencionas en una matriz 1:1, falta identificar las tres que no aparecen como módulos independientes en `ModuleCatalog.cpp` y recorrer las rutas UI/runtime con Geode compatible con GD 2.2081.
