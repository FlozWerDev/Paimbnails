# Inventario de Dynamic Transition

Fuente: bindings de GD `2.2091` del build configurado y todos sus includes.

Se revisaron **1123 clases** y se extrajeron **471 candidatos** de interfaz, controles y efectos. Este inventario describe cobertura por herencia y puntos de entrada; la validacion visual en Geometry Dash sigue pendiente.

Regenerar sin compilar: `python3 tests/audit_dynamic_transition_bindings.py --write`.

## Cobertura implementada

- Escenas: `replaceScene`, `pushScene`, `popScene`, `popSceneWithTransition`; regreso por Escape o Volver.
- Popups, paneles desplegables, bloqueantes y dialogos: insercion y retirada del nodo, incluso desde overrides de cierre.
- `LevelBrowserLayer` admite tambien el modo superpuesto (`m_isOverlay`).
- Aperturas especificas: las implementaciones de `show` con direccion Win distinta se interceptan una vez; los metodos compartidos no reciben hooks duplicados.
- `EndLevelLayer` y `RetryLevelLayer` incluyen su `showLayer` propio. Opciones usa `GJDropDownLayer`.
- `SlideInLayer::showLayer/hideLayer` tiene bindings solo en macOS; alli se adapta su animacion. La insercion y retirada comun no depende de esas direcciones.
- Los componentes, carga, monedas, particulas, texto, scroll y objetos del nivel conservan sus animaciones propias. Dynamic Transition se aplica a la navegacion y a los paneles completos.
- Los popups marcados de Paimon usan Dynamic Popups. Los popups de Geode de otros mods son opcionales.

| Familia | Clases |
| --- | ---: |
| Panel bloqueante | 3 |
| Escena / navegador superpuesto | 1 |
| Dialogo | 1 |
| Panel desplegable | 15 |
| Punto comun del motor | 5 |
| Escena de gameplay / editor conservada | 5 |
| Carga / animacion nativa conservada | 2 |
| Popup del juego | 280 |
| Navegacion de escenas | 25 |
| Componente / animacion nativa conservada | 84 |
| Control o efecto / animacion nativa conservada | 50 |

## Popup del juego

