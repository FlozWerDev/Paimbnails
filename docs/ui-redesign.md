# Rediseño de la interfaz de Paimbnails

El diseño comparte una paleta oscura, acentos celestes y violeta, superficies discretas y texto de alto contraste. Los estados se distinguen por texto, posición y color. Los controles conservan los callbacks y los bindings de Geode.

## Navegación y funciones

- El hub muestra la descripción de cada acción, busca en nombres, descripciones y ajustes, y ofrece favoritos y los últimos doce accesos. Las claves de favoritos y recientes no dependen del idioma del ajuste ni del índice opcional de Discord.
- La categoría Crear da acceso a iconos, Texture Studio y conversión de GIF. General incorpora Comunidad, Versus y peticiones de streaming; Extras incluye Quick Hub y la guía.
- Guardar favoritos conserva el desplazamiento de la lista; Recientes actualiza su orden al abrir una función. Seleccionar una categoría vuelve a su vista de funciones. Las búsquedas vacías muestran una explicación útil. La última categoría se conserva entre visitas. Las tarjetas adaptan sus columnas al ancho y su altura al texto.
- Los ajustes usan interruptores con estado visible, valores numéricos separados del título, descripciones que se ajustan al ancho y pestañas con indicador animado. Los selectores compartidos permiten buscar una opción al pulsar el valor; las flechas siguen disponibles.
- Los paneles de fondos, módulos, iconos, texturas, comunidad, PaiDraw y Versus comparten superficies y colores. Los lienzos, las herramientas de edición y las integraciones conservan sus acciones específicas.
- Las noticias ajustan sus tarjetas al texto y el foro comparte botones y estados de etiquetas. La ayuda del hub explica sus controles y muestra el atajo configurado en Geode.
- Las ayudas calculan la altura desde el texto renderizado. El popup informativo prescinde del fondo aleatorio y de la lectura de miniaturas que acompañaba a ese adorno.

## Ventanas y movimiento

167 clases de popup usan `PaimonPopup`, una subclase normal de `geode::Popup`; la lista de cobertura está debajo. El fondo, título, cierre, sombra y ajuste a la pantalla se comparten. No se añaden hooks de bindings ni dependencias al manifiesto.

La entrada predeterminada dura 0,24 s y la salida 0,16 s antes del multiplicador de velocidad. Los estilos configurables existentes conservan sus opciones y respetan la escala de la ventana ajustada a la pantalla. Los menús compartidos, las revelaciones, las ventanas y el panel de ajustes respetan el control de movimiento reducido.

Las revelaciones conservan la opacidad original de cada nodo y cancelan la animación anterior al reiniciarse. El desplazamiento manual cancela el destino pendiente de la rueda. El panel flotante de ajustes mantiene sus bordes dentro de la pantalla al arrastrarlo. El selector de emotes conserva su animación propia sin duplicar la del sistema compartido.

Durante la salida de un popup se desactivan sus controles. Un cierre repetido libera la referencia de la animación antes de retirar la ventana.

## Compatibilidad y comprobaciones

- SDK local consultado: `Popup.hpp` y su implementación, `CCMenuItemToggler`, `CCMenuItemSpriteExtra`, `ButtonSprite`, `ScrollLayer`, `CCNode` y `CCRGBAProtocol`.
- Se conservan la invocación de `Popup::onClose`, los delegates de teclado, el orden de actualización de los toggles y la prioridad de los controles dentro de popups.
- No se ejecutó ninguna compilación ni ningún comando de build. La verificación usa análisis sintáctico de 340 archivos C++, revisión de rutas e inclusiones, comprobación del diff y validación del JavaScript de la referencia visual. El HTML se renderizó y se inspeccionó con Firefox. Este análisis no comprueba los tipos ni el enlace de C++.
- La referencia HTML es una demostración del diseño y no sustituye una captura del juego. La comprobación visual y de interacción en Geometry Dash queda pendiente: escritorio y Android, ventanas anidadas, búsqueda, favoritos, controles, rueda y arrastre, texto largo, modos de animación y movimiento reducido.
- Los cambios de renderizado RTX que ya estaban presentes se conservan; la integración de su popup consiste en usar la nueva base.

## Referencia visual

