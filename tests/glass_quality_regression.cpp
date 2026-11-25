#include "../src/features/liquid-glass/services/GlassQuality.hpp"
#include "../src/features/liquid-glass/services/GlassStyle.hpp"

#include <cassert>
#include <cmath>
#include <limits>
#include <string_view>

int main() {
    using namespace paimon::glass;
    float const nan = std::numeric_limits<float>::quiet_NaN();

    // Disabled always wins; Auto picks Lite on mobile, Balanced on desktop.
    GlassSettings settings;
    assert(resolveTier(settings, false) == GlassTier::Off);
    settings.enabled = true;
    assert(resolveTier(settings, true) == GlassTier::Lite);
    assert(resolveTier(settings, false) == GlassTier::Balanced);
    settings.tier = 5; // High
    assert(resolveTier(settings, true) == GlassTier::High);
    settings.tier = 2; // Simulated
    assert(resolveTier(settings, false) == GlassTier::Simulated);

    // The popup cycles the saved selectors, whose values are not UI indices.
    int selector = 0;
    for (auto const expected : {GlassTier::High, GlassTier::Balanced,
                               GlassTier::Lite, GlassTier::Simulated}) {
        selector = nextGlassTier(selector);
        settings.tier = selector;
        assert(resolveTier(settings, false) == expected);
    }
    assert(nextGlassTier(selector) == 0);
    assert(std::string_view(glassTierName(5)) == "High");
    assert(std::string_view(glassTierName(4)) == "Balanced");
    assert(std::string_view(glassTierName(3)) == "Lite");
    assert(std::string_view(glassTierName(2)) == "Simulated");

    // NaN from a hand-edited config falls back instead of reaching GL.
    GlassSettings const defaults;
    settings.blur = nan;
    settings.refraction = std::numeric_limits<float>::infinity();
    settings.tintOpacity = 99.f;
    settings.cornerRadius = -5.f;
    settings.tintR = 300;
    auto clean = sanitizeGlassSettings(settings);
    assert(clean.blur == defaults.blur);
    assert(clean.refraction == defaults.refraction);
    assert(clean.tintOpacity == 0.8f);
    assert(clean.cornerRadius == 0.f);
    assert(clean.tintR == 255);
    // New installs reach popups and buttons, with GD colours kept.
    assert(defaults.popups && defaults.buttons && defaults.coloredButtons);

    // Only the live tiers copy the framebuffer, each within a frame budget.
    assert(!profileFor(GlassTier::Off).live);
    assert(!profileFor(GlassTier::Simulated).live);
    assert(profileFor(GlassTier::Simulated).maxCapturesPerFrame == 0);
    auto const lite = profileFor(GlassTier::Lite);
    auto const balanced = profileFor(GlassTier::Balanced);
    auto const high = profileFor(GlassTier::High);
    for (auto const& profile : {lite, balanced, high}) {
        assert(profile.live);
        assert(profile.blurLevels >= 1 && profile.blurLevels <= 4);
        assert(profile.maxCapturesPerFrame >= 1 && profile.maxCapturesPerFrame <= 12);
    }
    // Lite throttles and does not layer panel copies; the others are live.
    assert(lite.refreshSeconds > 0.f && !lite.layered && !lite.dispersion);
    assert(balanced.refreshSeconds == 0.f && balanced.layered);
    assert(high.refreshSeconds == 0.f && high.layered);
    assert(lite.maxCapturesPerFrame < balanced.maxCapturesPerFrame);
    assert(balanced.maxCapturesPerFrame < high.maxCapturesPerFrame);
    assert(lite.blurLevels < balanced.blurLevels && balanced.blurLevels < high.blurLevels);

    // Blur maps to a bounded kawase spread; panels frost more than buttons.
    assert(blurSpreadFor(0.f) > 0.f);
    assert(blurSpreadFor(1.f) <= 3.f);
    assert(blurSpreadFor(nan) == blurSpreadFor(0.65f));
    assert(frostFor(0.f, true) == 0.5f);
    assert(frostFor(1.f, true) == 1.f);
    assert(frostFor(0.f, false) == 0.f);
    assert(frostFor(1.f, false) < frostFor(1.f, true));
    assert(frostFor(nan, true) == frostFor(0.65f, true));

    // Button skins keep their meaning as a tint; gray takes the user tint.
    auto const green = buttonGlassColor("GJ_button_01.png");
    auto const red = buttonGlassColor("GJ_button_06.png");
    assert(green && green->g > green->r && green->g > green->b);
    assert(red && red->r > red->g && red->r > red->b);
    assert(isNeutralButtonFrame("GJ_button_04.png"));
    assert(!isNeutralButtonFrame("GJ_button_01.png"));
    assert(!buttonGlassColor("GJ_square01.png"));

    // UI vs gameplay sheet boundaries: gameplay sheets are never touched.
    assert(isGlassUiSheet("GJ_GameSheet03-uhd.png"));
    assert(isGlassUiSheet("Resources/GJ_GameSheet04-hd.png"));
    assert(isGlassUiSheet("GJ_ShopSheet.png"));
    assert(!isGlassUiSheet("GJ_GameSheet-uhd.png"));
    assert(!isGlassUiSheet("GJ_GameSheet02-uhd.png"));
    assert(!isGlassUiSheet("GJ_GameSheetIcons-uhd.png"));
    assert(isGlassGameplaySheet("GJ_GameSheet-uhd.png"));
    assert(isGlassGameplaySheet("GJ_GameSheet02-hd.png"));
    assert(isGlassGameplaySheet("GJ_GameSheetIcons-uhd.png"));
    assert(isGlassGameplaySheet("FireSheet_01-uhd.png"));
    assert(isGlassGameplaySheet("PixelSheet_01-uhd.png"));
    assert(!isGlassGameplaySheet("GJ_GameSheet03-uhd.png"));

    // Solid UI panels match; hollow GJ_square07 overlay and gameplay textures do not.
    assert(isGlassPanelTexture("GJ_square01.png"));
    assert(isGlassPanelTexture("GJ_square05-uhd.png"));
    assert(isGlassPanelTexture("square02_001-uhd.png"));
    assert(isGlassPanelTexture("square02b_001.png"));
    assert(isGlassPanelTexture("square02_small.png"));
    assert(isGlassPanelTexture("geode.loader/GE_square01.png"));
    assert(!isGlassPanelTexture("GJ_square07.png"));
    assert(!isGlassPanelTexture("gameBG_01_001-uhd.png"));
    assert(isGlassListBorderFrame("GJ_table_top_001.png"));
    assert(isGlassListBorderFrame("GJ_commentSide2_001.png"));
    assert(isSliderGrooveTexture("slidergroove.png"));
    assert(isSliderThumbTexture("sliderthumb.png"));
    assert(!isSliderThumbTexture("player_01_001.png"));

    // Composite GD bases, Geode BasedButtonSprites, utility glyph buttons,
    // and baked UI icon buttons all classify cleanly.
    auto const checkOn = classifyUiButtonFrame("GJ_checkOn_001.png", "GJ_GameSheet03-uhd.png");
    assert(checkOn.match && checkOn.shape == GlassShape::RoundedSquare && checkOn.glyph == GlassGlyph::Check && checkOn.color);
    auto const checkOff = classifyUiButtonFrame("GJ_checkOff_001.png", "GJ_GameSheet03-uhd.png");
    assert(checkOff.match && checkOff.shape == GlassShape::RoundedSquare && checkOff.glyph == GlassGlyph::None && !checkOff.color);
    auto const infoBtn = classifyUiButtonFrame("GJ_infoBtn_001.png", "GJ_GameSheet03-uhd.png");
    assert(infoBtn.match && infoBtn.shape == GlassShape::Circle && infoBtn.glyph == GlassGlyph::Info);
    auto const infoIcon = classifyUiButtonFrame("GJ_infoIcon_001.png", "GJ_GameSheet03-uhd.png");
    assert(infoIcon.match && infoIcon.shape == GlassShape::Circle && infoIcon.glyph == GlassGlyph::Info);
    auto const plusBtn = classifyUiButtonFrame("GJ_plusBtn_001.png", "GJ_GameSheet03-uhd.png");
    assert(plusBtn.match && plusBtn.shape == GlassShape::Circle && plusBtn.glyph == GlassGlyph::Plus);
    auto const plus3Btn = classifyUiButtonFrame("GJ_plus3Btn_001.png", "GJ_GameSheet03-uhd.png");
    assert(plus3Btn.match && plus3Btn.shape == GlassShape::Circle && plus3Btn.glyph == GlassGlyph::Plus);
    auto const minusBtn = classifyUiButtonFrame("GJ_minusBtn_001.png", "GJ_GameSheet03-uhd.png");
    assert(minusBtn.match && minusBtn.shape == GlassShape::Circle && minusBtn.glyph == GlassGlyph::Minus);
    auto const playMusic = classifyUiButtonFrame("GJ_playMusicBtn_001.png", "GJ_GameSheet03-uhd.png");
    assert(playMusic.match && playMusic.shape == GlassShape::Circle && playMusic.glyph == GlassGlyph::Play);
    auto const stopMusic = classifyUiButtonFrame("GJ_stopMusicBtn_001.png", "GJ_GameSheet03-uhd.png");
    assert(stopMusic.match && stopMusic.shape == GlassShape::Circle && stopMusic.glyph == GlassGlyph::Stop);
    auto const downloadBtn = classifyUiButtonFrame("GJ_downloadBtn_001.png", "GJ_GameSheet03-uhd.png");
    assert(downloadBtn.match && downloadBtn.shape == GlassShape::Circle && downloadBtn.glyph == GlassGlyph::Download);
    auto const longBtn = classifyUiButtonFrame("GJ_longBtn01_001.png", "GJ_GameSheet03-uhd.png");
    assert(longBtn.match && longBtn.shape == GlassShape::Pill && longBtn.glyph == GlassGlyph::None &&
           longBtn.requiresOverlayChild);
    auto const plainBtn = classifyUiButtonFrame("GJ_plainBtn_001.png", "GJ_GameSheet03-uhd.png");
    assert(plainBtn.match && plainBtn.shape == GlassShape::RoundedSquare && plainBtn.requiresOverlayChild);
    // GJ_longBtn05/06/07 have baked search/user/list icons and must never be blanked.
    for (char const* bakedLong : {"GJ_longBtn05_001.png", "GJ_longBtn06_001.png", "GJ_longBtn07_001.png"}) {
        auto const spec = classifyUiButtonFrame(bakedLong, "GJ_GameSheet03-uhd.png");
        assert(spec.match && spec.shape == GlassShape::SpriteMask &&
               spec.spriteStyle == GlassSpriteStyle::IconButton && !spec.requiresOverlayChild);
    }
    assert(!checkOff.requiresOverlayChild && !checkOn.requiresOverlayChild);
    auto const thumbSpec = classifyUiButtonFrame("", "sliderthumb.png");
    assert(thumbSpec.match && thumbSpec.shape == GlassShape::Circle && !thumbSpec.requiresOverlayChild);
    auto const navArrow = classifyUiButtonFrame("navArrowBtn_001.png", "GJ_GameSheet03-uhd.png");
    assert(navArrow.match && navArrow.shape == GlassShape::SpriteMask && !navArrow.mirrorArrow);
    auto const geodeCircle = classifyUiButtonFrame("", "geode.loader/baseCircle_Medium_Green.png");
    assert(geodeCircle.match && geodeCircle.shape == GlassShape::Circle && geodeCircle.color &&
           geodeCircle.requiresOverlayChild);
    auto const geodeTab = classifyUiButtonFrame("", "geode.loader/baseTab_Normal_Selected.png");
    assert(geodeTab.match && geodeTab.shape == GlassShape::Pill && geodeTab.color &&
           geodeTab.requiresOverlayChild);
    auto const menuPlay = classifyUiButtonFrame("GJ_playBtn_001.png", "GJ_GameSheet03-uhd.png");
    assert(menuPlay.match && menuPlay.shape == GlassShape::SpriteMask &&
           menuPlay.spriteStyle == GlassSpriteStyle::IconButton);
    auto const optionsBtn = classifyUiButtonFrame("GJ_optionsBtn_001.png", "GJ_GameSheet03-uhd.png");
    assert(optionsBtn.match && optionsBtn.shape == GlassShape::SpriteMask);
    auto const dailyBtn = classifyUiButtonFrame("GJ_dailyBtn_001.png", "GJ_GameSheet04-uhd.png");
    assert(dailyBtn.match && dailyBtn.shape == GlassShape::SpriteMask);
    auto const shipTab = classifyUiButtonFrame("gj_shipBtn_on_001.png", "GJ_GameSheet03-uhd.png");
    assert(shipTab.match && shipTab.shape == GlassShape::SpriteMask);
    auto const topTabOn = classifyUiButtonFrame("top_tab_on_001.png", "GJ_GameSheet03-uhd.png");
    assert(topTabOn.match && topTabOn.shape == GlassShape::SpriteMask &&
           topTabOn.spriteStyle == GlassSpriteStyle::Tab && topTabOn.color);
    auto const friendsTabOff = classifyUiButtonFrame("friends_tab_off_001.png", "GJ_GameSheet03-uhd.png");
    assert(friendsTabOff.match && friendsTabOff.shape == GlassShape::SpriteMask &&
           friendsTabOff.spriteStyle == GlassSpriteStyle::Tab && !friendsTabOff.color);

    // Gameplay assets, player icons, color swatches, difficulty faces and
    // brand logos are strictly rejected.
    assert(!classifyUiButtonFrame("GJ_button_01.png", "GJ_GameSheet-uhd.png").match);
    assert(!classifyUiButtonFrame("player_01_001.png", "GJ_GameSheetIcons-uhd.png").match);
    assert(!classifyUiButtonFrame("GJ_pauseBtn_001.png", "GJ_GameSheet03-uhd.png").match);
    assert(!classifyUiButtonFrame("pBtn_jump_001.png", "GJ_GameSheet03-uhd.png").match);
    assert(!classifyUiButtonFrame("GJ_colorBtn_001.png", "GJ_GameSheet03-uhd.png").match);
    assert(!classifyUiButtonFrame("difficulty_01_btn_001.png", "GJ_GameSheet03-uhd.png").match);
    assert(!classifyUiButtonFrame("GJ_ngBtn_001.png", "GJ_GameSheet03-uhd.png").match);
    assert(!classifyUiButtonFrame("currencyOrbIcon_001.png", "GJ_GameSheet03-uhd.png").match);
}
