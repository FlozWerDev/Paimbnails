# Inventario de Dynamic Transition

Fuente: `build-win/_deps/bindings-src/bindings/2.2081/Entry.bro` y todos sus includes.

Se revisaron **940 clases** y se extrajeron **380 candidatos** de interfaz, controles y efectos. Este inventario describe cobertura por herencia y puntos de entrada; la validacion visual en Geometry Dash sigue pendiente.

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
| Panel desplegable | 14 |
| Punto comun del motor | 5 |
| Escena de gameplay / editor conservada | 5 |
| Carga / animacion nativa conservada | 2 |
| Popup del juego | 201 |
| Navegacion de escenas | 26 |
| Componente / animacion nativa conservada | 74 |
| Control o efecto / animacion nativa conservada | 48 |

## Popup del juego

| Clase | Metodos propios detectados |
| --- | --- |
| `AccountLoginLayer` | `keyBackClicked`, `onClose` |
| `AccountRegisterLayer` | `keyBackClicked`, `onClose` |
| `AudioAssetsBrowser` | `keyBackClicked`, `onClose` |
| `BrowseSmartKeyLayer` | `onBack` |
| `BrowseSmartTemplateLayer` | `keyBackClicked`, `onBack`, `onClose` |
| `ChallengesPage` | `keyBackClicked`, `onClose`, `show` |
| `CharacterColorPage` | `keyBackClicked`, `onClose`, `show` |
| `CollisionBlockPopup` | `keyBackClicked`, `onClose`, `show` |
| `ColorSelectLiveOverlay` | `keyBackClicked`, `show` |
| `ColorSelectPopup` | `keyBackClicked`, `show` |
| `CommunityCreditsPage` | `keyBackClicked`, `onClose`, `show` |
| `ConfigureValuePopup` | `keyBackClicked`, `onClose` |
| `CreateGuidelinesLayer` | `keyBackClicked`, `keyDown`, `keyUp`, `onClose` |
| `CreateParticlePopup` | `keyBackClicked`, `onClose` |
| `CustomSongLayer` | `keyBackClicked`, `onClose`, `show` |
| `CustomizeObjectLayer` | `keyBackClicked`, `onClose` |
| `CustomizeObjectSettingsPopup` | `onClose` |
| `DailyLevelPage` | `keyBackClicked`, `onClose`, `show` |
| `DemonFilterSelectLayer` | `keyBackClicked`, `onClose` |
| `DemonInfoPopup` | `keyBackClicked`, `onClose` |
| `EditGameObjectPopup` | Heredados |
| `EditTriggersPopup` | `onClose` |
| `EditorOptionsLayer` | `onClose` |
| `FLAlertLayer` | `keyBackClicked`, `keyDown`, `onBtn1`, `onBtn2`, `onEnter`, `show` |
| `FRequestProfilePage` | `keyBackClicked`, `onClose` |
| `FindBPMLayer` | `onClose` |
| `FindObjectPopup` | Heredados |
| `FollowRewardPage` | `keyBackClicked`, `onClose`, `show` |
| `FriendRequestPopup` | `keyBackClicked`, `onClose` |
| `FriendsProfilePage` | `keyBackClicked`, `onClose` |
| `GJAccountSettingsLayer` | `keyBackClicked`, `onClose` |
| `GJColorSetupLayer` | `keyBackClicked`, `onClose` |
| `GJFollowCommandLayer` | `onClose` |
| `GJMessagePopup` | `keyBackClicked`, `onClose` |
| `GJOptionsLayer` | Heredados |
| `GJPFollowCommandLayer` | `onClose` |
| `GJPathPage` | `keyBackClicked`, `onBack`, `show` |
| `GJPathRewardPopup` | `keyBackClicked` |
| `GJPathsLayer` | `keyBackClicked`, `onClose`, `onExit`, `show` |
| `GJPromoPopup` | `keyBackClicked`, `onClose`, `onExit`, `show` |
| `GJRateLevelLayer` | `keyBackClicked`, `onClose` |
| `GJRotateCommandLayer` | `onClose` |
| `GJSpecialColorSelect` | `keyBackClicked`, `onClose` |
| `GJWriteMessagePopup` | `keyBackClicked`, `onClose` |
| `GameLevelOptionsLayer` | Heredados |
| `GameOptionsLayer` | Heredados |
| `HSVLiveOverlay` | `keyBackClicked`, `show` |
| `HSVWidgetPopup` | `keyBackClicked`, `onClose` |
| `InfoLayer` | `keyBackClicked`, `onClose`, `show` |
| `ItemInfoPopup` | `keyBackClicked`, `onClose` |
| `KeybindingsLayer` | `keyBackClicked`, `onClose` |
| `LevelFeatureLayer` | `keyBackClicked`, `onClose` |
| `LevelLeaderboard` | `keyBackClicked`, `onClose`, `show` |
| `LevelOptionsLayer` | Heredados |
| `LevelOptionsLayer2` | Heredados |
| `LevelSettingsLayer` | `keyBackClicked`, `onClose` |
| `LikeItemLayer` | `keyBackClicked`, `onClose` |
| `MessagesProfilePage` | `keyBackClicked`, `onClose` |
| `MoreOptionsLayer` | `keyBackClicked`, `onClose` |
| `MoreSearchLayer` | `keyBackClicked`, `onClose` |
| `MoreVideoOptionsLayer` | `keyBackClicked`, `onClose` |
| `MultiTriggerPopup` | Heredados |
| `MusicBrowser` | `keyBackClicked`, `onClose` |
| `NCSInfoLayer` | `keyBackClicked`, `onClose` |
| `NewgroundsInfoLayer` | `keyBackClicked`, `onClose` |
| `NumberInputLayer` | `keyBackClicked`, `onClose` |
| `OptionsScrollLayer` | `keyBackClicked`, `onClose` |
| `ParentalOptionsLayer` | `keyBackClicked`, `onClose` |
| `ProfilePage` | `keyBackClicked`, `onClose`, `show` |
| `PromoInterstitial` | `keyBackClicked`, `onClose`, `show` |
| `PurchaseItemPopup` | `keyBackClicked`, `onClose` |
| `RateDemonLayer` | `keyBackClicked`, `onClose` |
| `RateLevelLayer` | `keyBackClicked`, `onClose` |
| `RateStarsLayer` | `keyBackClicked`, `onClose` |
| `RewardUnlockLayer` | `keyBackClicked`, `onClose` |
| `RewardsPage` | `keyBackClicked`, `onClose`, `show` |
| `SFXBrowser` | `keyBackClicked`, `onClose` |
| `SearchSFXPopup` | Heredados |
| `SelectArtLayer` | `keyBackClicked`, `onClose` |
| `SelectEventLayer` | `keyBackClicked`, `onClose` |
| `SelectFontLayer` | `keyBackClicked`, `onClose` |
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
| `SetupCollisionTriggerPopup` | `onClose` |
| `SetupCountTriggerPopup` | `onClose` |
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
| `SetupObjectOptionsPopup` | `keyBackClicked`, `onClose`, `show` |
| `SetupObjectTogglePopup` | `onClose` |
| `SetupOpacityPopup` | `onClose` |
| `SetupOptionsTriggerPopup` | Heredados |
| `SetupPersistentItemTriggerPopup` | Heredados |
| `SetupPickupTriggerPopup` | Heredados |
| `SetupPlatformerEndPopup` | Heredados |
| `SetupPlayerControlPopup` | Heredados |
| `SetupPortalPopup` | `keyBackClicked`, `onClose` |
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
| `SetupTriggerPopup` | `keyBackClicked`, `onClose`, `show` |
| `SetupZoomTriggerPopup` | `onClose` |
| `ShardsPage` | `keyBackClicked`, `onClose`, `show` |
| `ShareCommentLayer` | `keyBackClicked`, `onClose` |
| `ShareLevelLayer` | `keyBackClicked`, `onClose` |
| `ShareLevelSettingsLayer` | `keyBackClicked`, `onClose` |
| `ShareListLayer` | `keyBackClicked`, `onClose` |
| `SongInfoLayer` | `keyBackClicked`, `onClose` |
| `SongOptionsLayer` | `keyBackClicked`, `onClose` |
| `StarInfoPopup` | `keyBackClicked`, `onClose` |
| `TOSPopup` | `keyBackClicked`, `onClose` |
| `TopArtistsLayer` | `keyBackClicked`, `onClose`, `show` |
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
| `LevelSearchLayer` | `keyBackClicked`, `onBack`, `onClose`, `scene` |
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
| `SecretLayer6` | `keyBackClicked`, `onBack`, `scene` |
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
| `CCScrollLayerExt` | Heredados |
| `CCTextInputNode` | Heredados |
| `CommentCell` | Heredados |
| `CurrencyRewardLayer` | Heredados |
| `CustomListView` | Heredados |
| `CustomMusicCell` | Heredados |
| `CustomSFXCell` | Heredados |
| `CustomSongCell` | Heredados |
| `DrawGridLayer` | Heredados |
| `ExtendedLayer` | Heredados |
| `GJCommentListLayer` | Heredados |
| `GJFlyGroundLayer` | Heredados |
| `GJGameLoadingLayer` | `onEnter`, `transitionToLoadingLayer` |
| `GJGradientLayer` | Heredados |
| `GJGroundLayer` | `fadeInFinished`, `fadeInGround`, `fadeOutGround` |
| `GJLevelScoreCell` | Heredados |
| `GJListLayer` | Heredados |
| `GJLocalLevelScoreCell` | Heredados |
| `GJMGLayer` | Heredados |
| `GJMessageCell` | Heredados |
| `GJRequestCell` | Heredados |
| `GJRotationControl` | Heredados |
| `GJScaleControl` | Heredados |
| `GJScoreCell` | Heredados |
| `GJTransformControl` | Heredados |
| `GJUserCell` | Heredados |
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
| `ScrollingLayer` | Heredados |
| `SecretGame01Layer` | Heredados |
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
| `0x30bc70` | `UploadListPopup::show`, `UploadPopup::show` |
| `0x3c53b0` | `ProfilePage::show`, `RewardsPage::show` |
| `0x867f0` | `ChallengesPage::show`, `CharacterColorPage::show`, `DailyLevelPage::show`, `FollowRewardPage::show`, `GJPathPage::show`, `GJPathsLayer::show`, `GJPromoPopup::show`, `LevelLeaderboard::show`, `ShardsPage::show`, `TopArtistsLayer::show` |
| `0x8baf0` | `CollisionBlockPopup::show`, `ColorSelectLiveOverlay::show`, `HSVLiveOverlay::show`, `SetTextPopup::show`, `SetupObjectOptionsPopup::show`, `SetupSmartBlockLayer::show`, `SetupTriggerPopup::show` |