[Abrir la vista interactiva](ui-preview.html). Permite explorar categorías, buscar, usar favoritos y recientes, comparar anchos y ver controles de Smooth UI y fondos.

## Cobertura de popups

La base común se aplica a cada clase de esta lista. Las mejoras de filas, selectores y pestañas se propagan a las funciones que usan `PaiConfigKit`, `SettingsControls`, `IconMakerKit` y `VersusUIKit`.

### backgrounds (2)

| Popup | Archivo |
| --- | --- |
| `SameAsPickerPopup` | [SameAsPickerPopup.hpp](../src/features/backgrounds/ui/SameAsPickerPopup.hpp) |
| `VideoSettingsPopup` | [VideoSettingsPopup.hpp](../src/features/backgrounds/ui/VideoSettingsPopup.hpp) |

### beat-shaders (1)

| Popup | Archivo |
| --- | --- |
| `BeatShaderConfigLayer` | [BeatShaderConfigLayer.hpp](../src/features/beat-shaders/ui/BeatShaderConfigLayer.hpp) |

### capture (4)

| Popup | Archivo |
| --- | --- |
| `CaptureAssetBrowserPopup` | [CaptureAssetBrowserPopup.hpp](../src/features/capture/ui/CaptureAssetBrowserPopup.hpp) |
| `CaptureLayerEditorPopup` | [CaptureLayerEditorPopup.hpp](../src/features/capture/ui/CaptureLayerEditorPopup.hpp) |
| `CaptureMenuPopup` | [CaptureMenuPopup.hpp](../src/features/capture/ui/CaptureMenuPopup.hpp) |
| `CapturePreviewPopup` | [CapturePreviewPopup.hpp](../src/features/capture/ui/CapturePreviewPopup.hpp) |

### collab-editor (5)

| Popup | Archivo |
| --- | --- |
| `CollabRoomPopup` | [CollabPopups.hpp](../src/features/collab-editor/CollabPopups.hpp) |
| `CollabInvitePopup` | [CollabPopups.hpp](../src/features/collab-editor/CollabPopups.hpp) |
| `CollabChatPopup` | [CollabPopups.hpp](../src/features/collab-editor/CollabPopups.hpp) |
| `HostOptionsPopup` | [CollabPopups.hpp](../src/features/collab-editor/CollabPopups.hpp) |
| `CollabPeersPopup` | [CollabPopups.hpp](../src/features/collab-editor/CollabPopups.hpp) |

### colorful-icons (1)

| Popup | Archivo |
| --- | --- |
| `PaimonIconsConfigPopup` | [PaimonIconsConfigPopup.hpp](../src/features/colorful-icons/ui/PaimonIconsConfigPopup.hpp) |

### compat-mods (3)

| Popup | Archivo |
| --- | --- |
| `ModlyCommentsPopup` | [ModlyCommentsPopup.hpp](../src/features/compat-mods/ui/ModlyCommentsPopup.hpp) |
| `ModlyModPopup` | [ModlyModPopup.hpp](../src/features/compat-mods/ui/ModlyModPopup.hpp) |
| `ModlyProfilePopup` | [ModlyProfilePopup.hpp](../src/features/compat-mods/ui/ModlyProfilePopup.hpp) |

### core (1)

| Popup | Archivo |
| --- | --- |
| `ModerationPanel` | [ModAuthFlow.cpp](../src/core/ModAuthFlow.cpp) |

### cursor (3)

| Popup | Archivo |
| --- | --- |
| `ClickEffectTunePopup` | [ClickEffectTunePopup.hpp](../src/features/cursor/ui/ClickEffectTunePopup.hpp) |
| `CursorConfigPopup` | [CursorConfigPopup.hpp](../src/features/cursor/ui/CursorConfigPopup.hpp) |
| `CursorShopDetailPopup` | [CursorShopDetailPopup.hpp](../src/features/cursor/ui/CursorShopDetailPopup.hpp) |

### custom-hover (1)

| Popup | Archivo |
| --- | --- |
| `HoverPopup` | [HoverPopup.cpp](../src/features/custom-hover/HoverPopup.cpp) |

### custom-slider (1)

| Popup | Archivo |
| --- | --- |
| `CustomSliderPopup` | [CustomSliderPopup.hpp](../src/features/custom-slider/ui/CustomSliderPopup.hpp) |