| Clase | Metodos propios detectados |
| --- | --- |
| `AccountBanPopup` | `keyBackClicked`, `onClose` |
| `AccountLoginLayer` | `keyBackClicked`, `onClose` |
| `AccountRegisterLayer` | `keyBackClicked`, `onClose` |
| `AccountReviewPopup` | `onClose` |
| `AdminActionConfirmPopup` | `keyBackClicked`, `onClose` |
| `AudioAssetsBrowser` | `keyBackClicked`, `onClose` |
| `AudioAssetsPopup` | `keyBackClicked`, `onClose` |
| `AutoBackupOptionsPopup` | Heredados |
| `AutoBackupPopup` | `onClose` |
| `AutoSaveSettingsLayer` | `onClose` |
| `BrowseSmartKeyLayer` | `onBack` |
| `BrowseSmartTemplateLayer` | `keyBackClicked`, `onBack`, `onClose` |
| `ChallengesPage` | `keyBackClicked`, `onClose`, `show` |
| `CharacterColorPage` | `keyBackClicked`, `onClose`, `show` |
| `ClearPlatformerSavesOptionsPopup` | Heredados |
| `ClearPlatformerSavesPopup` | `keyBackClicked`, `onClose` |
| `CollisionBlockPopup` | Heredados |
| `ColorSelectLiveOverlay` | `keyBackClicked`, `show` |
| `ColorSelectPopup` | `keyBackClicked`, `show` |
| `CommentOptionsPopup` | `keyBackClicked`, `onClose` |
| `CommunityCreditsPage` | `keyBackClicked`, `onClose`, `show` |
| `ConfigureValuePopup` | `keyBackClicked`, `onClose` |
| `ControlSlotsPopup` | `onClose` |
| `CreateGuidelinesLayer` | `keyBackClicked`, `keyDown`, `keyUp`, `onClose` |
| `CreateParticlePopup` | `keyBackClicked`, `onClose` |
| `CreateStartPosPopup` | `onClose` |
| `CreatorSettingsPopup` | `onClose` |
| `CustomSongLayer` | `keyBackClicked`, `onClose`, `show` |
| `CustomizeObjectLayer` | `keyBackClicked`, `onClose` |
| `CustomizeObjectSettingsPopup` | `onClose` |
| `DailyLevelPage` | `keyBackClicked`, `onClose`, `show` |
| `DailyRecallUnlockPopup` | Heredados |
| `DeathMarkerSettingsPopup` | `keyBackClicked`, `onClose` |
| `DeathStatsPopup` | `keyBackClicked`, `onClose` |
| `DeathTrackerOptionsPopup` | `keyBackClicked`, `onClose` |
| `DeathTrackerPopup` | `keyBackClicked`, `keyDown`, `onClose`, `show` |
| `DemonFilterSelectLayer` | `keyBackClicked`, `onClose` |
| `DemonInfoPopup` | `keyBackClicked`, `onClose` |
| `DropdownListPopup` | `keyBackClicked` |
| `EasingSelectPopup` | `onClose` |
| `EditGameObjectPopup` | Heredados |
| `EditTriggersPopup` | `onClose` |
| `EditorCommandsPopup` | Heredados |
| `EditorOptionsLayer` | `onClose` |
| `EditorSFXActionConfigLayer` | `onClose` |
| `EditorSFXSettingsPopup` | Heredados |
| `EditorSoundBrowserLayer` | `onClose` |
| `EditorSoundConfigInfoLayer` | `onClose` |
| `EditorSoundConfigUploadPopup` | `keyBackClicked`, `onCancel`, `onClose` |
| `EditorSoundsLayer` | `onClose` |
| `EditorSoundsSettingsPopup` | `keyBackClicked`, `onClose` |
| `EditorWindowPopup` | `keyDown`, `onClose`, `onExit`, `show` |
| `EmotePickerPopup` | `keyDown`, `onClose`, `show` |
| `FLAlertLayer` | `keyBackClicked`, `keyDown`, `onEnter`, `show` |
| `FRequestProfilePage` | `keyBackClicked`, `onClose` |
| `FindBPMLayer` | `onClose` |
| `FindObjectOptionsLayer` | Heredados |
| `FindObjectPopup` | Heredados |
| `FollowRewardPage` | `keyBackClicked`, `onClose`, `show` |
| `FriendPickerPopup` | `onClose` |
| `FriendRequestPopup` | `keyBackClicked`, `onClose` |
| `FriendsProfilePage` | `onClose` |
| `GJAccountSettingsLayer` | `keyBackClicked`, `onClose` |
| `GJColorSetupLayer` | `keyBackClicked`, `onClose` |
| `GJFollowCommandLayer` | Heredados |
| `GJMessagePopup` | `keyBackClicked`, `onClose` |
| `GJOptionsLayer` | Heredados |
| `GJPFollowCommandLayer` | Heredados |
| `GJPathPage` | `keyBackClicked`, `onBack`, `show` |
| `GJPathRewardPopup` | `keyBackClicked` |
| `GJPathsLayer` | `keyBackClicked`, `onClose`, `onExit`, `show` |
| `GJPromoPopup` | `keyBackClicked`, `onClose`, `onExit`, `show` |
| `GJRateEditorSoundLayer` | `onClose` |
| `GJRateLevelLayer` | `keyBackClicked`, `onClose` |
| `GJRateLevelListLayer` | `keyBackClicked`, `onClose` |
| `GJSpecialColorSelect` | `keyBackClicked`, `onClose` |
| `GJWriteMessagePopup` | `keyBackClicked`, `onClose` |
| `GameLevelOptionsLayer` | Heredados |
| `GameOptionsLayer` | Heredados |
| `GameSettingsLayer` | `onClose` |
| `GridSettingsLayer` | Heredados |
| `HSVLiveOverlay` | `keyBackClicked`, `show` |
| `HSVWidgetPopup` | `keyBackClicked`, `onClose` |
| `IDNameBrowserPopup` | `keyBackClicked`, `onClose`, `show` |
| `InfoLayer` | `keyBackClicked`, `keyDown`, `onClose`, `show` |
| `InvitesPopup` | Heredados |
| `ItemInfoPopup` | `keyBackClicked`, `onClose` |
| `KeybindCapturePopup` | `fadeButtonSprite`, `keyDown`, `keyUp`, `onClose` |
| `KeybindingsLayer` | `onClose` |
| `KeybindingsSettingsPopup` | `keyBackClicked`, `onClose` |
| `LevelBadgePopup` | `keyBackClicked`, `show` |
| `LevelBindingsLayer` | `onClose` |
| `LevelFeatureLayer` | `keyBackClicked`, `onClose` |
| `LevelLeaderboard` | `keyBackClicked`, `onClose`, `show` |
| `LevelOptionsLayer` | Heredados |
| `LevelOptionsLayer2` | Heredados |
| `LevelPickerPopup` | Heredados |
| `LevelSettingsLayer` | `keyBackClicked`, `onClose` |
| `LevelStatsInfoPopup` | `keyBackClicked`, `onClose` |
| `LikeItemLayer` | `keyBackClicked`, `onClose` |
| `MemberListPopup` | Heredados |
| `MessageThreadsPage` | `keyBackClicked`, `onClose`, `show` |
| `MessagesProfilePage` | `keyBackClicked`, `onClose` |
| `MoreOptionsLayer` | `keyBackClicked`, `onClose` |
| `MoreSearchLayer` | `keyBackClicked`, `onClose` |
| `MoreVideoOptionsLayer` | `keyBackClicked`, `onClose` |
| `MoveStepPopup` | Heredados |
| `MultiTriggerPopup` | Heredados |
| `MusicBrowser` | `keyBackClicked`, `onClose` |
| `MusicChangelogPopup` | Heredados |
| `MyStuffPopup` | `keyBackClicked`, `onClose` |
| `NCSInfoLayer` | `keyBackClicked`, `onClose` |
| `NewThreadPopup` | `keyBackClicked`, `keyDown`, `onClose`, `show` |
| `NewgroundsInfoLayer` | `keyBackClicked`, `onClose` |
| `NewsPopup` | `keyBackClicked`, `keyDown`, `onClose` |
| `NotificationFilterPopup` | `keyBackClicked`, `onClose` |
| `NotificationsPage` | `keyBackClicked`, `onClose` |
| `NumberInputLayer` | `keyBackClicked`, `onClose` |
| `OptionsScrollLayer` | `keyBackClicked`, `onClose` |
| `PagedListPopup` | `keyBackClicked`, `onClose`, `show` |
| `ParentalGateLayer` | Heredados |
| `ParentalOptionsLayer` | `keyBackClicked`, `onClose` |
| `ParentalPrivacyPopup` | Heredados |
| `PlatformerControlsPopup` | `onClose` |
| `ProfilePage` | `keyBackClicked`, `onClose`, `show` |
| `PromoInterstitial` | `keyBackClicked`, `onClose`, `show` |
| `PurchaseItemPopup` | `keyBackClicked`, `onClose` |
| `RateDemonLayer` | `keyBackClicked`, `onClose` |
| `RateLevelLayer` | `keyBackClicked`, `onClose` |
| `RateStarsLayer` | `keyBackClicked`, `onClose` |
| `RateStarsModLayer` | `keyBackClicked`, `onClose` |
| `RenameThreadPopup` | `keyBackClicked`, `onClose` |
| `ReportPopup` | `keyBackClicked`, `onClose` |
| `ReportQueuePopup` | Heredados |
| `ReviewChoiceSheet` | `keyBackClicked`, `onClose` |
| `RewardUnlockLayer` | `keyBackClicked`, `onClose` |
| `RewardsPage` | `keyBackClicked`, `onClose`, `show` |
| `SFXBrowser` | `keyBackClicked`, `onClose` |
| `SFXChangelogPopup` | Heredados |
| `SFXFolderPickerPopup` | `keyBackClicked`, `onClose` |
| `SFXSettingsPopup` | `onClose` |
| `ScrollViewPopup` | `keyBackClicked`, `onClose` |
| `SelectArtLayer` | `keyBackClicked`, `onClose` |
| `SelectEventLayer` | `keyBackClicked`, `onClose` |
| `SelectFontLayer` | `keyBackClicked`, `onClose` |
| `SelectFontLayer2` | `keyBackClicked`, `onClose` |
| `SelectListIconLayer` | `keyBackClicked`, `onClose` |
| `SelectPremadeLayer` | `keyBackClicked`, `onClose` |
| `SelectSFXSortLayer` | `keyBackClicked`, `onClose` |
| `SelectSettingLayer` | `keyBackClicked`, `onClose` |
| `SetColorIDPopup` | Heredados |
| `SetFolderPopup` | Heredados |
| `SetGroupIDLayer` | `keyBackClicked`, `onClose` |
| `SetIDPopup` | `keyBackClicked`, `onCancel`, `onClose`, `show` |
| `SetItemIDLayer` | `onClose` |
| `SetLevelOrderPopup` | Heredados |
| `SetTargetIDLayer` | Heredados |
| `SetTextPopup` | `keyBackClicked`, `onCancel`, `onClose`, `show` |
| `SetupAdvFollowEditPhysicsPopup` | Heredados |
| `SetupAdvFollowPopup` | `onClose` |
| `SetupAdvFollowRetargetPopup` | Heredados |
| `SetupAnimSettingsPopup` | `onClose` |
| `SetupAnimationPopup` | `onClose` |
| `SetupAreaAnimTriggerPopup` | Heredados |
| `SetupAreaFadeTriggerPopup` | Heredados |
| `SetupAreaMoveTriggerPopup` | Heredados |
| `SetupAreaRotateTriggerPopup` | Heredados |
| `SetupAreaTintTriggerPopup` | `onClose` |
| `SetupAreaTransformTriggerPopup` | Heredados |
| `SetupAreaTriggerPopup` | Heredados |
| `SetupArtSwitchPopup` | Heredados |
| `SetupAudioLineGuidePopup` | Heredados |
| `SetupAudioTriggerPopup` | Heredados |
| `SetupBGSpeedTrigger` | Heredados |
| `SetupCameraEdgePopup` | `onClose` |
| `SetupCameraGuidePopup` | Heredados |
| `SetupCameraModePopup` | `onClose` |
| `SetupCameraOffsetTrigger` | `onClose` |
| `SetupCameraRotatePopup` | `onClose` |
| `SetupCameraRotatePopup2` | Heredados |
| `SetupCheckpointPopup` | Heredados |
| `SetupCoinLayer` | Heredados |
| `SetupCollisionStateTriggerPopup` | Heredados |
| `SetupCollisionTriggerPopup` | Heredados |
| `SetupCountTriggerPopup` | Heredados |
| `SetupDashRingPopup` | Heredados |
| `SetupEndPopup` | `onClose` |
| `SetupEnterEffectPopup` | `onClose` |
| `SetupEnterTriggerPopup` | Heredados |
| `SetupEventLinkPopup` | Heredados |
| `SetupForceBlockPopup` | Heredados |
| `SetupGameplayOffsetPopup` | Heredados |
| `SetupGradientPopup` | Heredados |
| `SetupGravityModPopup` | `keyBackClicked`, `onClose`, `show` |
| `SetupGravityTriggerPopup` | Heredados |
| `SetupInstantCollisionTriggerPopup` | Heredados |
| `SetupInstantCountPopup` | `onClose` |
| `SetupInteractObjectPopup` | `onClose` |
| `SetupItemCompareTriggerPopup` | Heredados |
| `SetupItemEditTriggerPopup` | Heredados |
| `SetupKeyframeAnimPopup` | Heredados |
| `SetupKeyframePopup` | `onClose` |
| `SetupMGTrigger` | `onClose` |
| `SetupMoveCommandPopup` | Heredados |
| `SetupObjectControlPopup` | Heredados |
| `SetupObjectOptions2Popup` | Heredados |
| `SetupObjectOptionsPopup` | `keyBackClicked`, `keyDown`, `onClose`, `show` |
| `SetupObjectTogglePopup` | `onClose` |
| `SetupOpacityPopup` | `onClose` |
| `SetupOptionsTriggerPopup` | Heredados |
| `SetupPersistentItemTriggerPopup` | Heredados |
| `SetupPickupTriggerPopup` | Heredados |
| `SetupPlatformerEndPopup` | Heredados |
| `SetupPlayerControlPopup` | Heredados |
| `SetupPortalPopup` | `keyBackClicked`, `onClose` |
| `SetupProgressPopup` | Heredados |
| `SetupPulsePopup` | `onClose`, `show` |
| `SetupRandAdvTriggerPopup` | `onClose` |
| `SetupRandTriggerPopup` | `onClose` |
| `SetupResetTriggerPopup` | Heredados |
| `SetupReverbPopup` | `onClose` |
| `SetupRotateCommandPopup` | Heredados |
| `SetupRotateGameplayPopup` | Heredados |
| `SetupRotatePopup` | `onClose` |
| `SetupSFXEditPopup` | Heredados |
| `SetupSFXPopup` | `onClose` |
| `SetupSequenceTriggerPopup` | Heredados |
| `SetupShaderEffectPopup` | `onClose` |
| `SetupShakePopup` | `onClose` |
| `SetupSmartBlockLayer` | `keyBackClicked`, `onClose`, `show` |
| `SetupSmartTemplateLayer` | `keyBackClicked`, `onBack`, `onClose` |
| `SetupSongTriggerPopup` | `onClose` |
| `SetupSpawnParticlePopup` | Heredados |
| `SetupSpawnPopup` | `onClose` |
| `SetupStaticCameraPopup` | Heredados |
| `SetupStopTriggerPopup` | `onClose` |
| `SetupTeleportPopup` | Heredados |
| `SetupTimeWarpPopup` | `onClose` |
| `SetupTimerControlTriggerPopup` | Heredados |
| `SetupTimerEventTriggerPopup` | Heredados |
| `SetupTimerTriggerPopup` | Heredados |
| `SetupTouchTogglePopup` | `onClose` |
| `SetupTransformPopup` | Heredados |
| `SetupTriggerPopup` | `keyBackClicked`, `keyDown`, `onClose`, `show` |
| `SetupZoomTriggerPopup` | `onClose` |
| `ShardsPage` | `keyBackClicked`, `onClose`, `show` |
| `ShareCommentLayer` | `keyBackClicked`, `onClose` |
| `ShareLevelLayer` | `keyBackClicked`, `onClose` |
| `ShareLevelSettingsLayer` | `keyBackClicked`, `onClose` |
| `ShareListLayer` | `keyBackClicked`, `onClose` |
| `SocialLinksPopup` | `keyBackClicked`, `onClose` |
| `SongInfoLayer` | `keyBackClicked`, `onClose` |
| `SongOptionsLayer` | `keyBackClicked`, `onClose` |
| `SongUsagePopup` | `keyBackClicked`, `onClose` |
| `SoundPackDescPopup` | `keyBackClicked`, `onClose` |
| `StarInfoPopup` | `keyBackClicked`, `onClose` |
| `StartPosBrowser` | `keyBackClicked`, `onClose` |
| `StartPosLayer` | `onClose` |
| `TOSPopup` | `keyBackClicked`, `onClose` |
| `TabbedPopupLayer` | Heredados |
| `ThreadDescPopup` | `keyBackClicked`, `onClose` |
| `ThreadMessageOptionsPopup` | `keyBackClicked`, `onClose` |
| `ThreadOptionsPopup` | `keyBackClicked`, `onClose` |
| `ThreadRequestPopup` | `keyBackClicked`, `onClose` |
| `ThreadSearchPopup` | `keyBackClicked`, `onClose` |
| `ThreadViewPopup` | `keyBackClicked`, `keyDown`, `onClose`, `show` |
| `TopArtistsLayer` | `keyBackClicked`, `onClose`, `show` |
| `TouchControlEditLayer` | `keyBackClicked`, `onEnter`, `onExit` |
| `TutorialLayer` | `keyBackClicked`, `onClose` |
| `TutorialPopup` | `animateMenu`, `keyBackClicked`, `show` |
| `UIObjectSettingsPopup` | Heredados |
| `UIOptionsLayer` | `onClose` |
| `UIPOptionsLayer` | `onClose` |
| `UISaveLoadLayer` | Heredados |
| `UpdateAccountSettingsPopup` | `keyBackClicked`, `onClose` |
| `UploadActionPopup` | `keyBackClicked`, `onClose` |
| `UploadListPopup` | `keyBackClicked`, `onBack`, `onClose`, `show` |
| `UploadPopup` | `keyBackClicked`, `onBack`, `onClose`, `show` |
| `VideoOptionsLayer` | `keyBackClicked`, `onClose` |
| `WorldLevelPage` | `keyBackClicked`, `onClose`, `show` |

