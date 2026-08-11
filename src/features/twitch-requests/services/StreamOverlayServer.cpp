#ifdef GEODE_IS_WINDOWS
#include <winsock.h>
#endif

#include "StreamOverlayServer.hpp"

#include "TwitchLevelBriefCache.hpp"
#include "../TwitchRequestFilters.hpp"
#include "../TwitchRequestManager.hpp"
#include "../../../core/modules/ModuleRegistry.hpp"
#include "../../../utils/MainThreadDelay.hpp"

#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/PlayLayer.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <string_view>
#include <thread>

#ifdef GEODE_IS_MACOS
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace geode::prelude;

namespace paimon::twitch {

namespace {

constexpr char const* kModuleID = "paimbnails.streamoverlay.menu";
constexpr char const* kConfigKey = "twitch-requests-obs-config";
constexpr uint16_t kPort = 21680;
constexpr float kRefreshSeconds = .25f;

StreamOverlayConfig g_config;

int64_t packColor(ccColor3B color) {
    return (static_cast<int64_t>(color.r) << 16)
        | (static_cast<int64_t>(color.g) << 8)
        | static_cast<int64_t>(color.b);
}

ccColor3B unpackColor(int64_t value, ccColor3B fallback) {
    if (value < 0 || value > 0xffffff) return fallback;
    return {
        static_cast<GLubyte>((value >> 16) & 0xff),
        static_cast<GLubyte>((value >> 8) & 0xff),
        static_cast<GLubyte>(value & 0xff),
    };
}

std::string cssColor(ccColor3B color) {
    return fmt::format("#{:02x}{:02x}{:02x}", color.r, color.g, color.b);
}

void clampConfig(StreamOverlayConfig& config) {
    config.layout = static_cast<StreamOverlayLayout>(std::clamp(
        static_cast<int>(config.layout), 0, kStreamOverlayLayoutCount - 1));
    config.animation = static_cast<StreamOverlayAnimation>(std::clamp(
        static_cast<int>(config.animation), 0, kStreamOverlayAnimationCount - 1));
    config.nextCount = std::clamp(config.nextCount, 1, 8);
    config.scale = std::clamp(config.scale, .7f, 1.6f);
    config.opacity = std::clamp(config.opacity, .15f, 1.f);
    config.roundness = std::clamp(config.roundness, 0.f, 34.f);
}

void loadConfig() {
    auto saved = Mod::get()->getSavedValue<matjson::Value>(
        kConfigKey, matjson::makeObject({}));

    StreamOverlayConfig config;
    config.layout = static_cast<StreamOverlayLayout>(
        saved["layout"].asInt().unwrapOr(static_cast<int>(config.layout)));
    config.animation = static_cast<StreamOverlayAnimation>(
        saved["animation"].asInt().unwrapOr(static_cast<int>(config.animation)));
    config.nextCount = static_cast<int>(
        saved["nextCount"].asInt().unwrapOr(config.nextCount));
    config.scale = static_cast<float>(saved["scale"].asDouble().unwrapOr(config.scale));
    config.opacity = static_cast<float>(
        saved["opacity"].asDouble().unwrapOr(config.opacity));
    config.roundness = static_cast<float>(
        saved["roundness"].asDouble().unwrapOr(config.roundness));
    config.accent = unpackColor(
        saved["accent"].asInt().unwrapOr(packColor(config.accent)), config.accent);
    config.background = unpackColor(
        saved["background"].asInt().unwrapOr(packColor(config.background)), config.background);
    config.text = unpackColor(
        saved["text"].asInt().unwrapOr(packColor(config.text)), config.text);
    config.showLevelID = saved["showLevelID"].asBool().unwrapOr(config.showLevelID);
    config.showAuthor = saved["showAuthor"].asBool().unwrapOr(config.showAuthor);
    config.showRequester = saved["showRequester"].asBool().unwrapOr(config.showRequester);
    config.showProgress = saved["showProgress"].asBool().unwrapOr(config.showProgress);
    config.showQueueCount = saved["showQueueCount"].asBool().unwrapOr(config.showQueueCount);
    clampConfig(config);
    g_config = config;
}

matjson::Value configJson(StreamOverlayConfig const& config) {
    return matjson::makeObject({
        {"nextCount", config.nextCount},
        {"scale", config.scale},
        {"opacity", config.opacity},
        {"roundness", config.roundness},
        {"accent", cssColor(config.accent)},
        {"background", cssColor(config.background)},
        {"text", cssColor(config.text)},
        {"showLevelID", config.showLevelID},
        {"showAuthor", config.showAuthor},
        {"showRequester", config.showRequester},
        {"showProgress", config.showProgress},
        {"showQueueCount", config.showQueueCount},
    });
}

char const* layoutKey(StreamOverlayLayout layout) {
    switch (layout) {
        case StreamOverlayLayout::Compact: return "compact";
        case StreamOverlayLayout::Ticker: return "ticker";
        default: return "cards";
    }
}

char const* animationKey(StreamOverlayAnimation animation) {
    switch (animation) {
        case StreamOverlayAnimation::Slide: return "slide";
        case StreamOverlayAnimation::Pulse: return "pulse";
        case StreamOverlayAnimation::None: return "none";
        default: return "flow";
    }
}

char const* overlayHtml() {
    return R"HTML(<!doctype html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Paimbnails - Level Requests</title>
<style>
:root{--accent:#a670ff;--accent-rgb:166,112,255;--panel-rgb:10,13,29;--text:#fff;--alpha:.86;--radius:22px;--scale:1}
*{box-sizing:border-box}
html,body{width:100%;height:100%;margin:0;overflow:hidden;background:transparent}
body{font-family:Inter,"Segoe UI",system-ui,sans-serif;color:var(--text);text-rendering:optimizeLegibility}
.overlay{position:relative;width:min(760px,calc(100vw - 64px));margin:32px;transform:scale(var(--scale));transform-origin:top left;isolation:isolate}
.ambient{position:absolute;inset:-90px;z-index:-2;pointer-events:none;filter:blur(52px);opacity:.44}
.ambient::before,.ambient::after{content:"";position:absolute;width:280px;height:280px;border-radius:50%;background:radial-gradient(circle,rgba(var(--accent-rgb),.72),transparent 67%);animation:orbit 12s ease-in-out infinite alternate}
.ambient::after{right:0;bottom:0;opacity:.48;animation-delay:-6s;animation-duration:15s}
.glass{background:linear-gradient(135deg,rgba(var(--panel-rgb),var(--alpha)),rgba(var(--panel-rgb),calc(var(--alpha) * .76)));border:1px solid rgba(255,255,255,.12);box-shadow:0 22px 70px rgba(0,0,0,.28),inset 0 1px rgba(255,255,255,.08);backdrop-filter:blur(18px);-webkit-backdrop-filter:blur(18px);border-radius:var(--radius)}
.header{display:flex;align-items:center;gap:12px;margin:0 0 12px;padding:0 4px;text-transform:uppercase;letter-spacing:.18em;font-size:12px;font-weight:800;text-shadow:0 2px 16px rgba(0,0,0,.5)}
.live-dot{width:9px;height:9px;border-radius:50%;background:var(--accent);box-shadow:0 0 0 5px rgba(var(--accent-rgb),.14),0 0 20px var(--accent);animation:live 1.8s ease-in-out infinite}
.queue-count{margin-left:auto;padding:6px 10px;border:1px solid rgba(255,255,255,.12);border-radius:999px;background:rgba(0,0,0,.18);letter-spacing:.08em}
.now{position:relative;min-height:184px;padding:24px 26px;overflow:hidden}
.now::after{content:"";position:absolute;inset:auto -10% -65% 28%;height:150%;background:radial-gradient(ellipse,rgba(var(--accent-rgb),.24),transparent 65%);pointer-events:none;animation:breathe 4.8s ease-in-out infinite}
.eyebrow{display:flex;align-items:center;gap:9px;margin-bottom:11px;color:var(--accent);font-size:12px;font-weight:900;letter-spacing:.16em;text-transform:uppercase}
.eyebrow::before{content:"";width:24px;height:2px;border-radius:2px;background:var(--accent);box-shadow:0 0 12px var(--accent)}
.level-name{position:relative;z-index:1;margin:0;max-width:92%;font-size:36px;line-height:1.04;font-weight:900;letter-spacing:-.035em;text-wrap:balance;text-shadow:0 7px 30px rgba(0,0,0,.34)}
.meta{position:relative;z-index:1;display:flex;flex-wrap:wrap;gap:8px 16px;margin-top:12px;color:rgba(255,255,255,.72);font-size:14px;font-weight:650}
.meta span{display:inline-flex;align-items:center;gap:6px}
.meta strong{color:var(--text);font-weight:800}
.platform{color:var(--accent)!important}
.progress-wrap{position:absolute;left:26px;right:26px;bottom:21px;z-index:2}
.progress-head{display:flex;justify-content:space-between;margin-bottom:7px;font-size:11px;font-weight:800;letter-spacing:.1em;text-transform:uppercase;color:rgba(255,255,255,.62)}
.progress{height:7px;overflow:hidden;border-radius:999px;background:rgba(255,255,255,.1);box-shadow:inset 0 1px 4px rgba(0,0,0,.3)}
.progress>i{position:relative;display:block;width:0;height:100%;border-radius:inherit;background:linear-gradient(90deg,var(--accent),#fff);box-shadow:0 0 18px rgba(var(--accent-rgb),.78);transition:width .55s cubic-bezier(.22,1,.36,1)}
.progress>i::after{content:"";position:absolute;inset:0;background:linear-gradient(90deg,transparent,rgba(255,255,255,.7),transparent);transform:translateX(-100%);animation:shine 2.4s ease-in-out infinite}
.next-title{display:flex;align-items:end;gap:10px;margin:17px 5px 9px;font-size:13px;font-weight:900;letter-spacing:.13em;text-transform:uppercase}
.next-title small{font-size:10px;color:rgba(255,255,255,.48);letter-spacing:.06em}
.queue{display:grid;gap:8px;list-style:none;margin:0;padding:0}
.queue-item{position:relative;display:grid;grid-template-columns:34px minmax(0,1fr) auto;align-items:center;gap:13px;min-height:62px;padding:10px 15px;overflow:hidden}
.queue-item::before{content:"";position:absolute;inset:0 auto 0 0;width:3px;background:var(--accent);box-shadow:0 0 14px var(--accent)}
.number{display:grid;place-items:center;width:30px;height:30px;border-radius:10px;background:rgba(var(--accent-rgb),.13);color:var(--accent);font-size:12px;font-weight:900}
.queue-copy{min-width:0}
.queue-name{overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-size:16px;font-weight:850;letter-spacing:-.01em}
.queue-meta{overflow:hidden;text-overflow:ellipsis;white-space:nowrap;margin-top:3px;color:rgba(255,255,255,.54);font-size:11px;font-weight:650}
.requester{max-width:180px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;padding:6px 9px;border-radius:999px;background:rgba(var(--accent-rgb),.12);color:var(--accent);font-size:11px;font-weight:800}
.empty{display:flex;align-items:center;justify-content:center;min-height:98px;padding:18px;text-align:center;color:rgba(255,255,255,.54);font-size:13px;font-weight:700;letter-spacing:.04em}
.waiting{min-height:138px;display:flex;flex-direction:column;justify-content:center}
.waiting .level-name{color:rgba(255,255,255,.7);font-size:28px}
.waiting-orb{position:absolute;right:34px;top:38px;width:64px;height:64px;border:1px solid rgba(var(--accent-rgb),.4);border-radius:50%;box-shadow:inset 0 0 30px rgba(var(--accent-rgb),.14),0 0 40px rgba(var(--accent-rgb),.2);animation:float 3s ease-in-out infinite}
.waiting-orb::before,.waiting-orb::after{content:"";position:absolute;inset:11px;border-radius:50%;border:2px solid transparent;border-top-color:var(--accent);animation:spin 2.4s linear infinite}
.waiting-orb::after{inset:22px;animation-direction:reverse;animation-duration:1.5s}
.preview-badge{display:none;position:fixed;right:22px;bottom:20px;padding:8px 12px;border-radius:999px;background:rgba(0,0,0,.46);color:#fff;font-size:10px;font-weight:900;letter-spacing:.14em}.preview .preview-badge{display:block}
.offline .live-dot{background:#ff7474;box-shadow:0 0 0 5px rgba(255,116,116,.12)}
.swap-in{animation:swapIn .55s cubic-bezier(.16,1,.3,1) both}.swap-out{animation:swapOut .23s ease-in both}
.anim-slide .swap-in{animation-name:slideIn}.anim-pulse .swap-in{animation-name:pulseIn}.anim-none *{animation:none!important;transition:none!important}
.layout-compact{width:min(620px,calc(100vw - 64px))}.layout-compact .now{min-height:142px;padding:19px 22px}.layout-compact .level-name{font-size:29px}.layout-compact .progress-wrap{left:22px;right:22px;bottom:16px}.layout-compact .queue-item{min-height:52px}.layout-compact .queue-meta{display:none}
.layout-ticker{position:absolute;left:32px;right:32px;bottom:32px;width:auto;margin:0;display:grid;grid-template-columns:minmax(320px,.85fr) minmax(0,1.15fr);gap:10px;transform:scale(var(--scale));transform-origin:bottom left}.layout-ticker .header,.layout-ticker .next-title{display:none}.layout-ticker .now{min-height:112px;padding:17px 20px}.layout-ticker .level-name{font-size:25px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.layout-ticker .eyebrow{margin-bottom:6px}.layout-ticker .meta{margin-top:6px;font-size:12px}.layout-ticker .progress-wrap{left:20px;right:20px;bottom:12px}.layout-ticker .queue{display:flex;gap:8px;overflow:hidden}.layout-ticker .queue-item{flex:1;min-width:0;height:112px;grid-template-columns:28px minmax(0,1fr);padding:12px}.layout-ticker .requester{display:none}.layout-ticker .queue-meta{white-space:normal;line-height:1.3}.layout-ticker .ambient{inset:-50px}
@keyframes orbit{from{transform:translate(-12%,-8%) scale(.85)}to{transform:translate(30%,18%) scale(1.18)}}
@keyframes live{0%,100%{transform:scale(.85);opacity:.7}50%{transform:scale(1.15);opacity:1}}
@keyframes breathe{0%,100%{transform:scale(.92);opacity:.65}50%{transform:scale(1.08);opacity:1}}
@keyframes shine{0%,35%{transform:translateX(-120%)}70%,100%{transform:translateX(150%)}}
@keyframes float{0%,100%{transform:translateY(-4px) rotate(-3deg)}50%{transform:translateY(7px) rotate(4deg)}}
@keyframes spin{to{transform:rotate(360deg)}}
@keyframes swapIn{from{opacity:0;transform:translateY(20px) scale(.97);filter:blur(5px)}to{opacity:1;transform:none;filter:none}}
@keyframes slideIn{from{opacity:0;transform:translateX(-38px)}to{opacity:1;transform:none}}
@keyframes pulseIn{0%{opacity:0;transform:scale(.86)}65%{transform:scale(1.025)}100%{opacity:1;transform:scale(1)}}
@keyframes swapOut{to{opacity:0;transform:translateY(-8px) scale(.985);filter:blur(3px)}}
@media(max-width:700px){.overlay{width:calc(100vw - 32px);margin:16px}.level-name{font-size:29px}.layout-ticker{left:16px;right:16px;bottom:16px;grid-template-columns:1fr}.layout-ticker .queue{display:none}}
@media(prefers-reduced-motion:reduce){*{animation-duration:.001ms!important;animation-iteration-count:1!important;transition-duration:.001ms!important}}
</style>
</head>
<body>
<main id="overlay" class="overlay layout-cards anim-flow">
  <div class="ambient"></div>
  <div class="header"><i class="live-dot"></i><span>Level Requests</span><span id="queue-count" class="queue-count">0 en cola</span></div>
  <section id="now" class="now glass"></section>
  <div class="next-title"><span id="next-label">Siguientes niveles</span><small id="order-label"></small></div>
  <ol id="queue" class="queue"></ol>
</main>
<span class="preview-badge">MODO VISTA PREVIA</span>
<script>
const root=document.documentElement,overlay=document.getElementById('overlay'),now=document.getElementById('now'),queue=document.getElementById('queue');
const preview=location.pathname==='/preview';let currentKey='',queueKey='',lastConfig='';
if(preview)document.body.classList.add('preview');
const node=(tag,cls,text)=>{const n=document.createElement(tag);if(cls)n.className=cls;if(text!==undefined)n.textContent=text;return n};
const hexRgb=hex=>{const value=parseInt((hex||'#000000').slice(1),16);return `${value>>16&255},${value>>8&255},${value&255}`};
function applyConfig(c){const key=JSON.stringify(c);if(key===lastConfig)return;lastConfig=key;root.style.setProperty('--accent',c.accent);root.style.setProperty('--accent-rgb',hexRgb(c.accent));root.style.setProperty('--panel-rgb',hexRgb(c.background));root.style.setProperty('--text',c.text);root.style.setProperty('--alpha',c.opacity);root.style.setProperty('--radius',`${c.roundness}px`);root.style.setProperty('--scale',c.scale);overlay.className=`overlay layout-${c.layout} anim-${c.animation}`;}
function metaSpan(label,value,cls=''){const span=node('span',cls);span.append(label,node('strong','',value));return span}
function fillNow(s){now.replaceChildren();const p=s.playing;if(!p.active){now.className='now glass waiting swap-in';now.append(node('div','eyebrow','En espera'),node('h1','level-name','Esperando el proximo nivel…'),node('div','waiting-orb'));return}now.className='now glass swap-in';now.append(node('div','eyebrow','Jugando ahora'));const title=node('h1','level-name',p.name||`Nivel ${p.id}`);now.append(title);const meta=node('div','meta');if(s.config.showAuthor&&p.author)meta.append(metaSpan('por ',p.author));if(s.config.showLevelID)meta.append(metaSpan('ID ',String(p.id)));if(s.config.showRequester&&p.requester)meta.append(metaSpan('pedido por ',p.requester,'platform'));now.append(meta);if(s.config.showProgress){const wrap=node('div','progress-wrap');const head=node('div','progress-head');head.append(node('span','', 'Progreso'),node('span','percent',`${p.percent}%`));const bar=node('div','progress');const fill=node('i');fill.style.width=`${p.percent}%`;bar.append(fill);wrap.append(head,bar);now.append(wrap)}}
function fillQueue(s){queue.replaceChildren();if(!s.queue.length){queue.append(node('li','empty glass','La cola esta lista para recibir nuevos niveles'));return}s.queue.forEach((item,index)=>{const li=node('li','queue-item glass swap-in');li.style.animationDelay=`${Math.min(index*55,260)}ms`;li.append(node('span','number',String(index+1)));const copy=node('div','queue-copy');copy.append(node('div','queue-name',item.name||`Nivel ${item.id}`));const bits=[];if(s.config.showAuthor&&item.author)bits.push(`por ${item.author}`);if(s.config.showLevelID)bits.push(`ID ${item.id}`);copy.append(node('div','queue-meta',bits.join('  -  ')));li.append(copy);if(s.config.showRequester&&item.requester)li.append(node('span','requester',item.requester));queue.append(li)})}
function demo(s){if(!preview||s.playing.active||s.queue.length)return s;return {...s,playing:{active:true,id:128451093,name:'Celestial Drift',author:'PaimonCreator',requester:'tu_chat',percent:67},pending:12,queue:[{id:112358132,name:'Neon Reverie',author:'Nova',requester:'viewer_one'},{id:314159265,name:'Afterglow',author:'Luma',requester:'gd_player'},{id:271828182,name:'Skyline Rush',author:'Kairo',requester:'stream_chat'}]}}
function update(raw){const s=demo(raw);applyConfig(s.config);document.body.classList.toggle('offline',false);document.getElementById('queue-count').hidden=!s.config.showQueueCount;document.getElementById('queue-count').textContent=`${s.pending} en cola`;document.getElementById('next-label').textContent=s.random?'Cola aleatoria':'Siguientes niveles';document.getElementById('order-label').textContent=s.random?'el orden se elige al jugar':'';const nextCurrent=JSON.stringify([s.playing.active,s.playing.id,s.playing.name,s.playing.author,s.playing.requester,s.config.showAuthor,s.config.showLevelID,s.config.showRequester,s.config.showProgress]);if(nextCurrent!==currentKey){currentKey=nextCurrent;fillNow(s)}else if(s.playing.active&&s.config.showProgress){const percent=now.querySelector('.percent'),fill=now.querySelector('.progress i');if(percent)percent.textContent=`${s.playing.percent}%`;if(fill)fill.style.width=`${s.playing.percent}%`}const nextQueue=JSON.stringify([s.queue,s.config.showAuthor,s.config.showLevelID,s.config.showRequester]);if(nextQueue!==queueKey){queueKey=nextQueue;fillQueue(s)}}
async function poll(){try{const response=await fetch('/api/state',{cache:'no-store'});if(!response.ok)throw new Error();update(await response.json())}catch{document.body.classList.add('offline')}finally{setTimeout(poll,650)}}poll();
</script>
</body>
</html>)HTML";
}

#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)

#ifdef GEODE_IS_WINDOWS
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket = -1;
#endif

void closeSocket(SocketHandle socket) {
    if (socket == kInvalidSocket) return;
#ifdef GEODE_IS_WINDOWS
    closesocket(socket);
#else
    close(socket);
#endif
}

void setReceiveTimeout(SocketHandle socket) {
#ifdef GEODE_IS_WINDOWS
    DWORD timeout = 2000;
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
        reinterpret_cast<char const*>(&timeout), sizeof(timeout));
#else
    timeval timeout{2, 0};
    setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
    int noSigPipe = 1;
    setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe));
#endif
#endif
}

bool sendAll(SocketHandle socket, std::string const& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        int count = send(socket, data.data() + sent,
            static_cast<int>(std::min<size_t>(data.size() - sent, 16 * 1024)), 0);
        if (count <= 0) return false;
        sent += static_cast<size_t>(count);
    }
    return true;
}

std::string httpResponse(
    int code,
    std::string_view status,
    std::string_view type,
    std::string const& body
) {
    return fmt::format(
        "HTTP/1.1 {} {}\r\n"
        "Content-Type: {}; charset=utf-8\r\n"
        "Content-Length: {}\r\n"
        "Cache-Control: no-store, no-cache, must-revalidate\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Connection: close\r\n\r\n{}",
        code, status, type, body.size(), body);
}

#endif

class StreamOverlayTicker final : public CCNode {
public:
    static StreamOverlayTicker* create() {
        auto* ret = new StreamOverlayTicker();
        if (ret->init()) {
            ret->autorelease();
            return ret;
        }
        delete ret;
        return nullptr;
    }

    void update(float dt) override {
        StreamOverlayServer::get().tick(dt);
    }
};

Ref<StreamOverlayTicker> g_ticker;

} // namespace

struct StreamOverlayServer::Impl {
    std::atomic_bool stopping = false;
    std::atomic_bool running = false;
    std::thread worker;
    mutable std::mutex mutex;
    std::string payload = "{}";
    std::string status = "Apagado";

#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
    std::atomic<SocketHandle> listener = kInvalidSocket;