### death-effects (2)

| Popup | Archivo |
| --- | --- |
| `DeathEffectPopup` | [DeathEffectPopup.hpp](../src/features/death-effects/ui/DeathEffectPopup.hpp) |
| `DeathAnimationPopup` | [DeathAnimation.hpp](../src/features/death-effects/visuals/DeathAnimation.hpp) |

### dev-tools (1)

| Popup | Archivo |
| --- | --- |
| `GifToSheetPopup` | [GifToSheetPopup.hpp](../src/features/dev-tools/ui/GifToSheetPopup.hpp) |

### discord-presence (1)

| Popup | Archivo |
| --- | --- |
| `DiscordConfigPopup` | [DiscordConfigPopup.hpp](../src/features/discord-presence/ui/DiscordConfigPopup.hpp) |

### dynamic-songs (1)

| Popup | Archivo |
| --- | --- |
| `DynamicSongPopup` | [DynamicSongPopup.hpp](../src/features/dynamic-songs/ui/DynamicSongPopup.hpp) |

### dynamic-volume (1)

| Popup | Archivo |
| --- | --- |
| `DynamicVolumePopup` | [DynamicVolumePopup.hpp](../src/features/dynamic-volume/ui/DynamicVolumePopup.hpp) |

### editor-filters (1)

| Popup | Archivo |
| --- | --- |
| `MyLevelFilterPopup` | [MyLevelFilterPopup.hpp](../src/features/editor-filters/ui/MyLevelFilterPopup.hpp) |

### editor-music (1)

| Popup | Archivo |
| --- | --- |
| `EditorMusicPickerPopup` | [EditorMusicPickerPopup.hpp](../src/features/editor-music/ui/EditorMusicPickerPopup.hpp) |

### editor-physics (2)

| Popup | Archivo |
| --- | --- |
| `PhysicsBodyPopup` | [PhysicsBodyPopup.hpp](../src/features/editor-physics/ui/PhysicsBodyPopup.hpp) |
| `PhysicsPopup` | [PhysicsPopup.hpp](../src/features/editor-physics/ui/PhysicsPopup.hpp) |

### emotes (1)

| Popup | Archivo |
| --- | --- |
| `EmotePickerPopup` | [EmotePickerPopup.hpp](../src/features/emotes/ui/EmotePickerPopup.hpp) |

### fonts (1)

| Popup | Archivo |
| --- | --- |
| `FontPickerPopup` | [FontPickerPopup.hpp](../src/features/fonts/ui/FontPickerPopup.hpp) |

### forum (2)

| Popup | Archivo |
| --- | --- |
| `CreatePostPopup` | [CreatePostPopup.hpp](../src/features/forum/ui/CreatePostPopup.hpp) |
| `PostDetailPopup` | [PostDetailPopup.hpp](../src/features/forum/ui/PostDetailPopup.hpp) |

### foryou (3)

| Popup | Archivo |
| --- | --- |
| `ForYouPreferencesPopup` | [ForYouPreferencesPopup.hpp](../src/features/foryou/ui/ForYouPreferencesPopup.hpp) |
| `LevelTagsGatePopup` | [LevelTagsGatePopup.hpp](../src/features/foryou/ui/LevelTagsGatePopup.hpp) |
| `TagPreferencesPopup` | [TagPreferencesPopup.hpp](../src/features/foryou/ui/TagPreferencesPopup.hpp) |

### frame-interp (1)

| Popup | Archivo |
| --- | --- |
| `FrameInterpPopup` | [FrameInterpPopup.hpp](../src/features/frame-interp/ui/FrameInterpPopup.hpp) |

### gameplay-performance (1)

| Popup | Archivo |
| --- | --- |
| `GameplayPerformancePopup` | [GameplayPerformancePopup.hpp](../src/features/gameplay-performance/ui/GameplayPerformancePopup.hpp) |

### garage-hub (1)

| Popup | Archivo |
| --- | --- |
| `GarageHubPopup` | [GarageHubPopup.hpp](../src/features/garage-hub/ui/GarageHubPopup.hpp) |

### gif-import (1)

| Popup | Archivo |
| --- | --- |
| `GifImportPopup` | [GifImportPopup.hpp](../src/features/gif-import/ui/GifImportPopup.hpp) |

