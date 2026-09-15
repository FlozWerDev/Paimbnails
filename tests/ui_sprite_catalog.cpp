// Tests del catalogo UI (alcance del tinte: solo UI, nunca gameplay).
//
// Comprueba UiSpriteCatalog con el .cpp real (Geode-free: solo STL):
//   g++ -std=c++17 -O2 -o /tmp/uicat tests/ui_sprite_catalog.cpp && /tmp/uicat
//
// Cada prueba es una funcion bool. main() las ejecuta todas y devuelve 0 si
// pasan o 1 si alguna falla.

#include <iostream>
#include <string>

#include "../src/features/texture-studio/engine/UiSpriteCatalog.cpp"

using namespace paimon::texture_studio;

namespace {

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::cout << "FAIL " << __func__ << ":" << __LINE__ << ": " #cond "\n"; \
        return false; \
    } \
} while (0)

bool test_sheets() {
    CHECK(UiSpriteCatalog::isUiSheet("GJ_GameSheet03"));
    CHECK(UiSpriteCatalog::isUiSheet("GJ_GameSheet04"));
    CHECK(!UiSpriteCatalog::isUiSheet("GJ_GameSheet"));
    CHECK(!UiSpriteCatalog::isUiSheet("GJ_GameSheet02"));
    CHECK(!UiSpriteCatalog::isUiSheet("GJ_GameSheetIcons"));
    CHECK(!UiSpriteCatalog::isUiSheet("FireSheet_01"));
    CHECK(!UiSpriteCatalog::isUiSheet("PixelSheet_01"));
    CHECK(!UiSpriteCatalog::isUiSheet(""));
    CHECK(!UiSpriteCatalog::isUiSheet("SomeModSheet"));

    CHECK(UiSpriteCatalog::isGameplaySheet("GJ_GameSheet"));
    CHECK(UiSpriteCatalog::isGameplaySheet("GJ_GameSheet02"));
    CHECK(UiSpriteCatalog::isGameplaySheet("GJ_GameSheetIcons"));
    CHECK(UiSpriteCatalog::isGameplaySheet("GJ_GameSheetGlow"));
    CHECK(UiSpriteCatalog::isGameplaySheet("FireSheet_01"));
    CHECK(UiSpriteCatalog::isGameplaySheet("PixelSheet_01"));
    CHECK(!UiSpriteCatalog::isGameplaySheet("GJ_GameSheet03"));
    CHECK(!UiSpriteCatalog::isGameplaySheet("GJ_GameSheet04"));
    CHECK(!UiSpriteCatalog::isGameplaySheet(""));
    CHECK(!UiSpriteCatalog::isGameplaySheet("SomeModSheet"));
    return true;
}

bool test_classify_gameplay_sheet_wins() {
    // En hoja de gameplay todo es Gameplay aunque el nombre parezca boton.
    CHECK(UiSpriteCatalog::classify("player_01_001.png", "GJ_GameSheetIcons")
        == SpriteKind::Gameplay);
    CHECK(UiSpriteCatalog::classify("GJ_button_01_001.png", "GJ_GameSheet")
        == SpriteKind::Gameplay);
    CHECK(UiSpriteCatalog::classify("portalshine_001.png", "FireSheet_01")
        == SpriteKind::Gameplay);
    return true;
}

bool test_classify_curated_buttons() {
    // Botones curados de GJ_GameSheet03 (hoja UI), con mayusculas reales.
    CHECK(UiSpriteCatalog::classify("GJ_arrow_01_001.png", "GJ_GameSheet03")
        == SpriteKind::Button);
    CHECK(UiSpriteCatalog::classify("backarrowplain_01_001.png", "GJ_GameSheet03")
        == SpriteKind::Button);
    CHECK(UiSpriteCatalog::classify("GJ_checkon_001.png", "GJ_GameSheet03")
        == SpriteKind::Button);
    CHECK(UiSpriteCatalog::classify("GJ_tabon_001.png", "GJ_GameSheet03")
        == SpriteKind::Button);
    CHECK(UiSpriteCatalog::classify("GJ_chrsel_001.png", "GJ_GameSheet03")
        == SpriteKind::Button);
    CHECK(UiSpriteCatalog::classify("GJ_select_001.png", "GJ_GameSheet03")
        == SpriteKind::Button);
    return true;
}

