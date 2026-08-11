#include "CursorClickFX.hpp"

#include <Geode/binding/FMODAudioEngine.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;
using namespace cocos2d;

namespace paimon::cursorfx {

namespace {

// Bound pools for dense clicks layered over hold effects.
constexpr int kMaxClickParticles = 260;
constexpr int kMaxRings          = 12;
constexpr int kMaxBolts          = 24;

// Burst recipe; rings, flashes, and bolts are requested separately.
struct BurstSpec {
    int   tex      = TexDot;
    int   count    = 16;      // base particle count
    float speedMin = 60.f, speedMax = 220.f;
    int   dirMode  = 0;       // 0 radial, 1 upward cone, 2 wide upward fan
    float gravity  = 0.f;     // px/s²; positive is upward
    float drag     = 1.6f;
    float sizeMin  = 0.7f, sizeMax = 1.3f;
    float growth   = 1.f;     // end-of-life scale
    float spin     = 0.f;     // degrees/s
    float sway     = 0.f;
    float twinkle  = 0.f;     // flicker Hz
    float spiral   = 0.f;
    float lifeMul  = 1.f;
    float fadeIn   = 0.05f;
    bool  alignVel = false;   // align to velocity
    int   rings    = 0;       // accompanying rings
    int   ringKind = 0;       // 0 ring, 1 flash, 2 magic circle
    int   bolts    = 0;       // radial bolts
};

BurstSpec const& burstSpecFor(ClickBurst effect) {
    static const std::array<BurstSpec, kClickBurstCount> kSpecs = [] {
        std::array<BurstSpec, kClickBurstCount> s{};
        auto set = [&](ClickBurst e) -> BurstSpec& { return s[static_cast<size_t>(e)]; };

        auto& ripple = set(ClickBurst::Ripple);
        ripple = {TexDot, 0, 0.f, 0.f, 0, 0.f, 0.f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f,
                  1.f, 0.05f, false, 1, 0, 0};

        auto& shock = set(ClickBurst::Shockwave);
        shock = {TexSpark, 8, 140.f, 300.f, 0, 0.f, 5.0f, 0.55f, 1.05f, 0.4f, 0.f, 0.f,
                 0.f, 0.f, 0.55f, 0.02f, false, 3, 0, 0};

        auto& hearts = set(ClickBurst::Hearts);
        hearts = {TexHeart, 14, 70.f, 235.f, 0, 70.f, 2.3f, 0.7f, 1.35f, 1.f, 90.f,
                  26.f, 0.f, 0.f, 1.25f, 0.05f, false, 0, 0, 0};

        auto& stars = set(ClickBurst::Stars);
        stars = {TexStar, 13, 110.f, 330.f, 0, -420.f, 1.3f, 0.95f, 1.75f, 0.85f, 260.f,
                 0.f, 3.0f, 0.f, 1.f, 0.03f, false, 0, 0, 0};

        auto& confetti = set(ClickBurst::Confetti);
        confetti = {TexSquare, 24, 130.f, 380.f, 0, -720.f, 1.0f, 0.8f, 1.5f, 1.f,
                    520.f, 42.f, 0.f, 0.f, 1.35f, 0.02f, false, 0, 0, 0};

        auto& sparks = set(ClickBurst::Sparks);
        sparks = {TexSpark, 26, 150.f, 460.f, 0, -900.f, 2.8f, 0.65f, 1.35f, 0.5f,
                  0.f, 0.f, 0.f, 0.f, 0.7f, 0.02f, true, 0, 0, 0};

        auto& firework = set(ClickBurst::Firework);
        firework = {TexSpark, 32, 190.f, 430.f, 0, -280.f, 1.1f, 0.7f, 1.4f, 0.5f,
                    0.f, 0.f, 4.5f, 0.f, 1.25f, 0.02f, false, 1, 1, 0};

        auto& bubbles = set(ClickBurst::Bubbles);
        bubbles = {TexRing, 13, 35.f, 130.f, 0, 95.f, 1.3f, 0.6f, 1.4f, 1.35f, 0.f,
                   30.f, 0.f, 0.f, 1.5f, 0.08f, false, 0, 0, 0};

        auto& snow = set(ClickBurst::Snow);
        snow = {TexFlake, 15, 40.f, 165.f, 0, -95.f, 1.5f, 0.55f, 1.15f, 1.f, 70.f,
                46.f, 0.f, 0.f, 1.7f, 0.06f, false, 0, 0, 0};

        auto& ink = set(ClickBurst::Ink);
        ink = {TexSplat, 11, 45.f, 190.f, 0, 0.f, 4.5f, 0.65f, 1.5f, 1.6f, 40.f, 0.f,
               0.f, 0.f, 1.4f, 0.02f, false, 0, 0, 0};

        auto& lightning = set(ClickBurst::Lightning);
        lightning = {TexSpark, 10, 120.f, 320.f, 0, -260.f, 2.5f, 0.6f, 1.15f, 0.5f,
                     0.f, 0.f, 6.f, 0.f, 0.5f, 0.02f, false, 0, 0, 7};

        auto& magic = set(ClickBurst::MagicCircle);
        magic = {TexSpark, 16, 20.f, 90.f, 0, 0.f, 1.2f, 0.6f, 1.3f, 0.6f, 0.f, 0.f,
                 3.5f, 220.f, 1.4f, 0.06f, false, 1, 2, 0};

        auto& pixels = set(ClickBurst::Pixels);
        pixels = {TexSquare, 22, 90.f, 300.f, 0, 0.f, 3.6f, 0.85f, 1.5f, 0.35f, 0.f,
                  0.f, 0.f, 0.f, 0.85f, 0.f, false, 0, 0, 0};

        auto& smoke = set(ClickBurst::Smoke);
        smoke = {TexPuff, 11, 30.f, 120.f, 0, 55.f, 2.2f, 0.8f, 1.5f, 2.5f, 30.f,
                 18.f, 0.f, 0.f, 1.5f, 0.14f, false, 0, 0, 0};

        auto& coins = set(ClickBurst::Coins);
        coins = {TexCoin, 11, 170.f, 360.f, 1, -840.f, 0.7f, 0.6f, 1.f, 1.f, 320.f,
                 0.f, 0.f, 0.f, 1.3f, 0.02f, false, 0, 0, 0};

        auto& notes = set(ClickBurst::Notes);
        notes = {TexNote, 10, 60.f, 180.f, 2, 45.f, 1.6f, 0.7f, 1.2f, 1.f, 55.f,
                 34.f, 0.f, 0.f, 1.45f, 0.06f, false, 0, 0, 0};

        auto& petals = set(ClickBurst::Petals);
        petals = {TexPetal, 15, 70.f, 230.f, 0, -140.f, 1.6f, 0.65f, 1.25f, 1.f,
                  150.f, 50.f, 0.f, 0.f, 1.6f, 0.05f, false, 0, 0, 0};

        auto& bloom = set(ClickBurst::Bloom);
        bloom = {TexPetal, 12, 130.f, 190.f, 0, 0.f, 6.0f, 0.8f, 1.4f, 1.5f, 0.f,
                 0.f, 0.f, 0.f, 1.2f, 0.04f, true, 1, 0, 0};

        auto& galaxy = set(ClickBurst::Galaxy);
        galaxy = {TexSpark, 22, 25.f, 130.f, 0, 0.f, 1.1f, 0.55f, 1.4f, 0.7f, 0.f,
                  10.f, 4.f, 190.f, 1.5f, 0.05f, false, 0, 0, 0};

        auto& fireball = set(ClickBurst::Fireball);
        fireball = {TexGlow, 18, 60.f, 240.f, 0, 130.f, 2.4f, 0.8f, 1.6f, 1.9f, 0.f,
                    16.f, 0.f, 0.f, 1.15f, 0.02f, false, 1, 1, 0};

        return s;
    }();
    int i = static_cast<int>(effect);
    if (i < 0 || i >= kClickBurstCount) i = 0;
    return kSpecs[static_cast<size_t>(i)];
}

// Hold-effect recipe; mode selects particles, per-frame geometry, or rings.
struct HoldSpec {
    int   mode     = 0;
    int   tex      = TexDot;
    float rate     = 0.f;     // particles/s
    float speedMin = 0.f, speedMax = 0.f;
    int   dirMode  = 0;
    float gravity  = 0.f;
    float drag     = 1.f;
    float sizeMin  = 0.6f, sizeMax = 1.2f;
    float growth   = 1.f;
    float spin     = 0.f;
    float sway     = 0.f;
    float twinkle  = 0.f;
    float lifeMul  = 1.f;
    float radius   = 0.f;     // spawn radius
};

HoldSpec const& holdSpecFor(ClickHold effect) {
    static const std::array<HoldSpec, kClickHoldCount> kSpecs = [] {
        std::array<HoldSpec, kClickHoldCount> s{};
        auto set = [&](ClickHold e) -> HoldSpec& { return s[static_cast<size_t>(e)]; };

        set(ClickHold::Aura)       = {1};
        set(ClickHold::ChargeRing) = {1};
        set(ClickHold::Orbit)      = {1};
        set(ClickHold::Electric)   = {1};
        set(ClickHold::Pulse)      = {2};

        set(ClickHold::Vortex) = {0, TexSpark, 46.f, 60.f, 130.f, 0, 0.f, 0.f,
                                  0.55f, 1.15f, 0.6f, 0.f, 0.f, 3.f, 0.55f, 34.f};
        set(ClickHold::Drip) = {0, TexDrop, 14.f, 5.f, 30.f, 3, -430.f, 0.f,
                                0.5f, 0.9f, 1.f, 0.f, 0.f, 0.f, 1.1f, 4.f};
        set(ClickHold::Flame) = {0, TexGlow, 62.f, 30.f, 85.f, 1, 190.f, 1.1f,
                                 0.6f, 1.15f, 0.35f, 0.f, 14.f, 0.f, 0.7f, 3.f};
        set(ClickHold::Sparkle) = {0, TexSpark, 26.f, 4.f, 26.f, 0, 0.f, 0.f,
                                   0.7f, 1.45f, 0.4f, 0.f, 0.f, 4.5f, 0.85f, 20.f};

        return s;
    }();
    int i = static_cast<int>(effect);
    if (i < 0 || i >= kClickHoldCount) i = 0;
    return kSpecs[static_cast<size_t>(i)];
}

char const* soundFileFor(ClickSound sound) {
    switch (sound) {
        case ClickSound::Click:       return "chestClick.ogg";
        case ClickSound::Pop:         return "playSound_01.ogg";
        case ClickSound::Coin:        return "gold01.ogg";
        case ClickSound::Crystal:     return "crystal01.ogg";
        case ClickSound::Achievement: return "achievement_01.ogg";
        case ClickSound::Explosion:   return "explode_11.ogg";
        case ClickSound::Magic:       return "magicExplosion.ogg";
        case ClickSound::Key:         return "secretKey.ogg";
        case ClickSound::Chest:       return "chest01.ogg";
        case ClickSound::Score:       return "highscoreGet02.ogg";
        default:                      return nullptr;
    }
}

float easeOut(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return 1.f - (1.f - t) * (1.f - t) * (1.f - t);
}

float smoothStep(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

}


char const* clickBurstName(ClickBurst effect) {
    switch (effect) {
        case ClickBurst::None:        return "Ninguno";
        case ClickBurst::Ripple:      return "Onda";
        case ClickBurst::Shockwave:   return "Impacto";
        case ClickBurst::Hearts:      return "Corazones";
        case ClickBurst::Stars:       return "Estrellas";
        case ClickBurst::Confetti:    return "Confeti";
        case ClickBurst::Sparks:      return "Chispas";
        case ClickBurst::Firework:    return "Fuego artificial";
        case ClickBurst::Bubbles:     return "Burbujas";
        case ClickBurst::Snow:        return "Nieve";
        case ClickBurst::Ink:         return "Tinta";
        case ClickBurst::Lightning:   return "Rayos";
        case ClickBurst::MagicCircle: return "Circulo magico";
        case ClickBurst::Pixels:      return "Pixeles";
        case ClickBurst::Smoke:       return "Humo";
        case ClickBurst::Coins:       return "Monedas";
        case ClickBurst::Notes:       return "Notas";
        case ClickBurst::Petals:      return "Petalos";
        case ClickBurst::Bloom:       return "Flor";
        case ClickBurst::Galaxy:      return "Galaxia";
        case ClickBurst::Fireball:    return "Bola de fuego";
        default:                      return "Ninguno";
    }
}

char const* clickBurstDesc(ClickBurst effect) {
    switch (effect) {
        case ClickBurst::None:        return "Sin estallido.";
        case ClickBurst::Ripple:      return "Una onda circular que se abre y se apaga.";
        case ClickBurst::Shockwave:   return "Tres ondas seguidas con chispas: pega fuerte.";
        case ClickBurst::Hearts:      return "Explosion de corazones que salen y flotan.";
        case ClickBurst::Stars:       return "Estrellas que saltan, titilan y caen.";
        case ClickBurst::Confetti:    return "Papelitos que giran y caen con gravedad.";
        case ClickBurst::Sparks:      return "Chispas cortas y rapidas, tipo pedernal.";
        case ClickBurst::Firework:    return "Destello y chispas que titilan al caer.";
        case ClickBurst::Bubbles:     return "Burbujas que se van meciendo hacia arriba.";
        case ClickBurst::Snow:        return "Copos que bajan flotando despacio.";
        case ClickBurst::Ink:         return "Salpicadura de tinta que se abre y queda.";
        case ClickBurst::Lightning:   return "Rayos que salen en todas direcciones.";
        case ClickBurst::MagicCircle: return "Un circulo de invocacion que gira y se abre.";
        case ClickBurst::Pixels:      return "Cuadraditos retro que se dispersan y encogen.";
        case ClickBurst::Smoke:       return "Una bocanada de humo que crece.";
        case ClickBurst::Coins:       return "Monedas que saltan hacia arriba y caen.";
        case ClickBurst::Notes:       return "Notas musicales que suben meciendose.";
        case ClickBurst::Petals:      return "Petalos que se van con el viento.";
        case ClickBurst::Bloom:       return "Una flor que se abre de golpe y se cierra.";
        case ClickBurst::Galaxy:      return "Polvo estelar girando en espiral.";
        case ClickBurst::Fireball:    return "Una bola de fuego que revienta hacia arriba.";
        default:                      return "";
    }
}

char const* clickHoldName(ClickHold effect) {
    switch (effect) {
        case ClickHold::None:       return "Ninguno";
        case ClickHold::Aura:       return "Aura";
        case ClickHold::ChargeRing: return "Carga";
        case ClickHold::Orbit:      return "Orbita";
        case ClickHold::Vortex:     return "Vortice";
        case ClickHold::Drip:       return "Goteo";
        case ClickHold::Electric:   return "Electrico";
        case ClickHold::Flame:      return "Llama";
        case ClickHold::Sparkle:    return "Destellos";
        case ClickHold::Pulse:      return "Pulsos";
        default:                    return "Ninguno";
    }
}

char const* clickHoldDesc(ClickHold effect) {
    switch (effect) {
        case ClickHold::None:       return "Sin efecto mientras mantienes.";
        case ClickHold::Aura:       return "Un halo que late alrededor del cursor.";
        case ClickHold::ChargeRing: return "Un anillo que se cierra como si cargara energia.";
        case ClickHold::Orbit:      return "Satelites que giran alrededor del cursor.";
        case ClickHold::Vortex:     return "Particulas absorbidas hacia el cursor.";
        case ClickHold::Drip:       return "Gotas que caen del cursor.";
        case ClickHold::Electric:   return "Arcos electricos que chisporrotean.";
        case ClickHold::Flame:      return "Una llama constante que sube.";
        case ClickHold::Sparkle:    return "Destellos que aparecen y se apagan alrededor.";
        case ClickHold::Pulse:      return "Ondas concentricas que se repiten sin parar.";
        default:                    return "";
    }
}

char const* clickAnimName(ClickAnim anim) {
    switch (anim) {
        case ClickAnim::None:   return "Ninguna";
        case ClickAnim::Squash: return "Aplastar";
        case ClickAnim::Pop:    return "Agrandar";
        case ClickAnim::Sink:   return "Hundir";
        case ClickAnim::Tilt:   return "Inclinar";
        case ClickAnim::Spin:   return "Girar";
        case ClickAnim::Shake:  return "Vibrar";
        case ClickAnim::Bounce: return "Saltito";
        case ClickAnim::Wobble: return "Tambalear";
        default:                return "Ninguna";
    }
}

char const* clickAnimDesc(ClickAnim anim) {
    switch (anim) {
        case ClickAnim::None:   return "El cursor no reacciona al click.";
        case ClickAnim::Squash: return "Se achata a lo ancho mientras aprietas.";
        case ClickAnim::Pop:    return "Pega un tiron de tamano al apretar.";
        case ClickAnim::Sink:   return "Se hunde y encoge, como un boton fisico.";
        case ClickAnim::Tilt:   return "Se inclina hacia un lado.";
        case ClickAnim::Spin:   return "Da una vuelta completa.";
        case ClickAnim::Shake:  return "Vibra mientras el boton sigue abajo.";
        case ClickAnim::Bounce: return "Rebota hacia arriba una vez.";
        case ClickAnim::Wobble: return "Se tuerce de un lado al otro.";
        default:                return "";
    }
}

char const* clickSoundName(ClickSound sound) {
    switch (sound) {
        case ClickSound::None:        return "Ninguno";
        case ClickSound::Click:       return "Click";
        case ClickSound::Pop:         return "Pop";
        case ClickSound::Coin:        return "Moneda";
        case ClickSound::Crystal:     return "Cristal";
        case ClickSound::Achievement: return "Logro";
        case ClickSound::Explosion:   return "Explosion";
        case ClickSound::Magic:       return "Magia";
        case ClickSound::Key:         return "Llave";
        case ClickSound::Chest:       return "Cofre";
        case ClickSound::Score:       return "Puntaje";
        default:                      return "Ninguno";
    }
}


namespace {
const ClickPreset kClickPresets[] = {
    {"Onda simple", {ClickBurst::Ripple, ClickBurst::None, ClickHold::None,
        ClickAnim::Squash, ClickSound::None, ClickSound::None,
        TrailColorMode::Solid, {255,255,255}, {110,200,255},
        1.0f, 1.0f, 1.0f, 0.55f, 1.0f, 225, true, 1.0f, 0.14f, 0.55f, 1.0f, false, true}},

    {"Amor", {ClickBurst::Hearts, ClickBurst::None, ClickHold::Aura,
        ClickAnim::Pop, ClickSound::Click, ClickSound::None,
        TrailColorMode::Gradient, {255,120,170}, {255,40,90},
        1.0f, 1.0f, 1.2f, 0.95f, 1.0f, 235, false, 1.2f, 0.16f, 0.5f, 1.2f, true, true}},

    {"Fiesta", {ClickBurst::Confetti, ClickBurst::None, ClickHold::None,
        ClickAnim::Bounce, ClickSound::Pop, ClickSound::None,
        TrailColorMode::Random, {255,255,255}, {255,255,255},
        1.0f, 1.0f, 1.4f, 1.1f, 1.1f, 240, false, 1.0f, 0.18f, 0.5f, 1.0f, true, true}},

    {"Chispazo", {ClickBurst::Sparks, ClickBurst::None, ClickHold::Electric,
        ClickAnim::Shake, ClickSound::Click, ClickSound::None,
        TrailColorMode::Gradient, {255,250,190}, {255,130,20},
        1.0f, 0.9f, 1.2f, 0.6f, 1.2f, 240, true, 1.0f, 0.12f, 0.5f, 1.3f, true, true}},

    {"Tormenta", {ClickBurst::Lightning, ClickBurst::None, ClickHold::Electric,
        ClickAnim::Shake, ClickSound::Explosion, ClickSound::None,
        TrailColorMode::Gradient, {200,240,255}, {110,140,255},
        1.0f, 1.0f, 1.0f, 0.45f, 1.2f, 245, true, 1.3f, 0.10f, 0.45f, 1.0f, true, true}},

    {"Invocacion", {ClickBurst::MagicCircle, ClickBurst::Firework, ClickHold::ChargeRing,
        ClickAnim::Spin, ClickSound::Magic, ClickSound::Crystal,
        TrailColorMode::RainbowCycle, {255,255,255}, {255,255,255},
        1.6f, 1.1f, 1.0f, 1.1f, 0.9f, 235, true, 1.0f, 0.30f, 0.5f, 1.0f, false, true}},

    {"Estrellita", {ClickBurst::Stars, ClickBurst::None, ClickHold::Sparkle,
        ClickAnim::Pop, ClickSound::Crystal, ClickSound::None,
        TrailColorMode::Gradient, {255,255,215}, {255,205,80},
        1.0f, 1.0f, 1.0f, 0.9f, 1.0f, 240, true, 1.1f, 0.15f, 0.45f, 1.1f, true, true}},

    {"Monedas", {ClickBurst::Coins, ClickBurst::None, ClickHold::None,
        ClickAnim::Sink, ClickSound::Coin, ClickSound::None,
        TrailColorMode::Gradient, {255,230,130}, {225,150,30},
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 245, false, 1.0f, 0.14f, 0.5f, 1.0f, true, true}},

    {"Melodia", {ClickBurst::Notes, ClickBurst::None, ClickHold::None,
        ClickAnim::Wobble, ClickSound::Pop, ClickSound::None,
        TrailColorMode::RainbowCycle, {255,255,255}, {255,255,255},
        1.3f, 1.0f, 0.9f, 1.2f, 0.9f, 235, false, 1.0f, 0.20f, 0.45f, 1.0f, true, true}},

    {"Jardin", {ClickBurst::Bloom, ClickBurst::Petals, ClickHold::None,
        ClickAnim::Pop, ClickSound::Click, ClickSound::None,
        TrailColorMode::Gradient, {255,170,210}, {255,90,140},
        1.0f, 1.1f, 1.0f, 1.0f, 1.0f, 235, false, 1.0f, 0.18f, 0.4f, 1.2f, false, true}},

    {"Volcan", {ClickBurst::Fireball, ClickBurst::Smoke, ClickHold::Flame,
        ClickAnim::Squash, ClickSound::Explosion, ClickSound::None,
        TrailColorMode::Gradient, {255,225,120}, {255,60,0},
        1.2f, 1.0f, 1.1f, 0.9f, 1.0f, 230, true, 1.1f, 0.16f, 0.5f, 0.9f, true, true}},

    {"Retro", {ClickBurst::Pixels, ClickBurst::None, ClickHold::None,
        ClickAnim::Sink, ClickSound::Pop, ClickSound::Click,
        TrailColorMode::Gradient, {160,255,160}, {10,140,30},
        0.8f, 1.2f, 0.7f, 1.0f, 1.0f, 245, false, 0.9f, 0.10f, 0.45f, 1.4f, false, true}},

    {"Cosmos", {ClickBurst::Galaxy, ClickBurst::None, ClickHold::Orbit,
        ClickAnim::Tilt, ClickSound::Crystal, ClickSound::None,
        TrailColorMode::Gradient, {215,170,255}, {90,140,255},
        1.0f, 1.2f, 1.3f, 0.8f, 1.0f, 235, true, 1.0f, 0.20f, 0.4f, 1.0f, true, true}},

    {"Sereno", {ClickBurst::Bubbles, ClickBurst::None, ClickHold::Pulse,
        ClickAnim::None, ClickSound::None, ClickSound::None,
        TrailColorMode::Gradient, {205,245,255}, {120,190,255},
        1.1f, 0.8f, 1.4f, 0.7f, 1.0f, 205, false, 1.0f, 0.18f, 0.4f, 1.0f, false, true}},

    {"Impacto", {ClickBurst::Shockwave, ClickBurst::None, ClickHold::None,
        ClickAnim::Shake, ClickSound::Explosion, ClickSound::None,
        TrailColorMode::Solid, {255,255,255}, {255,120,60},
        1.2f, 1.0f, 0.6f, 1.2f, 1.0f, 240, true, 1.4f, 0.12f, 0.5f, 1.0f, false, true}},

    {"Tinta", {ClickBurst::Ink, ClickBurst::None, ClickHold::Drip,
        ClickAnim::Sink, ClickSound::Click, ClickSound::None,
        TrailColorMode::Solid, {25,25,35}, {255,255,255},
        1.1f, 1.0f, 1.5f, 0.8f, 1.0f, 220, false, 0.9f, 0.16f, 0.4f, 0.8f, false, true}},

    {"Nevada", {ClickBurst::Snow, ClickBurst::None, ClickHold::Vortex,
        ClickAnim::None, ClickSound::Crystal, ClickSound::None,
        TrailColorMode::Gradient, {255,255,255}, {150,225,255},
        1.0f, 1.0f, 1.7f, 0.7f, 1.0f, 220, false, 1.0f, 0.18f, 0.4f, 1.3f, false, true}},

    {"Arcoiris", {ClickBurst::Firework, ClickBurst::Ripple, ClickHold::Aura,
        ClickAnim::Pop, ClickSound::Achievement, ClickSound::None,
        TrailColorMode::RainbowCycle, {255,255,255}, {255,255,255},
        1.8f, 1.0f, 1.3f, 1.0f, 1.1f, 240, true, 1.2f, 0.18f, 0.5f, 1.0f, true, true}},
};
constexpr int kClickPresetTotal =
    static_cast<int>(sizeof(kClickPresets) / sizeof(kClickPresets[0]));
}

int clickPresetCount() { return kClickPresetTotal; }

ClickPreset const& clickPresetAt(int index) {
    if (index < 0 || index >= kClickPresetTotal) index = 0;
    return kClickPresets[index];
}

int findClickPreset(ClickSettings const& s) {
    auto sameColor = [](ccColor3B a, ccColor3B b) {
        return a.r == b.r && a.g == b.g && a.b == b.b;
    };
    auto sameF = [](float a, float b) { return std::fabs(a - b) < 0.01f; };
    for (int i = 0; i < kClickPresetTotal; ++i) {
        auto const& p = kClickPresets[i].settings;
        if (p.press == s.press && p.release == s.release && p.hold == s.hold &&
            p.anim == s.anim && p.pressSound == s.pressSound &&
            p.releaseSound == s.releaseSound && p.colorMode == s.colorMode &&
            sameColor(p.color1, s.color1) && sameColor(p.color2, s.color2) &&
            sameF(p.hueSpeed, s.hueSpeed) && sameF(p.size, s.size) &&
            sameF(p.amount, s.amount) && sameF(p.life, s.life) &&
            sameF(p.spread, s.spread) && p.opacity == s.opacity && p.glow == s.glow &&
            sameF(p.animStrength, s.animStrength) &&
            sameF(p.animDuration, s.animDuration) && sameF(p.volume, s.volume) &&
            sameF(p.pitch, s.pitch) && p.randomPitch == s.randomPitch &&
            p.rightClick == s.rightClick) {
            return i;
        }
    }
    return -1;
}


void playClickSound(ClickSound sound, float volume, float pitch, bool randomPitch) {
    char const* file = soundFileFor(sound);
    if (!file) return;
    auto* engine = FMODAudioEngine::sharedEngine();
    if (!engine) return;

    float speed = std::clamp(pitch, kClickPitchMin, kClickPitchMax);
    if (randomPitch) speed = std::clamp(speed * frand(0.86f, 1.16f), 0.4f, 2.5f);
    engine->playEffect(file, speed, 1.f, std::clamp(volume, 0.f, 1.f));
}


TransitionFrame sampleClickAnim(ClickAnim anim, float t, float duration,
                                float strength, bool held) {
    TransitionFrame frame{};
    if (anim == ClickAnim::None) return frame;

    duration = std::clamp(duration, kClickAnimDurMin, kClickAnimDurMax);
    strength = std::clamp(strength, kClickAnimMin, kClickAnimMax);
    float raw = std::clamp(t / duration, 0.f, 1.f);

// `stay` holds while pressed; `bump` completes one round trip.
    float stay = held ? smoothStep(raw) : 1.f - smoothStep(raw);
    float bump = held ? std::sin(raw * kPi) : 0.f;
    float s = strength;

    switch (anim) {
        case ClickAnim::Squash:
            frame.scaleX = 1.f + 0.24f * s * stay;
            frame.scaleY = 1.f - 0.20f * s * stay;
            break;
        case ClickAnim::Pop: {
            float k = held ? std::max(bump, stay * 0.45f) : stay * 0.45f;
            frame.scaleX = frame.scaleY = 1.f + 0.30f * s * k;
            break;
        }
        case ClickAnim::Sink:
            frame.scaleX = frame.scaleY = 1.f - 0.18f * s * stay;
            frame.offset = ccp(1.5f * s * stay, -2.5f * s * stay);
            break;
        case ClickAnim::Tilt:
            frame.rotation = 14.f * s * stay;
            break;
        case ClickAnim::Spin:
// One cycle on press and release always returns to neutral.
            frame.rotation = 360.f * easeOut(raw);
            frame.scaleX = frame.scaleY = 1.f - 0.08f * s * std::sin(raw * kPi);
            break;
        case ClickAnim::Shake: {
            float amp = 2.6f * s * (held ? std::max(stay, 0.35f) : stay);
            frame.offset = ccp(std::sin(t * 46.f) * amp, std::cos(t * 38.f) * amp * 0.6f);
            break;
        }
        case ClickAnim::Bounce: {
            float k = held ? bump : 0.f;
            frame.offset = ccp(0.f, 7.f * s * k);
            frame.scaleY = 1.f + 0.12f * s * k;
            frame.scaleX = 1.f - 0.08f * s * k;
            break;
        }
        case ClickAnim::Wobble: {
            float amp = 10.f * s * (held ? std::max(stay, 0.3f) : stay);
            frame.skewX = std::sin(t * 22.f) * amp;
            frame.rotation = std::sin(t * 15.f) * amp * 0.35f;
            break;
        }
        default: break;
    }
    return frame;
}

CursorClickNode* CursorClickNode::create() {
    auto* node = new CursorClickNode();
    if (node->init()) {
        node->autorelease();
        return node;
    }
    delete node;
    return nullptr;
}

bool CursorClickNode::init() {
    if (!CCNode::init()) return false;

    m_draw = FxDrawBatch::create();
    if (m_draw) this->addChild(m_draw, 0);

    m_particles.resize(kMaxClickParticles);
    m_rings.resize(kMaxRings);
    m_bolts.resize(kMaxBolts);
    return true;
}

ccBlendFunc CursorClickNode::spriteBlend() const {
    return m_cfg.glow ? ccBlendFunc{GL_SRC_ALPHA, GL_ONE}
                      : ccBlendFunc{GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA};
}

void CursorClickNode::refreshBlend() {
    auto blend = spriteBlend();
    for (auto* batch : m_batches) {
        if (batch) batch->setBlendFunc(blend);
    }
    if (m_draw) m_draw->setAdditive(m_cfg.glow);
}

CCSpriteBatchNode* CursorClickNode::ensureBatch(int texKind) {
    if (texKind < 0 || texKind >= TexCount) return nullptr;
    auto& slot = m_batches[static_cast<size_t>(texKind)];
    if (slot) return slot;

    auto* tex = fxTexture(texKind);
    if (!tex) return nullptr;
    auto* batch = CCSpriteBatchNode::createWithTexture(tex, 64);
    if (!batch) return nullptr;
    batch->setBlendFunc(spriteBlend());
    this->addChild(batch, 1);
    slot = batch;
    return batch;
}

void CursorClickNode::applySettings(ClickSettings const& s) {
    bool glowChanged = s.glow != m_cfg.glow;
    m_cfg = s;
    m_cfg.size    = std::clamp(m_cfg.size, kClickSizeMin, kClickSizeMax);
    m_cfg.amount  = std::clamp(m_cfg.amount, kClickAmountMin, kClickAmountMax);
    m_cfg.life    = std::clamp(m_cfg.life, kClickLifeMin, kClickLifeMax);
    m_cfg.spread  = std::clamp(m_cfg.spread, kClickSpreadMin, kClickSpreadMax);
    m_cfg.opacity = std::clamp(m_cfg.opacity, 0, 255);
    m_cfg.hueSpeed = std::clamp(m_cfg.hueSpeed, kHueSpeedMin, kHueSpeedMax);
    if (glowChanged) refreshBlend();
}

void CursorClickNode::beginOverlayPass() {
    m_overlayPassSeen = true;
    m_inOverlayPass = true;
}

void CursorClickNode::visit() {
    if (m_overlayPassSeen && !m_inOverlayPass) return;
    CCNode::visit();
}

void CursorClickNode::reset() {
    for (auto& p : m_particles) {
        p.alive = false;
        if (p.spr) p.spr->setVisible(false);
    }
    for (auto& r : m_rings) r.alive = false;
    for (auto& b : m_bolts) b.alive = false;
    m_held = false;
    m_holdTime = 0.f;
    m_holdCredit = 0.f;
    m_pulseTimer = 0.f;
    m_chargeTime = 0.f;
    if (m_draw) m_draw->clear();
}

void CursorClickNode::press(CCPoint const& pos) {
    m_pos = pos;
    m_held = true;
    m_holdTime = 0.f;
    m_holdCredit = 0.f;
    m_pulseTimer = 0.f;
    m_chargeTime = 0.f;
    spawnBurst(m_cfg.press, pos);
}

void CursorClickNode::release(CCPoint const& pos) {
    m_pos = pos;
    m_held = false;
    m_holdTime = 0.f;
    spawnBurst(m_cfg.release, pos);
}

ccColor3B CursorClickNode::resolveColor(float t, float rnd) const {
    switch (m_cfg.colorMode) {
        case TrailColorMode::Solid:
            return m_cfg.color1;
        case TrailColorMode::Gradient:
            return mixColor(m_cfg.color1, m_cfg.color2, t);
        case TrailColorMode::RainbowCycle:
            return hsv(m_time * m_cfg.hueSpeed * 0.18f, 0.85f, 1.f);
        case TrailColorMode::RainbowTrail:
            return hsv(m_time * m_cfg.hueSpeed * 0.18f + t * 0.85f, 0.85f, 1.f);
        case TrailColorMode::Random:
            return hsv(rnd, 0.80f, 1.f);
        case TrailColorMode::Speed:
// Click effects use hold duration rather than cursor speed.
            return mixColor(m_cfg.color1, m_cfg.color2,
                            std::clamp(m_holdTime / 1.2f, 0.f, 1.f));
        default:
            return m_cfg.color1;
    }
}

float CursorClickNode::holdTuneSize() const {
    return std::clamp(m_cfg.holdTuning[static_cast<size_t>(m_cfg.hold)].size,
                      kClickTuneMin, kClickTuneMax);
}

float CursorClickNode::holdTuneSpeed() const {
    return std::clamp(m_cfg.holdTuning[static_cast<size_t>(m_cfg.hold)].speed,
                      kClickTuneMin, kClickTuneMax);
}

CursorClickNode::Particle* CursorClickNode::acquire(int texKind) {
    Particle* reuse = nullptr;
    for (auto& p : m_particles) {
        if (p.alive) continue;
// Prefer a batch that already owns this texture; switching batches recreates it.
        if (p.texKind == texKind && p.spr) return &p;
        if (!reuse) reuse = &p;
    }
    if (!reuse) {
        float worst = -1.f;
        for (auto& p : m_particles) {
            float t = p.life / std::max(0.01f, p.maxLife);
            if (t > worst) { worst = t; reuse = &p; }
        }
    }
    if (!reuse) return nullptr;

    if (reuse->texKind != texKind || !reuse->spr) {
        if (reuse->spr) {
            reuse->spr->removeFromParent();
            reuse->spr = nullptr;
        }
        reuse->texKind = -1;
        auto* batch = ensureBatch(texKind);
        auto* tex = fxTexture(texKind);
        if (!batch || !tex) return nullptr;
// Create outside the batch; setTexture cannot recalculate its blend function there.
        auto* spr = CCSprite::createWithTexture(tex);
        if (!spr) return nullptr;
        spr->setAnchorPoint({0.5f, 0.5f});
        batch->addChild(spr);
        reuse->spr = spr;
        reuse->texKind = texKind;
    }
    return reuse;
}

CursorClickNode::Ring* CursorClickNode::acquireRing() {
    Ring* oldest = nullptr;
    float worst = -1.f;
    for (auto& r : m_rings) {
        if (!r.alive) return &r;
        float t = r.age / std::max(0.01f, r.maxAge);
        if (t > worst) { worst = t; oldest = &r; }
    }
    return oldest;
}

CursorClickNode::Bolt* CursorClickNode::acquireBolt() {
    Bolt* oldest = nullptr;
    float worst = -1.f;
    for (auto& b : m_bolts) {
        if (!b.alive) return &b;
        float t = b.age / std::max(0.01f, b.maxAge);
        if (t > worst) { worst = t; oldest = &b; }
    }
    return oldest;
}

void CursorClickNode::spawnBurst(ClickBurst effect, CCPoint const& pos) {
    if (effect == ClickBurst::None) return;
    auto const& spec = burstSpecFor(effect);
    auto const& tune = m_cfg.burstTuning[static_cast<size_t>(effect)];

    float tuneSize  = std::clamp(tune.size, kClickTuneMin, kClickTuneMax);
    float tuneSpeed = std::clamp(tune.speed, kClickTuneMin, kClickTuneMax);
    float sizeScale = m_cfg.size * tuneSize * 2.4f;
    float speedScale = m_cfg.spread * tuneSpeed;

    float ringScale = m_cfg.size * tuneSize;

    for (int i = 0; i < spec.rings; ++i) {
        auto* r = acquireRing();
        if (!r) break;
        float delay = static_cast<float>(i) * 0.06f;
        r->pos = pos;
    r->age = -delay;
        r->maxAge = std::max(0.12f, m_cfg.life * (spec.ringKind == 2 ? 1.2f : 0.75f));
        r->startR = 3.f * ringScale;
        r->endR = (spec.ringKind == 2 ? 46.f : 58.f) * ringScale * speedScale;
        r->thickness = (spec.ringKind == 1 ? 0.f : 3.4f * ringScale);
        r->scale = ringScale;
        r->kind = spec.ringKind;
        r->spin = spec.ringKind == 2 ? frand(70.f, 130.f) * (frand() < 0.5f ? -1.f : 1.f) : 0.f;
        r->color = resolveColor(0.f, frand());
        r->alive = true;
    }

    if (spec.bolts > 0) {
        int count = std::max(2, static_cast<int>(spec.bolts * m_cfg.amount));
        count = std::min(count, kMaxBolts);
        float base = frand(0.f, 2.f * kPi);
        for (int i = 0; i < count; ++i) {
            auto* b = acquireBolt();
            if (!b) break;
            b->pos = pos;
            b->age = 0.f;
            b->maxAge = std::max(0.08f, m_cfg.life * 0.35f);
            b->angle = base + 2.f * kPi * static_cast<float>(i) / static_cast<float>(count)
                     + frand(-0.25f, 0.25f);
            b->length = frand(34.f, 70.f) * ringScale * speedScale;
            b->scale = ringScale;
            b->seed = static_cast<unsigned int>(frand(1.f, 4.0e6f));
            b->color = resolveColor(frand(), frand());
            b->alive = true;
        }
    }

    int count = static_cast<int>(spec.count * m_cfg.amount + 0.5f);
    count = std::clamp(count, 0, 90);
    for (int i = 0; i < count; ++i) {
        Particle* p = acquire(spec.tex);
        if (!p) break;

        float angle;
        switch (spec.dirMode) {
            case 1:  angle = kPi * 0.5f + frand(-0.55f, 0.55f); break;
            case 2:  angle = kPi * 0.5f + frand(-1.15f, 1.15f); break;
            default: angle = 2.f * kPi * static_cast<float>(i) / static_cast<float>(std::max(1, count))
                             + frand(-0.30f, 0.30f); break;
        }
        float speed = frand(spec.speedMin, spec.speedMax) * speedScale;

        p->pos = ccp(pos.x + std::cos(angle) * 2.f, pos.y + std::sin(angle) * 2.f);
        p->vel = ccp(std::cos(angle) * speed, std::sin(angle) * speed);
        p->life = 0.f;
        p->maxLife = std::max(0.08f, m_cfg.life * spec.lifeMul * frand(0.82f, 1.18f));
        p->baseSize = sizeScale * frand(spec.sizeMin, spec.sizeMax);
        p->growth = spec.growth;
        p->gravity = spec.gravity;
        p->drag = spec.drag;
        p->spin = frand(-spec.spin, spec.spin);
        p->rot = spec.alignVel ? -angle * 180.f / kPi + 90.f : frand(0.f, 360.f);
        p->alignVel = spec.alignVel;
        p->homing = false;
        p->swayAmp = spec.sway * frand(0.5f, 1.f) * (frand() < 0.5f ? -1.f : 1.f);
        p->swayPhase = frand(0.f, 2.f * kPi);
        p->twinkle = spec.twinkle;
        p->fadeIn = spec.fadeIn;
        p->spiral = spec.spiral * frand(0.6f, 1.4f) * (frand() < 0.5f ? -1.f : 1.f);
        p->spiralRadius = spec.spiral > 0.f ? m_cfg.size * frand(2.f, 9.f) : 0.f;
        p->spiralAngle = angle;
        p->rnd = frand();
        p->color = resolveColor(p->rnd, p->rnd);
        p->alive = p->spr != nullptr;
        if (p->spr) p->spr->setVisible(true);
    }
}

void CursorClickNode::stepHold(float dt) {
    if (!m_held || m_cfg.hold == ClickHold::None) return;
    auto const& spec = holdSpecFor(m_cfg.hold);
    float holdSize  = m_cfg.size * holdTuneSize();
    float holdSpeed = m_cfg.spread * holdTuneSpeed();

    if (spec.mode == 2) {
        m_pulseTimer -= dt;
        if (m_pulseTimer <= 0.f) {
            m_pulseTimer = std::max(0.12f, 0.45f / std::max(0.2f, m_cfg.amount));
            if (auto* r = acquireRing()) {
                r->pos = m_pos;
                r->age = 0.f;
                r->maxAge = std::max(0.2f, m_cfg.life * 0.9f);
                r->startR = 4.f * holdSize;
                r->endR = 44.f * holdSize * holdSpeed;
                r->thickness = 2.6f * holdSize;
                r->scale = holdSize;
                r->kind = 0;
                r->spin = 0.f;
                r->color = resolveColor(0.f, frand());
                r->alive = true;
            }
        }
        return;
    }

    if (spec.mode == 1) {
// Geometry-only effects draw from m_holdTime; electric refreshes its seed.
        if (m_cfg.hold == ClickHold::Electric) {
            m_pulseTimer -= dt;
            if (m_pulseTimer <= 0.f) {
                m_pulseTimer = 1.f / 22.f;
                m_boltSeed = m_boltSeed * 1664525u + 1013904223u;
            }
        }
        return;
    }

    float rate = spec.rate * m_cfg.amount;
    if (rate <= 0.f) return;
    m_holdCredit += rate * dt;
    int count = static_cast<int>(m_holdCredit);
    if (count <= 0) return;
    m_holdCredit -= static_cast<float>(count);
    count = std::min(count, 8);

    bool vortex = m_cfg.hold == ClickHold::Vortex;
    for (int i = 0; i < count; ++i) {
        Particle* p = acquire(spec.tex);
        if (!p) return;

        float ring = spec.radius * holdSize * holdSpeed;
        float birthAngle = frand(0.f, 2.f * kPi);
        p->pos = ccp(m_pos.x + std::cos(birthAngle) * ring,
                     m_pos.y + std::sin(birthAngle) * ring);

        float angle;
        switch (spec.dirMode) {
            case 1:  angle = kPi * 0.5f + frand(-0.5f, 0.5f); break;
            case 3:  angle = -kPi * 0.5f + frand(-0.35f, 0.35f); break;
            default: angle = frand(0.f, 2.f * kPi); break;
        }
        float speed = frand(spec.speedMin, spec.speedMax) * holdSpeed;
        if (vortex) {
            angle = birthAngle + kPi;
        }
        p->vel = ccp(std::cos(angle) * speed, std::sin(angle) * speed);

        p->life = 0.f;
        p->maxLife = std::max(0.08f, m_cfg.life * spec.lifeMul * frand(0.8f, 1.2f));
        p->baseSize = holdSize * 2.4f * frand(spec.sizeMin, spec.sizeMax);
        p->growth = spec.growth;
        p->gravity = spec.gravity;
        p->drag = spec.drag;
        p->spin = frand(-spec.spin, spec.spin);
        p->rot = frand(0.f, 360.f);
        p->alignVel = false;
        p->homing = vortex;
        p->swayAmp = spec.sway * frand(0.5f, 1.f) * (frand() < 0.5f ? -1.f : 1.f);
        p->swayPhase = frand(0.f, 2.f * kPi);
        p->twinkle = spec.twinkle;
        p->fadeIn = 0.08f;
        p->spiral = 0.f;
        p->spiralRadius = 0.f;
        p->spiralAngle = 0.f;
        p->rnd = frand();
        p->color = resolveColor(p->rnd, p->rnd);
        p->alive = p->spr != nullptr;
        if (p->spr) p->spr->setVisible(true);
    }
}

void CursorClickNode::step(float dt, CCPoint const& pos, bool held) {
    dt = std::clamp(dt, 0.f, 0.05f);
    m_time += dt;
    if (m_time > 3600.f) m_time -= 3600.f;

    m_pos = pos;
    m_held = held;
    if (held) {
        m_holdTime += dt;
        m_chargeTime += dt;
    } else {
        m_holdTime = 0.f;
        m_chargeTime = 0.f;
    }

    stepHold(dt);
    updateParticles(dt);
    updateRings(dt);
    updateBolts(dt);
    redraw();
}

void CursorClickNode::updateParticles(float dt) {
    float baseAlpha = m_cfg.opacity / 255.f;
    for (auto& p : m_particles) {
        if (!p.alive) continue;
        p.life += dt;
        if (p.life >= p.maxLife) {
            p.alive = false;
            if (p.spr) p.spr->setVisible(false);
            continue;
        }
        float t = p.life / p.maxLife;

        if (p.homing) {
            float dx = m_pos.x - p.pos.x, dy = m_pos.y - p.pos.y;
            float d = std::sqrt(dx * dx + dy * dy);
            if (d < 3.f) {
                p.alive = false;
                if (p.spr) p.spr->setVisible(false);
                continue;
            }
            float pull = 260.f * dt / std::max(6.f, d);
            p.vel.x += dx * pull;
            p.vel.y += dy * pull;
        }

        p.vel.y += p.gravity * dt;
        float damp = std::exp(-p.drag * dt);
        p.vel.x *= damp;
        p.vel.y *= damp;
        p.pos.x += p.vel.x * dt;
        p.pos.y += p.vel.y * dt;
        if (p.swayAmp != 0.f) {
            p.pos.x += std::sin(m_time * 3.4f + p.swayPhase) * p.swayAmp * dt;
        }

        CCPoint draw = p.pos;
        if (p.spiral != 0.f) {
            p.spiralAngle += p.spiral * kPi / 180.f * dt;
            float r = p.spiralRadius * (0.35f + t);
            draw.x += std::cos(p.spiralAngle) * r;
            draw.y += std::sin(p.spiralAngle) * r;
        }

        float fade = p.fadeIn > 0.001f ? std::min(1.f, t / p.fadeIn) : 1.f;
        float out = std::pow(std::max(0.f, 1.f - t), 1.25f);
        float alpha = baseAlpha * fade * out;
        if (p.twinkle > 0.f) {
            alpha *= 0.55f + 0.45f * std::sin(m_time * p.twinkle * 6.28f + p.swayPhase);
        }

        float scale = p.baseSize * (1.f + (p.growth - 1.f) * t) / 64.f;
        if (p.alignVel) {
            p.rot = -std::atan2(p.vel.y, p.vel.x) * 180.f / kPi + 90.f;
        } else {
            p.rot += p.spin * dt;
        }

        if (p.spr) {
            p.spr->setPosition(draw);
            p.spr->setScale(std::max(0.f, scale));
            p.spr->setRotation(p.rot);
            p.spr->setColor(p.color);
            p.spr->setOpacity(static_cast<GLubyte>(std::clamp(alpha, 0.f, 1.f) * 255.f));
        }
    }
}

void CursorClickNode::updateRings(float dt) {
    for (auto& r : m_rings) {
        if (!r.alive) continue;
        r.age += dt;
        if (r.age >= r.maxAge) r.alive = false;
    }
}

void CursorClickNode::updateBolts(float dt) {
    for (auto& b : m_bolts) {
        if (!b.alive) continue;
        b.age += dt;
        if (b.age >= b.maxAge) b.alive = false;
    }
}

void CursorClickNode::redraw() {
    if (!m_draw) return;
    m_draw->clear();
    m_draw->setAdditive(m_cfg.glow);

    float baseAlpha = m_cfg.opacity / 255.f;

    for (auto const& r : m_rings) {
        if (!r.alive || r.age < 0.f) continue;
        float t = std::clamp(r.age / r.maxAge, 0.f, 1.f);
        float alpha = baseAlpha * std::pow(1.f - t, 1.6f);
        if (alpha <= 0.004f) continue;
        float radius = r.startR + (r.endR - r.startR) * easeOut(t);

        switch (r.kind) {
            case 1: {
                float flash = baseAlpha * std::pow(std::max(0.f, 1.f - t * 2.2f), 2.f);
                if (flash > 0.004f) {
                    m_draw->circle(r.pos, radius * 0.55f, pma(r.color, flash * 0.75f), 22);
                    m_draw->circle(r.pos, radius * 0.28f, pma({255, 255, 255}, flash), 18);
                }
                break;
            }
            case 2: {
                float spin = r.spin * r.age;
                m_draw->ring(r.pos, radius, 2.2f * r.scale, pma(r.color, alpha), 44);
                m_draw->ring(r.pos, radius * 0.72f, 1.4f * r.scale,
                             pma(r.color, alpha * 0.7f), 38);
                constexpr int kSides = 5;
                float inner = radius * 0.72f;
                for (int i = 0; i < kSides; ++i) {
                    float a0 = spin * kPi / 180.f + 2.f * kPi * i / kSides;
                    float a1 = spin * kPi / 180.f + 2.f * kPi * ((i + 2) % kSides) / kSides;
                    CCPoint p0 = ccp(r.pos.x + std::cos(a0) * inner, r.pos.y + std::sin(a0) * inner);
                    CCPoint p1 = ccp(r.pos.x + std::cos(a1) * inner, r.pos.y + std::sin(a1) * inner);
                    m_draw->thickLine(p0, p1, 1.6f * r.scale, pma(r.color, alpha * 0.85f));
                    m_draw->circle(p0, 2.2f * r.scale, pma({255, 255, 255}, alpha * 0.9f), 10);
                }
                break;
            }
            default: {
                float width = r.thickness * (1.f - t * 0.55f);
                m_draw->ring(r.pos, radius, std::max(0.6f, width), pma(r.color, alpha), 46);
                m_draw->ring(r.pos, radius * 0.82f, std::max(0.4f, width * 0.5f),
                             pma(r.color, alpha * 0.4f), 40);
                break;
            }
        }
    }

    for (auto const& b : m_bolts) {
        if (!b.alive) continue;
        float t = std::clamp(b.age / b.maxAge, 0.f, 1.f);
        float alpha = baseAlpha * std::pow(1.f - t, 1.4f);
        if (alpha <= 0.004f) continue;

        constexpr int kSegs = 6;
        CCPoint prev = b.pos;
        float nx = -std::sin(b.angle), ny = std::cos(b.angle);
        for (int i = 1; i <= kSegs; ++i) {
            float f = static_cast<float>(i) / kSegs;
            float jitter = hashNoise(b.seed, i) * 9.f * b.scale * (1.f - f * 0.5f);
            CCPoint next = ccp(
                b.pos.x + std::cos(b.angle) * b.length * f + nx * jitter,
                b.pos.y + std::sin(b.angle) * b.length * f + ny * jitter);
            m_draw->thickLine(prev, next, 3.2f * b.scale, pma(b.color, alpha * 0.55f));
            m_draw->thickLine(prev, next, 1.3f * b.scale, pma({255, 255, 255}, alpha));
            prev = next;
        }
    }

    if (m_held) {
        float alpha = baseAlpha * std::min(1.f, m_holdTime * 6.f);
        float holdSize = m_cfg.size * holdTuneSize();
        float holdSpeed = m_cfg.spread * holdTuneSpeed();
        switch (m_cfg.hold) {
            case ClickHold::Aura: {
                float pulse = 0.5f + 0.5f * std::sin(m_holdTime * 5.2f);
                float radius = (16.f + 5.f * pulse) * holdSize * holdSpeed;
                auto color = resolveColor(pulse, 0.3f);
                m_draw->circle(m_pos, radius, pma(color, alpha * 0.14f), 30);
                m_draw->ring(m_pos, radius, 3.0f * holdSize,
                             pma(color, alpha * (0.35f + 0.35f * pulse)), 40);
                m_draw->ring(m_pos, radius * 0.6f, 1.6f * holdSize,
                             pma(color, alpha * 0.3f), 30);
                break;
            }
            case ClickHold::ChargeRing: {
                constexpr float kCycle = 0.8f;
                float f = std::fmod(m_chargeTime, kCycle) / kCycle;
                float radius = (34.f - 26.f * easeOut(f)) * holdSize * holdSpeed;
                auto color = resolveColor(f, 0.5f);
                m_draw->ring(m_pos, radius, (1.6f + 2.6f * f) * holdSize,
                             pma(color, alpha * (0.45f + 0.5f * f)), 44);
                if (f > 0.9f) {
                    float flash = (f - 0.9f) / 0.1f;
                    m_draw->circle(m_pos, 7.f * holdSize * flash,
                                   pma({255, 255, 255}, alpha * flash), 16);
                }
                break;
            }
            case ClickHold::Orbit: {
                int count = std::clamp(static_cast<int>(3.f * m_cfg.amount + 0.5f), 2, 8);
                float radius = 20.f * holdSize * holdSpeed;
                for (int i = 0; i < count; ++i) {
                    float a = m_holdTime * 3.1f + 2.f * kPi * i / count;
                    CCPoint at = ccp(m_pos.x + std::cos(a) * radius,
                                     m_pos.y + std::sin(a) * radius * 0.72f);
                    auto color = resolveColor(static_cast<float>(i) / count,
                                              static_cast<float>(i) * 0.37f);
                    m_draw->circle(at, 4.2f * holdSize, pma(color, alpha * 0.9f), 14);
                    m_draw->circle(at, 8.0f * holdSize, pma(color, alpha * 0.18f), 14);
                }
                break;
            }
            case ClickHold::Electric: {
                int count = std::clamp(static_cast<int>(4.f * m_cfg.amount + 0.5f), 2, 10);
                float len = 22.f * holdSize * holdSpeed;
                for (int i = 0; i < count; ++i) {
                    float a = 2.f * kPi * i / count + hashNoise(m_boltSeed, i) * 0.7f;
                    CCPoint prev = m_pos;
                    float nx = -std::sin(a), ny = std::cos(a);
                    auto color = resolveColor(static_cast<float>(i) / count, 0.6f);
                    for (int s = 1; s <= 4; ++s) {
                        float f = static_cast<float>(s) / 4.f;
                        float jitter = hashNoise(m_boltSeed, i * 8 + s) * 6.f * holdSize;
                        CCPoint next = ccp(m_pos.x + std::cos(a) * len * f + nx * jitter,
                                           m_pos.y + std::sin(a) * len * f + ny * jitter);
                        m_draw->thickLine(prev, next, 2.4f * holdSize, pma(color, alpha * 0.5f));
                        m_draw->thickLine(prev, next, 1.1f * holdSize,
                                          pma({255, 255, 255}, alpha * 0.9f));
                        prev = next;
                    }
                }
                break;
            }
            default: break;
        }
    }
}

}