### global-icon (1)

| Popup | Archivo |
| --- | --- |
| `GlobalIconViewPopup` | [GlobalIconViewPopup.hpp](../src/features/global-icon/ui/GlobalIconViewPopup.hpp) |

### guide (1)

| Popup | Archivo |
| --- | --- |
| `PaimonGuideChatPopup` | [PaimonGuideChatPopup.hpp](../src/features/guide/ui/PaimonGuideChatPopup.hpp) |

### hooks (2)

| Popup | Archivo |
| --- | --- |
| `ProfilePreviewPopup` | [LeaderboardsLayer.cpp](../src/hooks/LeaderboardsLayer.cpp) |
| `SimpleThumbnailPopup` | [LevelAreaInnerLayer.cpp](../src/hooks/LevelAreaInnerLayer.cpp) |

### icon-copy (5)

| Popup | Archivo |
| --- | --- |
| `CopiedIconsPopup` | [CopiedIconsPopup.hpp](../src/features/icon-copy/ui/CopiedIconsPopup.hpp) |
| `CopyIconsPopup` | [CopyIconsPopup.hpp](../src/features/icon-copy/ui/CopyIconsPopup.hpp) |
| `IconDetailPopup` | [IconDetailPopup.hpp](../src/features/icon-copy/ui/IconDetailPopup.hpp) |
| `IconSetNamePopup` | [IconSetNamePopup.hpp](../src/features/icon-copy/ui/IconSetNamePopup.hpp) |
| `MyIconSetsPopup` | [MyIconSetsPopup.hpp](../src/features/icon-copy/ui/MyIconSetsPopup.hpp) |

### icon-gradients (5)

| Popup | Archivo |
| --- | --- |
| `ColorSelectLayer` | [ColorSelectLayer.hpp](../src/features/icon-gradients/ui/ColorSelectLayer.hpp) |
| `CustomAnimationPopup` | [CustomAnimationPopup.hpp](../src/features/icon-gradients/ui/CustomAnimationPopup.hpp) |
| `GradientAnimationPopup` | [GradientAnimationPopup.hpp](../src/features/icon-gradients/ui/GradientAnimationPopup.hpp) |
| `GradientLayer` | [GradientLayer.hpp](../src/features/icon-gradients/ui/GradientLayer.hpp) |
| `LoadLayer` | [LoadLayer.hpp](../src/features/icon-gradients/ui/LoadLayer.hpp) |

### icon-maker (7)

| Popup | Archivo |
| --- | --- |
| `GradientEditorPopup` | [GradientEditorPopup.hpp](../src/features/icon-maker/ui/GradientEditorPopup.hpp) |
| `IconActionSheet` | [IconActionSheet.hpp](../src/features/icon-maker/ui/IconActionSheet.hpp) |
| `IconHelpPopup` | [IconHelpPopup.hpp](../src/features/icon-maker/ui/IconHelpPopup.hpp) |
| `IconNamePopup` | [IconNamePopup.hpp](../src/features/icon-maker/ui/IconNamePopup.hpp) |
| `IconTryPopup` | [IconTryPopup.hpp](../src/features/icon-maker/ui/IconTryPopup.hpp) |
| `NewIconPopup` | [NewIconPopup.hpp](../src/features/icon-maker/ui/NewIconPopup.hpp) |
| `TemplatePickerPopup` | [TemplatePickerPopup.hpp](../src/features/icon-maker/ui/TemplatePickerPopup.hpp) |

### info-suite (8)

| Popup | Archivo |
| --- | --- |
| `AdvancedSearchPopup` | [AdvancedSearchPopup.hpp](../src/features/info-suite/ui/AdvancedSearchPopup.hpp) |
| `ExtendedInfoPopup` | [ExtendedInfoPopup.hpp](../src/features/info-suite/ui/ExtendedInfoPopup.hpp) |
| `JumpToPagePopup` | [JumpToPagePopup.hpp](../src/features/info-suite/ui/JumpToPagePopup.hpp) |
| `LevelHistoryDetailPopup` | [LevelHistoryDetailPopup.hpp](../src/features/info-suite/ui/LevelHistoryDetailPopup.hpp) |
| `LevelHistoryPopup` | [LevelHistoryPopup.hpp](../src/features/info-suite/ui/LevelHistoryPopup.hpp) |
| `LevelStatsPopup` | [LevelStatsPopup.hpp](../src/features/info-suite/ui/LevelStatsPopup.hpp) |
| `SearchPresetsPopup` | [SearchPresetsPopup.hpp](../src/features/info-suite/ui/SearchPresetsPopup.hpp) |
| `UnregisteredProfilePopup` | [UnregisteredProfilePopup.hpp](../src/features/info-suite/ui/UnregisteredProfilePopup.hpp) |