## Panel desplegable

| Clase | Metodos propios detectados |
| --- | --- |
| `AccountHelpLayer` | `exitLayer`, `layerHidden` |
| `AccountLayer` | `exitLayer`, `layerHidden` |
| `AchievementsLayer` | `keyDown` |
| `EndLevelLayer` | `enterAnimFinished`, `keyBackClicked`, `keyDown`, `keyUp`, `onMenu`, `onReplay`, `showLayer` |
| `GJDropDownLayer` | `enterAnimFinished`, `enterLayer`, `exitLayer`, `hideLayer`, `keyBackClicked`, `layerHidden`, `layerVisible`, `showLayer` |
| `GJMoreGamesLayer` | Heredados |
| `GJSongBrowser` | `exitLayer` |
| `OptionsLayer` | `exitLayer`, `layerHidden` |
| `RetryLevelLayer` | `enterAnimFinished`, `keyBackClicked`, `keyDown`, `keyUp`, `onMenu`, `onReplay`, `showLayer` |
| `SelectSmartTemplateLayer` | Heredados |
| `SlideInLayer` | `enterAnimFinished`, `enterLayer`, `exitLayer`, `hideLayer`, `keyBackClicked`, `layerHidden`, `layerVisible`, `showLayer` |
| `SongsLayer` | Heredados |
| `StatsLayer` | Heredados |
| `SupportLayer` | `exitLayer` |
| `URLViewLayer` | Heredados |