    void setStatus(std::string text) {
        std::lock_guard lock(mutex);
        status = std::move(text);
    }

    std::string responseFor(std::string path) const {
        auto query = path.find('?');
        if (query != std::string::npos) path.resize(query);

        if (path == "/" || path == "/overlay" || path == "/preview") {
            return httpResponse(200, "OK", "text/html", overlayHtml());
        }
        if (path == "/api/state") {
            std::lock_guard lock(mutex);
            return httpResponse(200, "OK", "application/json", payload);
        }
        if (path == "/health") {
            return httpResponse(200, "OK", "text/plain", "Paimbnails OBS overlay OK");
        }
        if (path == "/favicon.ico") {
            return httpResponse(204, "No Content", "image/x-icon", "");
        }
        return httpResponse(404, "Not Found", "text/plain", "Not found");
    }

    void handleClient(SocketHandle client) const {
        setReceiveTimeout(client);
        std::string request;
        request.reserve(2048);
        char buffer[2048];
        while (request.size() < 16 * 1024) {
            int count = recv(client, buffer, sizeof(buffer), 0);
            if (count <= 0) break;
            request.append(buffer, static_cast<size_t>(count));
            if (request.find("\r\n\r\n") != std::string::npos) break;
        }

        auto lineEnd = request.find("\r\n");
        auto firstLine = request.substr(0, lineEnd);
        auto firstSpace = firstLine.find(' ');
        auto secondSpace = firstSpace == std::string::npos
            ? std::string::npos : firstLine.find(' ', firstSpace + 1);
        if (firstLine.compare(0, firstSpace, "GET") != 0
            || firstSpace == std::string::npos || secondSpace == std::string::npos) {
            sendAll(client, httpResponse(405, "Method Not Allowed", "text/plain", "GET only"));
            return;
        }
        sendAll(client, responseFor(
            firstLine.substr(firstSpace + 1, secondSpace - firstSpace - 1)));
    }