### layers (1)

| Popup | Archivo |
| --- | --- |
| `PaimonInfoPopup` | [PaimonInfoPopup.hpp](../src/layers/PaimonInfoPopup.hpp) |

### main-menu-layout (1)

| Popup | Archivo |
| --- | --- |
| `MainMenuLayoutPresetPopup` | [MainMenuLayoutPresetPopup.hpp](../src/features/main-menu-layout/ui/MainMenuLayoutPresetPopup.hpp) |

### menu-music (10)

| Popup | Archivo |
| --- | --- |
| `ExternalSongsPopup` | [ExternalSongsPopup.hpp](../src/features/menu-music/ui/ExternalSongsPopup.hpp) |
| `FfmpegInstallPopup` | [FfmpegInstallPopup.hpp](../src/features/menu-music/ui/FfmpegInstallPopup.hpp) |
| `MenuMusicAddPopup` | [MenuMusicAddPopup.hpp](../src/features/menu-music/ui/MenuMusicAddPopup.hpp) |
| `MenuMusicLibraryPopup` | [MenuMusicLibraryPopup.hpp](../src/features/menu-music/ui/MenuMusicLibraryPopup.hpp) |
| `MenuMusicPlaylistsPopup` | [MenuMusicPlaylistsPopup.hpp](../src/features/menu-music/ui/MenuMusicPlaylistsPopup.hpp) |
| `MenuMusicPopup` | [MenuMusicPopup.hpp](../src/features/menu-music/ui/MenuMusicPopup.hpp) |
| `MenuMusicSettingsPopup` | [MenuMusicSettingsPopup.hpp](../src/features/menu-music/ui/MenuMusicSettingsPopup.hpp) |
| `MusicTagsPopup` | [MusicTagsPopup.hpp](../src/features/menu-music/ui/MusicTagsPopup.hpp) |
| `NewgroundsBrowserPopup` | [NewgroundsBrowserPopup.hpp](../src/features/menu-music/ui/NewgroundsBrowserPopup.hpp) |
| `YtDlpInstallPopup` | [YtDlpInstallPopup.hpp](../src/features/menu-music/ui/YtDlpInstallPopup.hpp) |

### mod-previews (1)

| Popup | Archivo |
| --- | --- |
| `ModPreviewGalleryPopup` | [ModPreviewGalleryPopup.hpp](../src/features/mod-previews/ui/ModPreviewGalleryPopup.hpp) |

### moderation (7)

| Popup | Archivo |
| --- | --- |
| `AddModeratorPopup` | [AddModeratorPopup.hpp](../src/features/moderation/ui/AddModeratorPopup.hpp) |
| `BanListPopup` | [BanListPopup.hpp](../src/features/moderation/ui/BanListPopup.hpp) |
| `BanUserPopup` | [BanUserPopup.hpp](../src/features/moderation/ui/BanUserPopup.hpp) |
| `BannedPopup` | [BannedPopup.hpp](../src/features/moderation/ui/BannedPopup.hpp) |
| `SetDailyWeeklyPopup` | [SetDailyWeeklyPopup.hpp](../src/features/moderation/ui/SetDailyWeeklyPopup.hpp) |
| `UserReportsPopup` | [UserReportsPopup.hpp](../src/features/moderation/ui/UserReportsPopup.hpp) |
| `WhitelistPopup` | [WhitelistPopup.hpp](../src/features/moderation/ui/WhitelistPopup.hpp) |

### official-slots (3)

| Popup | Archivo |
| --- | --- |
| `SlotEditorPopup` | [SlotEditorPopup.hpp](../src/features/official-slots/ui/SlotEditorPopup.hpp) |
| `SlotManagerPopup` | [SlotManagerPopup.hpp](../src/features/official-slots/ui/SlotManagerPopup.hpp) |
| `SlotOrderPopup` | [SlotOrderPopup.hpp](../src/features/official-slots/ui/SlotOrderPopup.hpp) |