## Panel bloqueante

| Clase | Metodos propios detectados |
| --- | --- |
| `CCBlockLayer` | `enterAnimFinished`, `enterLayer`, `exitLayer`, `hideLayer`, `keyBackClicked`, `layerHidden`, `layerVisible`, `showLayer` |
| `EditorPauseLayer` | `keyBackClicked`, `keyDown`, `onResume` |
| `PauseLayer` | `keyBackClicked`, `keyDown`, `keyUp`, `onReplay`, `onResume` |

## Dialogo

| Clase | Metodos propios detectados |
| --- | --- |
| `DialogLayer` | `addToMainScene`, `animateIn`, `animateInDialog`, `animateInRandomSide`, `fadeInTextFinished`, `finishCurrentAnimation`, `keyBackClicked`, `keyDown`, `onClose`, `onEnter` |

## Escena / navegador superpuesto

| Clase | Metodos propios detectados |
| --- | --- |
| `LevelBrowserLayer` | `exitLayer`, `keyBackClicked`, `keyDown`, `onBack`, `onEnter`, `scene`, `show` |

## Navegacion de escenas

| Clase | Metodos propios detectados |
| --- | --- |
| `CreatorLayer` | `keyBackClicked`, `onBack`, `scene` |
| `EditLevelLayer` | `keyBackClicked`, `keyDown`, `onBack`, `scene` |
| `GJGarageLayer` | `keyBackClicked`, `onBack`, `scene` |
| `GJShopLayer` | `keyBackClicked`, `onBack`, `onExit`, `scene` |
| `GauntletLayer` | `keyBackClicked`, `onBack`, `scene` |
| `GauntletSelectLayer` | `keyBackClicked`, `onBack`, `onExit`, `scene` |
| `GraphicsReloadLayer` | `scene` |
| `LeaderboardsLayer` | `keyBackClicked`, `onBack`, `scene` |
| `LevelAreaInnerLayer` | `keyBackClicked`, `onBack`, `onExit`, `scene` |
| `LevelAreaLayer` | `fadeInsideTower`, `keyBackClicked`, `onBack`, `onExit`, `scene` |
| `LevelInfoLayer` | `keyBackClicked`, `keyDown`, `onBack`, `scene` |
| `LevelListLayer` | `onBack`, `onEnter`, `onExit`, `scene` |
| `LevelSearchLayer` | `keyBackClicked`, `keyDown`, `onBack`, `onClose`, `scene` |
| `LevelSelectLayer` | `keyBackClicked`, `keyDown`, `onBack`, `scene` |
| `MPLobbyLayer` | `keyBackClicked`, `keyDown`, `keyUp`, `onBack`, `onBtn1`, `onBtn2`, `scene` |
| `MapSelectLayer` | `keyBackClicked`, `onBack`, `onExit`, `scene` |
| `MenuLayer` | `keyBackClicked`, `keyDown`, `scene` |
| `MultiplayerLayer` | `keyBackClicked`, `onBack`, `onBtn1`, `onBtn2`, `scene` |
| `SecretLayer` | `keyBackClicked`, `onBack`, `scene` |
| `SecretLayer2` | `keyBackClicked`, `onBack`, `onExit`, `scene` |
| `SecretLayer3` | `animateEyes`, `keyBackClicked`, `onBack`, `onExit`, `scene` |
| `SecretLayer4` | `keyBackClicked`, `onBack`, `onExit`, `scene` |
| `SecretLayer5` | `animateHead`, `fadeInMessage`, `fadeInSubmitMessage`, `fadeOutMessage`, `keyBackClicked`, `onBack`, `onExit`, `scene` |
| `SecretRewardsLayer` | `fadeInMusic`, `fadeInOutMusic`, `keyBackClicked`, `onBack`, `onExit`, `scene` |
| `WorldSelectLayer` | `animateInActiveIsland`, `keyBackClicked`, `onBack`, `onExit`, `scene` |