bool test_classify_button_tokens() {
    // Botones sueltos (PNGs standalone, sin hoja): el token "button" manda.
    CHECK(UiSpriteCatalog::classify("GJ_button_01-uhd.png", "")
        == SpriteKind::Button);
    CHECK(UiSpriteCatalog::classify("GJ_button_04-uhd.png", "")
        == SpriteKind::Button);
    CHECK(UiSpriteCatalog::classify("someBtnThing.png", "")
        == SpriteKind::Button);
    return true;
}

bool test_classify_gameplay_effects_and_meaningful_color() {
    CHECK(UiSpriteCatalog::classify("portalshine_001.png", "GJ_GameSheet03")
        == SpriteKind::Gameplay);
    CHECK(UiSpriteCatalog::classify("explosionicon_001.png", "")
        == SpriteKind::Gameplay);
    // Color con significado (dificultad, badges): nunca se tinta.
    CHECK(UiSpriteCatalog::classify("difficulty_01_001.png", "GJ_GameSheet03")
        == SpriteKind::Other);
    CHECK(UiSpriteCatalog::classify("modbadge_001.png", "")
        == SpriteKind::Other);
    return true;
}

bool test_classify_menu_ui_and_fallback() {
    CHECK(UiSpriteCatalog::classify("topbar_001.png", "GJ_GameSheet03")
        == SpriteKind::MenuUi);
    CHECK(UiSpriteCatalog::classify("levelcomplete_001.png", "")
        == SpriteKind::MenuUi);
    // Iconos de gameplay fuera de hoja: Other (no se pintan).
    CHECK(UiSpriteCatalog::classify("player_01_001.png", "")
        == SpriteKind::Other);
    CHECK(UiSpriteCatalog::classify("ship_01_001.png", "")
        == SpriteKind::Other);
    CHECK(UiSpriteCatalog::classify("gameBG_01_001-uhd.png", "")
        == SpriteKind::Other);
    return true;
}