### onboarding (1)

| Popup | Archivo |
| --- | --- |
| `WelcomePopup` | [WelcomeFlow.hpp](../src/features/onboarding/WelcomeFlow.hpp) |

### pet (3)

| Popup | Archivo |
| --- | --- |
| `PaimonShopPopup` | [PaimonShopPopup.hpp](../src/features/pet/ui/PaimonShopPopup.hpp) |
| `PetLayerPickerPopup` | [PetConfigPopup.cpp](../src/features/pet/ui/PetConfigPopup.cpp) |
| `PetConfigPopup` | [PetConfigPopup.hpp](../src/features/pet/ui/PetConfigPopup.hpp) |

### profile-music (2)

| Popup | Archivo |
| --- | --- |
| `ProfileMusicPopup` | [ProfileMusicPopup.hpp](../src/features/profile-music/ui/ProfileMusicPopup.hpp) |
| `SongSearchPopup` | [SongSearchPopup.hpp](../src/features/profile-music/ui/SongSearchPopup.hpp) |

### profiles (13)

| Popup | Archivo |
| --- | --- |
| `CommentBgSettingsPopup` | [CommentBgSettingsPopup.hpp](../src/features/profiles/ui/CommentBgSettingsPopup.hpp) |
| `CustomBadgePickerPopup` | [CustomBadgePickerPopup.hpp](../src/features/profiles/ui/CustomBadgePickerPopup.hpp) |
| `ProfileBgGradientPopup` | [ProfileBgGradientPopup.hpp](../src/features/profiles/ui/ProfileBgGradientPopup.hpp) |
| `ProfileBgPickerPopup` | [ProfileBgPickerPopup.hpp](../src/features/profiles/ui/ProfileBgPickerPopup.hpp) |
| `ProfileImgPopup` | [ProfileImgPopup.hpp](../src/features/profiles/ui/ProfileImgPopup.hpp) |
| `ProfilePicEditorPopup` | [ProfilePicEditorPopup.hpp](../src/features/profiles/ui/ProfilePicEditorPopup.hpp) |
| `ProfilePicIconsDetailPopup` | [ProfilePicIconsDetailPopup.hpp](../src/features/profiles/ui/ProfilePicIconsDetailPopup.hpp) |
| `ProfileReviewsPopup` | [ProfileReviewsPopup.hpp](../src/features/profiles/ui/ProfileReviewsPopup.hpp) |
| `ProfileSettingsPopup` | [ProfileSettingsPopup.hpp](../src/features/profiles/ui/ProfileSettingsPopup.hpp) |
| `ProfileViewsPopup` | [ProfileViewsPopup.hpp](../src/features/profiles/ui/ProfileViewsPopup.hpp) |
| `RatePopup` | [RatePopup.hpp](../src/features/profiles/ui/RatePopup.hpp) |
| `RateProfilePopup` | [RateProfilePopup.hpp](../src/features/profiles/ui/RateProfilePopup.hpp) |
| `ReportUserPopup` | [ReportUserPopup.hpp](../src/features/profiles/ui/ReportUserPopup.hpp) |

### progressbar (1)

| Popup | Archivo |
| --- | --- |
| `ProgressBarConfigPopup` | [ProgressBarConfigPopup.hpp](../src/features/progressbar/ui/ProgressBarConfigPopup.hpp) |

### progression (2)

| Popup | Archivo |
| --- | --- |
| `BadgeDetailPopup` | [BadgeDetailPopup.hpp](../src/features/progression/ui/BadgeDetailPopup.hpp) |
| `ProgressionPopup` | [ProgressionPopup.hpp](../src/features/progression/ui/ProgressionPopup.hpp) |

### quick-hub (5)