    void closeListener() {
        auto socket = listener.exchange(kInvalidSocket);
        if (socket == kInvalidSocket) return;
#ifdef GEODE_IS_WINDOWS
        ::shutdown(socket, 2);
#else
        ::shutdown(socket, SHUT_RDWR);
#endif
        closeSocket(socket);
    }

    void run() {
#ifdef GEODE_IS_WINDOWS
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            setStatus("Windows no pudo iniciar la red local");
            return;
        }
#endif

        auto socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == kInvalidSocket) {
            setStatus("No se pudo crear el servidor local");
#ifdef GEODE_IS_WINDOWS
            WSACleanup();
#endif
            return;
        }
        listener = socket;

#ifdef GEODE_IS_WINDOWS
        int reuse = 1;
        setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,
            reinterpret_cast<char const*>(&reuse), sizeof(reuse));
#else
        int reuse = 1;
        setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(kPort);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0
            || listen(socket, 8) != 0) {
            setStatus("El puerto 21680 ya esta ocupado");
            closeListener();
#ifdef GEODE_IS_WINDOWS
            WSACleanup();
#endif
            return;
        }

        running = true;
        setStatus("Activo en localhost:21680");
        while (!stopping) {
            fd_set readSet;
            FD_ZERO(&readSet);
            FD_SET(socket, &readSet);
            timeval timeout{0, 250000};
#ifdef GEODE_IS_WINDOWS
            int ready = select(0, &readSet, nullptr, nullptr, &timeout);
#else
            int ready = select(socket + 1, &readSet, nullptr, nullptr, &timeout);
#endif
            if (ready <= 0 || stopping) continue;

            auto client = accept(socket, nullptr, nullptr);
            if (client == kInvalidSocket) continue;
            handleClient(client);
            closeSocket(client);
        }

        running = false;
        closeListener();