## Escena de gameplay / editor conservada

| Clase | Metodos propios detectados |
| --- | --- |
| `EditorUI` | `keyBackClicked`, `keyDown`, `keyUp` |
| `GJBaseGameLayer` | `animateInDualGroundNew`, `animateInGroundNew`, `animateOutGroundNew`, `animatePortalY` |
| `LevelEditorLayer` | `scene` |
| `PlayLayer` | `onExit`, `scene` |
| `UILayer` | `keyBackClicked`, `keyDown`, `keyUp` |

## Carga / animacion nativa conservada

| Clase | Metodos propios detectados |
| --- | --- |
| `LoadingCircle` | `fadeAndRemove`, `show` |
| `LoadingLayer` | `scene` |

## Componente / animacion nativa conservada

| Clase | Metodos propios detectados |
| --- | --- |
| `AchievementCell` | Heredados |
| `ArtistCell` | Heredados |
| `AudioEffectsLayer` | Heredados |
| `BoomListLayer` | Heredados |
| `BoomListView` | Heredados |
| `BoomScrollLayer` | Heredados |
| `ButtonPage` | Heredados |
| `CCContentLayer` | Heredados |
| `CCMenuExtra` | Heredados |
| `CCScrollLayerExt` | Heredados |
| `CCTextInputMenu` | Heredados |
| `CCTextInputNode` | `onExit` |
| `CommentCell` | Heredados |
| `CurrencyRewardLayer` | Heredados |
| `CustomListView` | Heredados |
| `CustomMusicCell` | Heredados |
| `CustomSFXCell` | Heredados |
| `CustomSFXRowMenu` | Heredados |
| `CustomSongCell` | Heredados |
| `DpadMenu` | Heredados |
| `DrawGridLayer` | Heredados |
| `EditorSoundConfigCell` | Heredados |
| `ExtendedLayer` | Heredados |
| `FontPreviewCell` | Heredados |
| `GJCommentListLayer` | Heredados |
| `GJCompactUserCell` | Heredados |
| `GJFlyGroundLayer` | Heredados |
| `GJGameLoadingLayer` | `onEnter`, `transitionToLoadingLayer` |
| `GJGradientLayer` | Heredados |
| `GJGroundLayer` | `fadeInFinished`, `fadeInGround`, `fadeOutGround` |
| `GJLevelScoreCell` | Heredados |
| `GJListLayer` | Heredados |
| `GJLocalLevelScoreCell` | Heredados |
| `GJMGLayer` | Heredados |
| `GJMessageCell` | Heredados |
| `GJNotificationCell` | Heredados |
| `GJRequestCell` | Heredados |
| `GJRotationControl` | Heredados |
| `GJScaleControl` | Heredados |
| `GJScoreCell` | Heredados |
| `GJThreadCell` | Heredados |
| `GJTransformControl` | Heredados |
| `GameCell` | Heredados |
| `LevelCell` | Heredados |
| `LevelListCell` | Heredados |
| `LevelPage` | Heredados |
| `ListButtonPage` | Heredados |
| `ListCell` | Heredados |
| `MapPackCell` | Heredados |
| `MenuGameLayer` | Heredados |
| `OptionsCell` | Heredados |
| `ParticlePreviewLayer` | Heredados |
| `ScrollableContentLayer` | Heredados |
| `ScrollingLayer` | Heredados |
| `SecretGame01Layer` | Heredados |
| `SecretLayer6` | `keyBackClicked`, `onBack` |
| `SecretNumberLayer` | Heredados |
| `ShaderLayer` | Heredados |
| `Slider` | Heredados |
| `SliderTouchLogic` | Heredados |
| `SmartTemplateCell` | Heredados |
| `SongCell` | Heredados |
| `StatsCell` | Heredados |
| `TableView` | `onEnter`, `onExit` |
| `TableViewCell` | Heredados |
| `URLCell` | Heredados |
| `cocos2d::CCLayer` | `keyBackClicked`, `keyDown`, `onEnter`, `onExit` |
| `cocos2d::CCLayerColor` | Heredados |
| `cocos2d::CCLayerGradient` | Heredados |
| `cocos2d::CCLayerMultiplex` | Heredados |
| `cocos2d::CCLayerRGBA` | Heredados |
| `cocos2d::CCMenu` | `addChild`, `onExit`, `removeChild` |
| `cocos2d::extension::CCControl` | `onEnter`, `onExit` |
| `cocos2d::extension::CCControlButton` | Heredados |
| `cocos2d::extension::CCControlColourPicker` | Heredados |
| `cocos2d::extension::CCControlHuePicker` | Heredados |
| `cocos2d::extension::CCControlPotentiometer` | Heredados |
| `cocos2d::extension::CCControlSaturationBrightnessPicker` | Heredados |
| `cocos2d::extension::CCControlSlider` | Heredados |
| `cocos2d::extension::CCControlStepper` | Heredados |
| `cocos2d::extension::CCControlSwitch` | Heredados |
| `cocos2d::extension::CCEditBox` | `onEnter`, `onExit` |
| `cocos2d::extension::CCScrollView` | `addChild` |
| `cocos2d::extension::CCTableView` | Heredados |