| Popup | Archivo |
| --- | --- |
| `QuickButtonImagePopup` | [QuickButtonImagePopup.hpp](../src/features/quick-hub/ui/QuickButtonImagePopup.hpp) |
| `IconPickerPopup` | [QuickButtonPopup.cpp](../src/features/quick-hub/ui/QuickButtonPopup.cpp) |
| `QuickButtonPopup` | [QuickButtonPopup.hpp](../src/features/quick-hub/ui/QuickButtonPopup.hpp) |
| `QuickButtonSfxPopup` | [QuickButtonSfxPopup.hpp](../src/features/quick-hub/ui/QuickButtonSfxPopup.hpp) |
| `RadialConfigPopup` | [RadialConfigPopup.hpp](../src/features/quick-hub/ui/RadialConfigPopup.hpp) |

### rtx (1)

| Popup | Archivo |
| --- | --- |
| `RTXConfigLayer` | [RTXConfigLayer.hpp](../src/features/rtx/ui/RTXConfigLayer.hpp) |

### scorecell (2)

| Popup | Archivo |
| --- | --- |
| `LeaderboardLayoutPopup` | [LeaderboardLayoutPopup.hpp](../src/features/scorecell/ui/LeaderboardLayoutPopup.hpp) |
| `ScoreCellSettingsPopup` | [ScoreCellSettingsPopup.hpp](../src/features/scorecell/ui/ScoreCellSettingsPopup.hpp) |

### search-history (1)

| Popup | Archivo |
| --- | --- |
| `SearchHistoryPopup` | [SearchHistoryPopup.hpp](../src/features/search-history/ui/SearchHistoryPopup.hpp) |

### smooth-scroll (1)

| Popup | Archivo |
| --- | --- |
| `SmoothScrollConfigPopup` | [SmoothScrollConfigPopup.hpp](../src/features/smooth-scroll/ui/SmoothScrollConfigPopup.hpp) |

### texture-studio (1)

| Popup | Archivo |
| --- | --- |
| `NewProjectPopup` | [NewProjectPopup.hpp](../src/features/texture-studio/ui/NewProjectPopup.hpp) |

### thumb-requests (1)

| Popup | Archivo |
| --- | --- |
| `ThumbRequestsPopup` | [ThumbRequestsPopup.hpp](../src/features/thumb-requests/ui/ThumbRequestsPopup.hpp) |

### thumbnails (5)

| Popup | Archivo |
| --- | --- |
| `LevelCellSettingsPopup` | [LevelCellSettingsPopup.hpp](../src/features/thumbnails/ui/LevelCellSettingsPopup.hpp) |
| `LocalThumbnailViewPopup` | [LocalThumbnailViewPopup.hpp](../src/features/thumbnails/ui/LocalThumbnailViewPopup.hpp) |
| `ReportInputPopup` | [ReportInputPopup.hpp](../src/features/thumbnails/ui/ReportInputPopup.hpp) |
| `ThumbnailOrderPopup` | [ThumbnailOrderPopup.hpp](../src/features/thumbnails/ui/ThumbnailOrderPopup.hpp) |
| `ThumbnailSettingsPopup` | [ThumbnailSettingsPopup.hpp](../src/features/thumbnails/ui/ThumbnailSettingsPopup.hpp) |

### transitions (6)

| Popup | Archivo |
| --- | --- |
| `CustomTransitionEditorPopup` | [CustomTransitionEditorPopup.hpp](../src/features/transitions/ui/CustomTransitionEditorPopup.hpp) |
| `PreviewPopup` | [DynamicTransitionConfigPopup.cpp](../src/features/transitions/ui/DynamicTransitionConfigPopup.cpp) |
| `DynamicTransitionConfigPopup` | [DynamicTransitionConfigPopup.hpp](../src/features/transitions/ui/DynamicTransitionConfigPopup.hpp) |
| `LevelEntryConfigPopup` | [LevelEntryConfigPopup.hpp](../src/features/transitions/ui/LevelEntryConfigPopup.hpp) |
| `StingerConfigPopup` | [StingerConfigPopup.hpp](../src/features/transitions/ui/StingerConfigPopup.hpp) |
| `TransitionConfigPopup` | [TransitionConfigPopup.hpp](../src/features/transitions/ui/TransitionConfigPopup.hpp) |

### twitch-requests (10)