#ifdef GEODE_IS_WINDOWS
        WSACleanup();
#endif
    }
#endif
};

std::vector<std::string> streamOverlayLayoutNames() {
    return {"Tarjetas", "Compacto", "Cinta inferior"};
}

std::vector<std::string> streamOverlayAnimationNames() {
    return {"Flotante", "Deslizar", "Pulso", "Sin animacion"};
}

StreamOverlayConfig const& streamOverlayConfig() {
    return g_config;
}

void setStreamOverlayConfig(StreamOverlayConfig config) {
    clampConfig(config);
    g_config = config;
    Mod::get()->setSavedValue<matjson::Value>(kConfigKey, matjson::makeObject({
        {"layout", static_cast<int>(config.layout)},
        {"animation", static_cast<int>(config.animation)},
        {"nextCount", config.nextCount},
        {"scale", config.scale},
        {"opacity", config.opacity},
        {"roundness", config.roundness},
        {"accent", packColor(config.accent)},
        {"background", packColor(config.background)},
        {"text", packColor(config.text)},
        {"showLevelID", config.showLevelID},
        {"showAuthor", config.showAuthor},
        {"showRequester", config.showRequester},
        {"showProgress", config.showProgress},
        {"showQueueCount", config.showQueueCount},
    }));
    paimon::requestDeferredModSave();
    StreamOverlayServer::get().refreshSnapshot();
}

