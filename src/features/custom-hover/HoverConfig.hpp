#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace paimon::hover {
struct Config {
    bool enabled = true;
    float scale = 1.10f, stretch = 1.f, lift = 0.f, slide = 0.f, rotation = 0.f;
    float enter = .18f, exit = .22f, delay = 0.f, amplitude = .04f, frequency = 1.5f;
    int easing = 1, loop = 0;
};
inline float bounded(float v, float lo, float hi, float fallback) {
    return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
}
inline Config sanitize(Config c) {
    c.scale = bounded(c.scale, .5f, 1.8f, 1.1f);
    c.stretch = bounded(c.stretch, .6f, 1.5f, 1.f);
    c.lift = bounded(c.lift, -25.f, 25.f, 0.f);
    c.slide = bounded(c.slide, -25.f, 25.f, 0.f);
    c.rotation = bounded(c.rotation, -180.f, 180.f, 0.f);
    c.enter = bounded(c.enter, .05f, 1.5f, .18f);
    c.exit = bounded(c.exit, .05f, 1.5f, .22f);
    c.delay = bounded(c.delay, 0.f, 1.f, 0.f);
    c.amplitude = bounded(c.amplitude, 0.f, .25f, .04f);
    c.frequency = bounded(c.frequency, .2f, 5.f, 1.5f);
    c.easing = std::clamp(c.easing, 0, 4); c.loop = std::clamp(c.loop, 0, 4);
    return c;
}
inline float ease(float t, int mode) {
    t = std::clamp(t, 0.f, 1.f);
    switch (mode) {
        case 1: return t*t*(3.f-2.f*t);
        case 2: return 1.f-std::pow(1.f-t, 3.f);
        case 3: { float x=t-1.f; return 1.f+2.70158f*x*x*x+1.70158f*x*x; }
        case 4: return t == 0.f || t == 1.f ? t : 1.f-std::pow(2.f,-10.f*t)*std::cos(t*15.707963f);
        default: return t;
    }
}
struct Pose { float x=0, y=0, sx=1, sy=1, rotation=0; };
inline Pose pose(Config const& c, float amount, float seconds) {
    float wave = std::sin(seconds * c.frequency * 6.2831853f) * c.amplitude * amount;
    Pose p;
    p.sx = 1.f+(c.scale*c.stretch-1.f)*amount;
    p.sy = 1.f+(c.scale/c.stretch-1.f)*amount;
    p.x=c.slide*amount; p.y=c.lift*amount; p.rotation=c.rotation*amount;
    if (c.loop==1) { p.sx+=wave; p.sy+=wave; }
    if (c.loop==2) p.y+=wave*80.f;
    if (c.loop==3) p.rotation+=wave*120.f;
    if (c.loop==4) { p.sx+=wave; p.sy-=wave; }
    return p;
}
struct Preset { std::string name; Config config; };
inline std::vector<Preset> const& presets() {
    static auto list = [] {
        std::vector<Preset> v;
        auto add = [&](char const* name, float scale, float lift, float rotation, int easing, int loop, float stretch=1.f, float slide=0.f) {
            Config c; c.scale=scale; c.lift=lift; c.rotation=rotation; c.easing=easing; c.loop=loop; c.stretch=stretch; c.slide=slide;
            v.push_back({name,c});
        };
        add("Suave",1.08f,0,0,1,0); add("Pop",1.18f,0,0,3,0);
        add("Resorte",1.16f,0,0,4,0); add("Elevar",1.05f,8,0,2,0);
        add("Flotar",1.08f,5,0,1,2); add("Latido",1.10f,0,0,1,1);
        add("Gelatina",1.08f,0,0,4,4); add("Balanceo",1.08f,0,0,1,3);
        add("Inclinar derecha",1.1f,0,12,3,0); add("Inclinar izquierda",1.1f,0,-12,3,0);
        add("Comprimir",.9f,0,0,2,0); add("Ancho",1.05f,0,0,3,0,1.2f);
        add("Alto",1.05f,0,0,3,0,.82f); add("Salto",1.12f,14,0,4,0);
        add("Deslizar derecha",1.05f,0,0,2,0,1.f,9); add("Deslizar izquierda",1.05f,0,0,2,0,1.f,-9);
        add("Giro",1.08f,0,90,2,0); add("Media vuelta",1.05f,0,180,3,0);
        add("Despegar",1.14f,12,-12,3,0); add("Respirar",1.02f,0,0,1,1);
        add("Flan",1.12f,3,0,3,4); add("Campana",1.04f,2,0,4,3);
        add("Hundirse",.95f,-6,0,1,0); add("Quieto",1.f,0,0,1,0);
        return v;
    }();
    return list;
}
}