| Popup | Archivo |
| --- | --- |
| `RequestQueuePopup` | [RequestSourcesPopup.cpp](../src/features/twitch-requests/ui/RequestSourcesPopup.cpp) |
| `RequestRoutePopup` | [RequestSourcesPopup.cpp](../src/features/twitch-requests/ui/RequestSourcesPopup.cpp) |
| `RequestQueueSelectorPopup` | [RequestSourcesPopup.cpp](../src/features/twitch-requests/ui/RequestSourcesPopup.cpp) |
| `RequestSourcesPopup` | [RequestSourcesPopup.hpp](../src/features/twitch-requests/ui/RequestSourcesPopup.hpp) |
| `StreamOverlayPopup` | [StreamOverlayPopup.hpp](../src/features/twitch-requests/ui/StreamOverlayPopup.hpp) |
| `AddVideoRulePopup` | [TwitchFiltersPopup.cpp](../src/features/twitch-requests/ui/TwitchFiltersPopup.cpp) |
| `TwitchFiltersPopup` | [TwitchFiltersPopup.hpp](../src/features/twitch-requests/ui/TwitchFiltersPopup.hpp) |
| `TwitchMessagePopup` | [TwitchMessagePopup.hpp](../src/features/twitch-requests/ui/TwitchMessagePopup.hpp) |
| `TwitchNotifyPopup` | [TwitchNotifyPopup.hpp](../src/features/twitch-requests/ui/TwitchNotifyPopup.hpp) |
| `WebFeedbackPopup` | [WebFeedbackPopup.hpp](../src/features/twitch-requests/ui/WebFeedbackPopup.hpp) |

### ui (4)

| Popup | Archivo |
| --- | --- |
| `FeatureConfigPopup` | [FeatureConfigPopup.hpp](../src/ui/FeatureConfigPopup.hpp) |
| `FeatureInfoPopup` | [FeatureInfoPopup.hpp](../src/ui/FeatureInfoPopup.hpp) |
| `OptionPickerPopup` | [PaiConfigKit.cpp](../src/ui/PaiConfigKit.cpp) |
| `SmoothUIConfigPopup` | [SmoothUIConfigPopup.hpp](../src/ui/SmoothUIConfigPopup.hpp) |

### updates (2)

| Popup | Archivo |
| --- | --- |
| `UpdateCenterPopup` | [UpdateCenterPopup.hpp](../src/features/updates/ui/UpdateCenterPopup.hpp) |
| `UpdateProgressPopup` | [UpdateProgressPopup.hpp](../src/features/updates/ui/UpdateProgressPopup.hpp) |

### utils (1)

| Popup | Archivo |
| --- | --- |
| `BetaUploadWarningPopup` | [BetaUploadWarning.hpp](../src/utils/BetaUploadWarning.hpp) |

### versus (7)

| Popup | Archivo |
| --- | --- |
| `VersusDeckPopup` | [VersusDeckPopup.hpp](../src/features/versus/ui/VersusDeckPopup.hpp) |
| `VersusEndPopup` | [VersusEndPopup.hpp](../src/features/versus/ui/VersusEndPopup.hpp) |
| `VersusFriendlyPopup` | [VersusFriendlyPopup.hpp](../src/features/versus/ui/VersusFriendlyPopup.hpp) |
| `VersusHistoryPopup` | [VersusHistoryPopup.hpp](../src/features/versus/ui/VersusHistoryPopup.hpp) |
| `VersusMatchPopup` | [VersusMatchPopup.hpp](../src/features/versus/ui/VersusMatchPopup.hpp) |
| `VersusProfilePopup` | [VersusProfilePopup.hpp](../src/features/versus/ui/VersusProfilePopup.hpp) |
| `VersusSeasonPopup` | [VersusSeasonPopup.hpp](../src/features/versus/ui/VersusSeasonPopup.hpp) |

### visuals (1)

| Popup | Archivo |
| --- | --- |
| `ExtraEffectsPopup` | [ExtraEffectsPopup.hpp](../src/features/visuals/ui/ExtraEffectsPopup.hpp) |

### volume-scroll (2)

| Popup | Archivo |
| --- | --- |
| `ExtendedKeybindEditPopup` | [ExtendedKeybindEditPopup.hpp](../src/features/volume-scroll/ui/ExtendedKeybindEditPopup.hpp) |
| `ScrollKeybindsPopup` | [ScrollKeybindsPopup.hpp](../src/features/volume-scroll/ui/ScrollKeybindsPopup.hpp) |