StreamOverlayServer& StreamOverlayServer::get() {
    static StreamOverlayServer instance;
    return instance;
}

StreamOverlayServer::StreamOverlayServer() : m_impl(std::make_unique<Impl>()) {}

StreamOverlayServer::~StreamOverlayServer() {
    stop();
}

void StreamOverlayServer::init() {
    if (m_initialized) return;
    m_initialized = true;
    loadConfig();

    auto* director = CCDirector::get();
    if (director && director->getScheduler()) {
        g_ticker = StreamOverlayTicker::create();
        if (g_ticker) {
            director->getScheduler()->scheduleUpdateForTarget(g_ticker.data(), 0, false);
        }
    }
    restart();
}

void StreamOverlayServer::shutdown() {
    if (!m_initialized) return;
    m_initialized = false;
    if (g_ticker) {
        if (auto* director = CCDirector::get(); director && director->getScheduler()) {
            director->getScheduler()->unscheduleUpdateForTarget(g_ticker.data());
        }
        (void)g_ticker.take();
    }
    stop();
}

bool StreamOverlayServer::supported() const {
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
    return true;
#else
    return false;
#endif
}

void StreamOverlayServer::start() {
    if (!supported() || m_impl->worker.joinable()) return;
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
    m_impl->stopping = false;
    m_impl->running = false;
    m_impl->setStatus("Iniciando servidor local...");
    refreshSnapshot();
    try {
        m_impl->worker = std::thread([impl = m_impl.get()] { impl->run(); });
    } catch (...) {
        m_impl->setStatus("No se pudo iniciar el hilo del servidor");
    }
#endif
}