// Ground truth del .bro + revision pixel a pixel de las hojas oficiales:
// arte con identidad propia (moneda, recompensas, dificultades, cofres,
// marcas) no se tinta aunque viva en hojas de UI o lo consuma una capa UI.
bool test_classify_content_art_stays_vanilla() {
    auto ui = SpriteKind::MenuUi;
    auto other = SpriteKind::Other;
    // Dificultades y demonios.
    CHECK(UiSpriteCatalog::classify("GJ_demonIcon_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("difficulty_01_btn_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("diffIcon_01_btn_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("difficulty_auto_btn_001.png", "GJ_GameSheet03") == other);
    // Moneda y recompensas.
    CHECK(UiSpriteCatalog::classify("currencyOrbIcon_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("currencyDiamondIcon_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("secretCoinUI_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("usercoin_small01_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_coinsIcon_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_diamondsIcon_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_starsIcon_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_sStarsIcon_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_moonsIcon_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_pointsIcon_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_bigStar_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_bigKey_001.png", "GJ_GameSheet03") == other);
    // Shards (arte coloreado por tipo; las etiquetas de texto si se tintan).
    CHECK(UiSpriteCatalog::classify("fireShardBig_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("bonusShardSmall_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("shard_glow_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("tier1Icon_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("fireShardLabel_001.png", "GJ_GameSheet03") == ui);
    // Cofres, cuerdas, coronas, paths: decoracion y recompensas.
    CHECK(UiSpriteCatalog::classify("chest_01_01_001.png", "GJ_ShopSheet") == other);
    CHECK(UiSpriteCatalog::classify("chest_01_03_glow_001.png", "GJ_ShopSheet") == other);
    CHECK(UiSpriteCatalog::classify("chest_glow_bg_001.png", "GJ_ShopSheet") == other);
    CHECK(UiSpriteCatalog::classify("chestPlatform_01_001.png", "TreasureRoomSheet") == other);
    CHECK(UiSpriteCatalog::classify("chestSpecial_01_price_001.png", "TreasureRoomSheet") == other);
    CHECK(UiSpriteCatalog::classify("shopRope2_001.png", "GJ_ShopSheet") == other);
    CHECK(UiSpriteCatalog::classify("chestIcon_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("gj_dailyCrown_001.png", "GJ_GameSheet04") == other);
    CHECK(UiSpriteCatalog::classify("pathIcon_01_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("storeItemIcon_001.png", "GJ_GameSheet03") == other);
    // Marcas sociales, badges de celda, stats con color de estado.
    CHECK(UiSpriteCatalog::classify("gj_discordIcon_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("gj_ytIcon_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_featuredIcon_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_likesIcon_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_completesIcon_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("newMusicIcon_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_hammerIcon_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_smallModeIcon_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("collaborationIcon_001.png", "GJ_GameSheet03") == other);
    // Retratos de dialogo y marcas sueltas: contenido, no chrome.
    CHECK(UiSpriteCatalog::classify("dialogIcon_001.png", "") == other);
    CHECK(UiSpriteCatalog::classify("gk-icon.png", "") == other);
    // Decoracion coloreada con token de mueble: la escalera de bloques no
    // es cromo neutro (verificado pixel a pixel).
    CHECK(UiSpriteCatalog::classify("GJ_sideArt_001.png", "GJ_GameSheet03") == other);
    // Guardianes de la boveda (caras de personaje con ojos que el tinte
    // recolorearia); los candados neutros secretLock01/02/03 siguen en UI.
    CHECK(UiSpriteCatalog::classify("GJ_secretLock_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_secretLock2_001.png", "SecretSheet") == other);
    CHECK(UiSpriteCatalog::classify("GJ_secretLock3_001.png", "SecretSheet") == other);
    CHECK(UiSpriteCatalog::classify("GJ_secretLock4_001.png", "SecretSheet") == other);
    CHECK(UiSpriteCatalog::classify("GJ_secretLock4_eyes_001.png", "SecretSheet") == other);
    // Lienzo runtime (.bro: CCScale9Sprite::create) y brillo de moneda.
    CHECK(UiSpriteCatalog::classify("GJ_square07.png", "") == other);
    CHECK(UiSpriteCatalog::classify("shineBurst_001.png", "") == other);
    // Glow de objetos (.bro: GameObject::addGlow): Gameplay, que tampoco
    // se tinta en ningun alcance. El token va antes que "lock" porque
    // "block" lo contiene.
    CHECK(UiSpriteCatalog::classify("blockOutline_15new.png", "")
        == SpriteKind::Gameplay);
    return true;
}

// Botones con arte pegado en el marco: el cromo se tintaria pero tambien
// el contenido, asi que quedan vainilla (hay override por sprite).
// Barrido adversarial + triaje visual marco por marco sobre las hojas
// oficiales (contacto3/contacto4): caras de dificultad, estrellas de rate,
// cubo de jugador, gemas, trofeos, cofres, coronas, logos y nodos de nivel.
bool test_classify_content_baked_buttons_stay_vanilla() {
    auto other = SpriteKind::Other;
    auto btn = SpriteKind::Button;
    CHECK(UiSpriteCatalog::classify("GJ_normalBtn_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_shardsBtn_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("gj_videoRewardBtn_001.png", "GJ_ShopSheet") == other);
    // Caras de dificultad y demonio en el marco (incluido rate).
    CHECK(UiSpriteCatalog::classify("GJ_rateDiffBtn_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_rateDiffBtn2_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_rateDiffBtnMod_001.png", "GJ_GameSheet03") == other);
    // Estrellas de rate.
    CHECK(UiSpriteCatalog::classify("GJ_starBtn_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_starBtn2_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_starBtnMod_001.png", "GJ_GameSheet03") == other);
    // Cubo de jugador, gemas, trofeos.
    CHECK(UiSpriteCatalog::classify("GJ_garageBtn_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_checkpointBtn_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_practiceBtn_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_levelLeaderboardBtn_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_achBtn_001.png", "GJ_GameSheet03") == other);
    // Cofres y recompensas con texto pegado.
    CHECK(UiSpriteCatalog::classify("GJ_adChestBtn_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_freeChestBtn_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_dailyRewardBtn_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_freeStuffBtn_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_rewardBtn_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_adVideoBtn_001.png", "GJ_GameSheet03") == other);
    // Botones grandes de menu (cromo verde + arte/texto pegados).
    CHECK(UiSpriteCatalog::classify("GJ_dailyBtn_001.png", "GJ_GameSheet04") == other);
    CHECK(UiSpriteCatalog::classify("GJ_weeklyBtn_001.png", "GJ_GameSheet04") == other);
    CHECK(UiSpriteCatalog::classify("levelLeaderboard_globalWeeklyBtn_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_eventBtn_001.png", "GJ_GameSheet04") == other);
    CHECK(UiSpriteCatalog::classify("GJ_featuredBtn_001.png", "GJ_GameSheet04") == other);
    CHECK(UiSpriteCatalog::classify("GJ_mapPacksBtn_001.png", "GJ_GameSheet04") == other);
    CHECK(UiSpriteCatalog::classify("GJ_pathsBtn_001.png", "GJ_GameSheet04") == other);
    CHECK(UiSpriteCatalog::classify("GJ_highscoreBtn_001.png", "GJ_GameSheet04") == other);
    // Nodos de nivel del WorldSheet (calavera sobre pedestal).
    CHECK(UiSpriteCatalog::classify("worldLevelBtn_001.png", "WorldSheet") == other);
    CHECK(UiSpriteCatalog::classify("worldLevelBtn_locked_001.png", "WorldSheet") == other);
    // Logos de marcas pegados.
    CHECK(UiSpriteCatalog::classify("GJ_ngBtn_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_gpBtn_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_gpgBtn_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_ncsLibraryBtn_001.png", "GJ_GameSheet03") == other);
    CHECK(UiSpriteCatalog::classify("GJ_everyplayBtn_001.png", "GJ_GameSheet03") == other);
    // Los botones de verdad siguen siendo botones.
    CHECK(UiSpriteCatalog::classify("GJ_unlockBtn_001.png", "GJ_GameSheet03") == btn);
    // Pestanas de garage: glifo blanco sobre cromo monocromo (verificado
    // pixel a pixel). NO son arte de jugador coloreado como garageBtn.
    CHECK(UiSpriteCatalog::classify("gj_iconBtn_on_001.png", "GJ_GameSheet03") == btn);
    CHECK(UiSpriteCatalog::classify("gj_iconBtn_off_001.png", "GJ_GameSheet03") == btn);
    CHECK(UiSpriteCatalog::classify("gj_shipBtn_on_001.png", "GJ_GameSheet03") == btn);
    CHECK(UiSpriteCatalog::classify("gj_shipBtn_off_001.png", "GJ_GameSheet03") == btn);
    CHECK(UiSpriteCatalog::classify("gj_ballBtn_on_001.png", "GJ_GameSheet03") == btn);
    CHECK(UiSpriteCatalog::classify("gj_birdBtn_on_001.png", "GJ_GameSheet03") == btn);
    CHECK(UiSpriteCatalog::classify("gj_dartBtn_on_001.png", "GJ_GameSheet03") == btn);
    CHECK(UiSpriteCatalog::classify("gj_robotBtn_on_001.png", "GJ_GameSheet03") == btn);
    CHECK(UiSpriteCatalog::classify("gj_spiderBtn_on_001.png", "GJ_GameSheet03") == btn);
    CHECK(UiSpriteCatalog::classify("gj_swingBtn_on_001.png", "GJ_GameSheet03") == btn);
    CHECK(UiSpriteCatalog::classify("gj_swingBtn_off_001.png", "GJ_GameSheet03") == btn);
    CHECK(UiSpriteCatalog::classify("gj_jetpackBtn_on_001.png", "GJ_GameSheet03") == btn);
    CHECK(UiSpriteCatalog::classify("gj_streakBtn_on_001.png", "GJ_GameSheet03") == btn);
    CHECK(UiSpriteCatalog::classify("gj_streakBtn_off_001.png", "GJ_GameSheet03") == btn);
    // Colisiones de substring que deben seguir siendo botones: longBtn,
    // songBtn y swingBtn contienen "ngbtn" pero no son el logo Newgrounds.
    CHECK(UiSpriteCatalog::classify("GJ_longBtn01_001.png", "GJ_GameSheet03") == btn);
    CHECK(UiSpriteCatalog::classify("GJ_changeSongBtn_001.png", "GJ_GameSheet03") == btn);
    CHECK(UiSpriteCatalog::classify("GJ_deleteSongBtn_001.png", "GJ_GameSheet03") == btn);
    return true;
}

// Mobiliario de menus verificado visualmente: controles, contenedores,
// cerraduras, seleccion y chrome standalone del .bro.
bool test_classify_menu_furniture() {
    auto ui = SpriteKind::MenuUi;
    CHECK(UiSpriteCatalog::classify("folderIcon_001.png", "GJ_GameSheet03") == ui);
    CHECK(UiSpriteCatalog::classify("GJ_deleteIcon_001.png", "GJ_GameSheet03") == ui);
    CHECK(UiSpriteCatalog::classify("GJ_deleteAllIcon_001.png", "GJ_GameSheet03") == ui);
    CHECK(UiSpriteCatalog::classify("GJ_filterIcon_001.png", "GJ_GameSheet03") == ui);
    CHECK(UiSpriteCatalog::classify("GJ_infoIcon_001.png", "GJ_GameSheet03") == ui);
    CHECK(UiSpriteCatalog::classify("GJ_sortIcon_001.png", "GJ_GameSheet03") == ui);
    CHECK(UiSpriteCatalog::classify("GJ_sLikeIcon_001.png", "GJ_GameSheet03") == ui);
    CHECK(UiSpriteCatalog::classify("GJ_sRecentIcon_001.png", "GJ_GameSheet03") == ui);
    CHECK(UiSpriteCatalog::classify("GJ_sTrendingIcon_001.png", "GJ_GameSheet03") == ui);
    CHECK(UiSpriteCatalog::classify("GJ_sDownloadIcon_001.png", "GJ_GameSheet03") == ui);
    CHECK(UiSpriteCatalog::classify("GJ_sModIcon_001.png", "GJ_GameSheet03") == ui);
    CHECK(UiSpriteCatalog::classify("GJ_musicIcon_001.png", "GJ_GameSheet03") == ui);
    CHECK(UiSpriteCatalog::classify("GJ_noteIcon_001.png", "GJ_GameSheet03") == ui);
    CHECK(UiSpriteCatalog::classify("GJ_timeIcon_001.png", "GJ_GameSheet03") == ui);
    CHECK(UiSpriteCatalog::classify("GJ_extendedIcon_001.png", "GJ_GameSheet03") == ui);
    CHECK(UiSpriteCatalog::classify("secretLock01_001.png", "SecretSheet") == ui);
    CHECK(UiSpriteCatalog::classify("secretLock02_001.png", "SecretSheet") == ui);
    CHECK(UiSpriteCatalog::classify("secretLock03_001.png", "SecretSheet") == ui);
    CHECK(UiSpriteCatalog::classify("gauntletLock_001.png", "GauntletSheet") == ui);
    CHECK(UiSpriteCatalog::classify("towerDoor_locked_001.png", "TowerSheet") == ui);
    CHECK(UiSpriteCatalog::classify("towerDoor_unlocked_001.png", "TowerSheet") == ui);
    // Puertas interactivas (token "door"): la variante abierta no trae
    // "lock" en el nombre pero es la misma puerta.
    CHECK(UiSpriteCatalog::classify("towerDoor_open_001.png", "TowerSheet") == ui);
    CHECK(UiSpriteCatalog::classify("theTowerDoor_001.png", "GJ_GameSheet04") == ui);
    CHECK(UiSpriteCatalog::classify("secretDoor_closed_001.png", "SecretSheet") == ui);
    CHECK(UiSpriteCatalog::classify("secretDoor_open_001.png", "SecretSheet") == ui);
    CHECK(UiSpriteCatalog::classify("difficultySelected_001.png", "GJ_GameSheet03") == ui);
    // Chrome standalone (.bro: Slider::create, loading UI, page dots).
    CHECK(UiSpriteCatalog::classify("sliderBar.png", "") == ui);
    CHECK(UiSpriteCatalog::classify("slidergroove.png", "") == ui);
    CHECK(UiSpriteCatalog::classify("sliderthumb.png", "") == ui);
    CHECK(UiSpriteCatalog::classify("sliderthumbsel.png", "") == ui);
    CHECK(UiSpriteCatalog::classify("loadingCircle.png", "") == ui);
    CHECK(UiSpriteCatalog::classify("smallDot.png", "") == ui);
    CHECK(UiSpriteCatalog::classify("GJ_progressBar_001.png", "") == ui);
    // Lienzo de recolor en runtime (.bro: CustomSongWidget::addExtraVisuals
    // le aplica setColor amarillo): pre-tintarlo seria doble tinte.
    CHECK(UiSpriteCatalog::classify("GJ_square07.png", "")
        == SpriteKind::Other);
    // Rellenos solidos genericos sin evidencia de uso exclusivo en menus.
    CHECK(UiSpriteCatalog::classify("GJ_square01.png", "")
        == SpriteKind::Other);
    CHECK(UiSpriteCatalog::classify("GJ_squareB_01.png", "")
        == SpriteKind::Other);
    return true;
}

bool test_should_tint_scopes() {
    using K = SpriteKind;
    using S = TintScope;
    CHECK(UiSpriteCatalog::shouldTint(K::Button, S::ButtonsOnly));
    CHECK(!UiSpriteCatalog::shouldTint(K::MenuUi, S::ButtonsOnly));
    CHECK(!UiSpriteCatalog::shouldTint(K::Gameplay, S::ButtonsOnly));
    CHECK(!UiSpriteCatalog::shouldTint(K::Other, S::ButtonsOnly));

    CHECK(UiSpriteCatalog::shouldTint(K::Button, S::ButtonsAndMenuUi));
    CHECK(UiSpriteCatalog::shouldTint(K::MenuUi, S::ButtonsAndMenuUi));
    CHECK(!UiSpriteCatalog::shouldTint(K::Gameplay, S::ButtonsAndMenuUi));
    CHECK(!UiSpriteCatalog::shouldTint(K::Other, S::ButtonsAndMenuUi));
    return true;
}

bool test_should_tint_legacy_everything_is_ui_only() {
    // Everything es legacy: ya NO pinta el juego entero. Como maximo pinta
    // lo mismo que ButtonsAndMenuUi.
    using K = SpriteKind;
    using S = TintScope;
    CHECK(UiSpriteCatalog::shouldTint(K::Button, S::Everything));
    CHECK(UiSpriteCatalog::shouldTint(K::MenuUi, S::Everything));
    CHECK(!UiSpriteCatalog::shouldTint(K::Gameplay, S::Everything));
    CHECK(!UiSpriteCatalog::shouldTint(K::Other, S::Everything));
    return true;
}

bool test_standalone_button_gate() {
    // La puerta del exportador para PNGs sueltos: el boton de menu pasa con
    // el alcance por defecto (ButtonsOnly), el arte de gameplay no.
    auto btn = UiSpriteCatalog::classify("GJ_button_01-uhd.png", "");
    CHECK(btn == SpriteKind::Button);
    CHECK(UiSpriteCatalog::shouldTint(btn, TintScope::ButtonsOnly));

    auto bg = UiSpriteCatalog::classify("gameBG_01_001-uhd.png", "");
    CHECK(!UiSpriteCatalog::shouldTint(bg, TintScope::ButtonsOnly));
    CHECK(!UiSpriteCatalog::shouldTint(bg, TintScope::ButtonsAndMenuUi));
    CHECK(!UiSpriteCatalog::shouldTint(bg, TintScope::Everything));
    return true;
}

}  // namespace

int main() {
    int ok = 0, total = 0;
#define RUN(fn) do { \
    ++total; \
    if (fn()) { ++ok; std::cout << "OK   " #fn "\n"; } \
    else { std::cout << "FAIL " #fn "\n"; } \
} while (0)

    RUN(test_sheets);
    RUN(test_classify_gameplay_sheet_wins);
    RUN(test_classify_curated_buttons);
    RUN(test_classify_button_tokens);
    RUN(test_classify_gameplay_effects_and_meaningful_color);
    RUN(test_classify_menu_ui_and_fallback);
    RUN(test_classify_content_art_stays_vanilla);
    RUN(test_classify_content_baked_buttons_stay_vanilla);
    RUN(test_classify_menu_furniture);
    RUN(test_should_tint_scopes);
    RUN(test_should_tint_legacy_everything_is_ui_only);
    RUN(test_standalone_button_gate);

    std::cout << ok << "/" << total << " OK\n";
    return ok == total ? 0 : 1;
}