## Punto comun del motor

| Clase | Metodos propios detectados |
| --- | --- |
| `cocos2d::CCDirector` | `popScene`, `popSceneWithTransition`, `pushScene`, `replaceScene` |
| `cocos2d::CCKeyboardDispatcher` | `dispatchKeyboardMSG` |
| `cocos2d::CCKeypadDispatcher` | `dispatchKeypadMSG` |
| `cocos2d::CCMenuItem` | `activate`, `selected`, `unselected` |
| `cocos2d::CCNode` | `addChild`, `onEnter`, `onExit`, `removeAllChildrenWithCleanup`, `removeChild` |

## Control o efecto / animacion nativa conservada

| Clase | Metodos propios detectados |
| --- | --- |
| `AchievementBar` | `show` |
| `AnimatedGameObject` | `AnimatedGameObject`, `animationFinished`, `animationForID` |
| `AnimatedShopKeeper` | `AnimatedShopKeeper`, `animationFinished` |
| `BonusDropdown` | `show` |
| `CCAnimatedSprite` | `animationFinished`, `animationFinishedO`, `runAnimation`, `runAnimationForced` |
| `CCLightFlash` | `fadeAndRemove` |
| `CCMenuItemSpriteExtra` | `activate`, `selected`, `unselected` |
| `CCMenuItemToggler` | `activate`, `selected`, `unselected` |
| `ConfigureHSVWidget` | `onClose` |
| `DungeonBarsSprite` | `animateOutBars` |
| `EnhancedGameObject` | `animationTriggered` |
| `FMODAudioEngine` | `fadeInBackgroundMusic`, `fadeInMusic`, `fadeMusic`, `fadeOutMusic` |
| `GameManager` | `fadeInMenuMusic`, `fadeInMusic` |
| `GameObject` | `animationTriggered` |
| `InfoAlertButton` | `activate` |
| `LoadingCircleSprite` | `fadeInCircle` |
| `PlayerFireBoostSprite` | `animateFireIn`, `animateFireOut` |
| `PlayerObject` | `animatePlatformerJump`, `animationFinished`, `fadeOutStreak2` |
| `RowPressItem` | `selected`, `unselected` |
| `SectionedContentNode` | `addChild`, `removeAllChildrenWithCleanup` |
| `SpriteAnimationManager` | `animationFinished`, `finishAnimation`, `runAnimation`, `runQueuedAnimation` |
| `TextArea` | `fadeIn`, `fadeInCharacters`, `fadeOut`, `fadeOutAndRemove` |
| `cocos2d::CCClippingNode` | `onEnter`, `onExit` |
| `cocos2d::CCMenuItemLabel` | `activate`, `selected`, `unselected` |
| `cocos2d::CCMenuItemSprite` | `selected`, `unselected` |
| `cocos2d::CCMenuItemToggle` | `activate`, `selected`, `unselected` |
| `cocos2d::CCParallaxNode` | `addChild`, `removeAllChildrenWithCleanup`, `removeChild` |
| `cocos2d::CCParticleBatchNode` | `addChild`, `removeAllChildrenWithCleanup`, `removeChild` |
| `cocos2d::CCSprite` | `addChild`, `removeAllChildrenWithCleanup`, `removeChild` |
| `cocos2d::CCSpriteBatchNode` | `addChild`, `removeAllChildrenWithCleanup`, `removeChild` |
| `cocos2d::CCTMXLayer` | `addChild`, `removeChild` |
| `cocos2d::CCTransitionCrossFade` | `onEnter`, `onExit` |
| `cocos2d::CCTransitionFade` | `onEnter`, `onExit` |
| `cocos2d::CCTransitionFadeTR` | `onEnter` |
| `cocos2d::CCTransitionFlipAngular` | `onEnter` |
| `cocos2d::CCTransitionFlipX` | `onEnter` |
| `cocos2d::CCTransitionFlipY` | `onEnter` |
| `cocos2d::CCTransitionJumpZoom` | `onEnter` |
| `cocos2d::CCTransitionMoveInL` | `onEnter` |
| `cocos2d::CCTransitionPageTurn` | `onEnter` |
| `cocos2d::CCTransitionProgress` | `onEnter`, `onExit` |
| `cocos2d::CCTransitionRotoZoom` | `onEnter` |
| `cocos2d::CCTransitionScene` | `onEnter`, `onExit` |
| `cocos2d::CCTransitionShrinkGrow` | `onEnter` |
| `cocos2d::CCTransitionSlideInL` | `onEnter` |
| `cocos2d::CCTransitionSplitCols` | `onEnter` |
| `cocos2d::CCTransitionTurnOffTiles` | `onEnter` |
| `cocos2d::CCTransitionZoomFlipAngular` | `onEnter` |
| `cocos2d::CCTransitionZoomFlipX` | `onEnter` |
| `cocos2d::CCTransitionZoomFlipY` | `onEnter` |

## Direcciones compartidas en Windows

La lista completa con parametros, archivo, linea y bindings por plataforma esta en `DYNAMIC_TRANSITION_BINDINGS.json`. Estas direcciones explican la eleccion de hooks comunes.

| Direccion | Metodos |
| --- | --- |
| `0x1262f0` | `ColorSelectLiveOverlay::show`, `HSVLiveOverlay::show`, `SetupObjectOptionsPopup::show`, `SetupSmartBlockLayer::show` |
| `0x3a83c0` | `SetTextPopup::show`, `SetupTriggerPopup::show` |
| `0x433f60` | `UploadListPopup::show`, `UploadPopup::show` |
| `0x559380` | `ProfilePage::show`, `RewardsPage::show` |
| `0x9eec0` | `ChallengesPage::show`, `CharacterColorPage::show`, `DailyLevelPage::show`, `FollowRewardPage::show`, `GJPathPage::show`, `GJPathsLayer::show`, `GJPromoPopup::show`, `LevelLeaderboard::show`, `ShardsPage::show`, `TopArtistsLayer::show` |
| `0xc39c0` | `DeathTrackerPopup::show`, `PagedListPopup::show`, `ThreadViewPopup::show` |