void StreamOverlayServer::stop() {
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
    m_impl->stopping = true;
    m_impl->closeListener();
    if (m_impl->worker.joinable()) m_impl->worker.join();
    m_impl->running = false;
    m_impl->setStatus("Apagado");
#endif
}

void StreamOverlayServer::restart() {
    stop();
    if (!m_initialized || !paimon::modules::isEnabled(kModuleID)) return;
    start();
}

void StreamOverlayServer::tick(float dt) {
    if (!m_initialized) return;
    if (!paimon::modules::isEnabled(kModuleID)) {
        if (m_impl->worker.joinable()) stop();
        return;
    }
    m_refreshClock += dt;
    if (m_refreshClock < kRefreshSeconds) return;
    m_refreshClock = 0.f;
    refreshSnapshot();
}

void StreamOverlayServer::refreshSnapshot() {
    if (!m_initialized || !paimon::modules::isEnabled(kModuleID)) return;

    auto& manager = TwitchRequestManager::get();
    auto& cache = TwitchLevelBriefCache::get();
    cache.tick();

    int currentID = 0;
    matjson::Value playing = matjson::makeObject({
        {"active", false}, {"id", 0}, {"name", ""}, {"author", ""},
        {"requester", ""}, {"percent", 0}, {"platform", ""},
    });

    if (auto* play = PlayLayer::get(); play && play->m_level) {
        auto* level = play->m_level;
        currentID = level->m_levelID.value();
        std::string requester;
        std::string platform;
        for (auto const& request : manager.requests()) {
            if (request.levelID != currentID) continue;
            requester = request.requester;
            platform = platformKey(request.platform);
            break;
        }
        playing = matjson::makeObject({
            {"active", true},
            {"id", currentID},
            {"name", std::string(level->m_levelName)},
            {"author", std::string(level->m_creatorName)},
            {"requester", requester},
            {"percent", std::clamp(static_cast<int>(std::round(play->getCurrentPercent())), 0, 100)},
            {"platform", platform},
        });
    }

    auto queue = matjson::Value::array();
    size_t pending = 0;
    for (auto const& request : manager.requests()) {
        if (request.played || request.levelID == currentID) continue;
        if (auto passes = requestPasses(request.levelID, !request.videoUrl.empty()); passes && !*passes) continue;
        ++pending;
        if (queue.size() >= static_cast<size_t>(g_config.nextCount)) continue;

        auto const* brief = cache.peek(request.levelID);
        if (!brief) cache.request(request.levelID);
        queue.push(matjson::makeObject({
            {"id", request.levelID},
            {"name", brief && brief->found ? brief->name : ""},
            {"author", brief && brief->found ? brief->author : ""},
            {"requester", request.requester},
            {"platform", platformKey(request.platform)},
        }));
    }

    auto config = configJson(g_config);
    config["layout"] = layoutKey(g_config.layout);
    config["animation"] = animationKey(g_config.animation);
    auto payload = matjson::makeObject({
        {"revision", static_cast<int64_t>(++m_revision)},
        {"playing", playing},
        {"queue", queue},
        {"pending", static_cast<int64_t>(pending)},
        {"random", manager.isRandomOrder()},
        {"accepting", manager.isAccepting()},
        {"config", config},
    }).dump(matjson::NO_INDENTATION);

    std::lock_guard lock(m_impl->mutex);
    m_impl->payload = std::move(payload);
}

bool StreamOverlayServer::isRunning() const {
    return m_impl->running;
}

std::string StreamOverlayServer::statusText() const {
    if (!supported()) return "Solo disponible en Windows y macOS";
    std::lock_guard lock(m_impl->mutex);
    return m_impl->status;
}

std::string StreamOverlayServer::overlayUrl() const {
    return fmt::format("http://localhost:{}/overlay", kPort);
}

std::string StreamOverlayServer::previewUrl() const {
    return fmt::format("http://localhost:{}/preview", kPort);
}

} // namespace paimon::twitch
