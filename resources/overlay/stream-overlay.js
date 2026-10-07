'use strict';

const $ = id => document.getElementById(id);
const body = document.body;
const overlay = $('overlay'), nowEl = $('now'), queueEl = $('queue'), toastsEl = $('toasts'), statsEl = $('stats');
const params = new URLSearchParams(location.search);
const preview = location.pathname === '/preview';
const forceDemo = params.get('demo') === '1';
const embedded = params.get('embed') === '1';
const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)').matches;
const keyParam = name => {
  const value = params.get(name);
  return value && /^[a-z0-9-]{1,24}$/.test(value) ? value : null;
};
const overrides = {
  style: keyParam('style'),
  layout: keyParam('layout'),
  animation: keyParam('anim') || keyParam('animation'),
  scale: Number.parseFloat(params.get('scale')),
};

const DIFF = {
  '-1': ['Auto', '#f5c96b', 'auto'], 0: ['N/A', '#9aa3b5', '00'], 1: ['Easy', '#5ad1ff', '01'],
  2: ['Normal', '#67e36b', '02'], 3: ['Hard', '#ffb347', '03'], 4: ['Harder', '#ff6b5a', '04'],
  5: ['Insane', '#ff5ad6', '05'], 6: ['Hard Demon', '#ff3355', '06'], 7: ['Easy Demon', '#d07bff', '07'],
  8: ['Medium Demon', '#ff6fae', '08'], 9: ['Insane Demon', '#ff4f4f', '09'], 10: ['Extreme Demon', '#ff2a2a', '10'],
};
const LENGTHS = ['Tiny', 'Short', 'Medium', 'Long', 'XL'];
const PLATFORMS = {
  twitch: ['Twitch', '#a970ff'], youtube: ['YouTube', '#ff3b3b'], kick: ['Kick', '#53fc18'],
  tiktok: ['TikTok', '#ff2d6f'], web: ['Web', '#4cc9ff'],
};
const PARTICLES = {
  gd: 'squares', neon: 'sparks', synthwave: 'sparks', arcade: 'pixels', terminal: 'rain', glitch: 'pixels',
  holo: 'sparkles', aurora: 'dust', inferno: 'embers', frost: 'snow', galaxy: 'stars', royal: 'sparkles',
  pastel: 'hearts', cozy: 'dust',
};
const SOUND = { gd: [784, 1047, 1319], arcade: [523, 659, 784, 1047], terminal: [880, 880], glitch: [330, 990] };

const agoEs = sec => sec < 60 ? 'ahora' : sec < 3600 ? `hace ${Math.floor(sec / 60)} min` : `hace ${Math.floor(sec / 3600)} h`;
const agoEn = sec => sec < 60 ? 'now' : sec < 3600 ? `${Math.floor(sec / 60)}m ago` : `${Math.floor(sec / 3600)}h ago`;
const COPY = {
  base: {
    brand: 'Level Requests', now: 'Jugando ahora', waiting: 'En espera', waitTitle: 'Esperando el proximo nivel…',
    next: 'Siguientes niveles', random: 'Cola aleatoria', randomHint: 'el orden se elige al jugar',
    empty: 'La cola esta lista para recibir nuevos niveles', complete: '¡Nivel completado!', best: '¡Nuevo récord!',
    newReq: 'Nuevo request', by: 'por ', req: 'pedido por ', progress: 'Progreso', bestLabel: 'Mejor',
    attempt: 'Intento', practice: 'Práctica', platformer: 'Plataformas', closed: 'Cola cerrada',
    count: (n, q) => `${n} en cola - ${q || 'Todas'}`,
    stats: s => `Recibidos ${s.received}  ·  Jugados ${s.played}  ·  Espera media ${fmtWait(s.averageWait)}`,
    ago: agoEs,
  },
  gd: {
    now: 'Now Playing', waiting: 'Waiting', waitTitle: 'Waiting for a level...', next: 'Up Next',
    random: 'Random Order', randomHint: 'picked when played', empty: 'No requests yet. Send one in chat!',
    complete: 'Level Complete!', best: 'New Best!', newReq: 'New Request!', by: 'By ', req: 'Sent by ',
    progress: 'Normal Mode', bestLabel: 'Best', attempt: 'Attempt', practice: 'Practice Mode',
    platformer: 'Platformer', closed: 'Closed', count: n => `${n} Requests`, ago: agoEn,
    stats: s => `Received ${s.received}  ·  Played ${s.played}  ·  Avg wait ${fmtWait(s.averageWait)}`,
  },
  arcade: {
    now: 'NOW PLAYING', waiting: 'INSERT COIN', waitTitle: 'PRESS START', next: 'NEXT STAGE',
    complete: 'STAGE CLEAR!', best: 'HI-SCORE!', newReq: 'NEW CHALLENGER!', progress: 'STAGE', attempt: 'CREDIT',
  },
  terminal: {
    now: 'now_playing', waiting: 'idle', waitTitle: 'awaiting input', next: 'queue --next',
    complete: 'exit 0: nivel completado', best: 'record++', newReq: 'incoming request', progress: 'progress',
  },
  esports: { now: 'LIVE // NOW', waiting: 'STANDBY', next: 'UP NEXT', newReq: 'INCOMING', complete: 'GG!', best: 'PERSONAL BEST' },
  comic: { complete: '¡BOOM! ¡Completado!', best: '¡POW! ¡Récord!', newReq: '¡ZAS! Nuevo request' },
  brutal: { now: 'AHORA', next: 'DESPUÉS', newReq: 'NUEVO', complete: 'HECHO.', best: 'RÉCORD.' },
  royal: { now: 'En la corte', next: 'Audiencias pendientes', newReq: 'Nueva petición real' },
  cozy: { now: 'Sonando ahora', next: 'Para despues', newReq: 'Nuevo pedido' },
};

const fmtWait = sec => !sec ? '-' : sec < 60 ? `${sec}s` : sec < 3600 ? `${Math.round(sec / 60)} min` : `${(sec / 3600).toFixed(1)} h`;
const hexRgb = hex => {
  const value = Number.parseInt((hex || '#000000').slice(1), 16);
  return `${value >> 16 & 255},${value >> 8 & 255},${value & 255}`;
};
const node = (tag, cls, text) => {
  const n = document.createElement(tag);
  if (cls) n.className = cls;
  if (text !== undefined) n.textContent = text;
  return n;
};
// data-gd marks text that the gd style swaps for the game's own bitmap font.
const label = (el, text, font) => {
  el.textContent = text;
  el.dataset.text = text;
  if (font) el.dataset.gd = font;
  return el;
};
const restart = (el, cls) => {
  el.classList.remove(cls);
  void el.offsetWidth;
  el.classList.add(cls);
};

let S = null, C = null, copy = COPY.base, styleKey = 'glass';
let lastConfig = '', nowKey = '', nowLevel = '', queueKey = '', swapToken = 0;
let prevPercent = null, celebratedKey = '', bestKey = '', lastMilestone = 0;
let seenLatest = null, pendingToast = null;

/* ---------- geometry dash assets ---------- */

const GD = {
  fonts: {}, sprites: {}, fontsReady: false, sheetReady: false, fontLoad: null, sheetLoad: null,

  loadImage(src) {
    return new Promise((resolve, reject) => {
      const img = new Image();
      img.onload = () => resolve(img);
      img.onerror = reject;
      img.src = src;
    });
  },

  async loadFont(key) {
    const [text, img] = await Promise.all([
      fetch(`/gd/${key}.fnt`).then(r => { if (!r.ok) throw new Error(key); return r.text(); }),
      this.loadImage(`/gd/${key}.png`),
    ]);
    const font = { chars: new Map(), kern: new Map(), line: 0, img };
    for (const line of text.split(/\r?\n/)) {
      const type = line.split(' ', 1)[0];
      const a = {};
      line.replace(/(\w+)=("[^"]*"|\S+)/g, (_, k, v) => { a[k] = v.replace(/"/g, ''); });
      if (type === 'common') font.line = +a.lineHeight;
      else if (type === 'char') font.chars.set(+a.id, { x: +a.x, y: +a.y, w: +a.width, h: +a.height, xo: +a.xoffset, yo: +a.yoffset, xa: +a.xadvance });
      else if (type === 'kerning') font.kern.set(`${a.first},${a.second}`, +a.amount);
    }
    if (!font.line || !font.chars.size) throw new Error(key);
    this.fonts[key] = font;
  },

  ensureFonts() {
    this.fontLoad ??= Promise.all([this.loadFont('gold'), this.loadFont('big')])
      .then(() => { this.fontsReady = true; rerender(); })
      .catch(() => {});
  },

  ensureSheet() {
    this.sheetLoad ??= this.loadSheet().then(() => { this.sheetReady = true; rerender(); }).catch(() => {});
  },

  async loadSheet() {
    const wanted = new Set(['GJ_starsIcon_001.png', 'GJ_moonsIcon_001.png']);
    Object.values(DIFF).forEach(d => wanted.add(`diffIcon_${d[2]}_btn_001.png`));
    const [xml, img] = await Promise.all([
      fetch('/gd/sheet.plist').then(r => { if (!r.ok) throw new Error('sheet'); return r.text(); }),
      this.loadImage('/gd/sheet.png'),
    ]);
    const doc = new DOMParser().parseFromString(xml, 'application/xml');
    const pairs = dict => {
      const out = {};
      const kids = [...dict.children];
      for (let i = 0; i + 1 < kids.length; i += 2) out[kids[i].textContent] = kids[i + 1];
      return out;
    };
    const top = doc.querySelector('plist > dict');
    const frames = top && pairs(top).frames;
    if (!frames) throw new Error('frames');
    const kids = [...frames.children];
    for (let i = 0; i + 1 < kids.length; i += 2) {
      const name = kids[i].textContent;
      if (!wanted.has(name)) continue;
      const f = pairs(kids[i + 1]);
      const rect = (f.textureRect || f.frame)?.textContent.match(/-?\d+/g)?.map(Number);
      if (!rect || rect.length < 4) continue;
      const rotated = (f.textureRotated || f.rotated)?.tagName === 'true';
      const [x, y, w, h] = rect;
      const canvas = document.createElement('canvas');
      canvas.width = w;
      canvas.height = h;
      const ctx = canvas.getContext('2d');
      if (rotated) {
        // texturepacker stores rotated frames 90 degrees clockwise.
        ctx.translate(0, h);
        ctx.rotate(-Math.PI / 2);
        ctx.drawImage(img, x, y, h, w, 0, 0, h, w);
      } else {
        ctx.drawImage(img, x, y, w, h, 0, 0, w, h);
      }
      this.sprites[name] = canvas.toDataURL();
    }
  },

  sprite(name, cls = 'gd-sprite') {
    const src = this.sprites[name];
    if (!src) return null;
    const img = node('img', cls);
    img.src = src;
    img.alt = '';
    return img;
  },

  render(text, key) {
    const font = this.fonts[key];
    if (!font) return null;
    const codes = [...text.normalize('NFD').replace(/[\u0300-\u036f¡¿]/g, '')].map(ch => ch.codePointAt(0));
    if (!codes.length) return null;
    const missing = codes.filter(code => !font.chars.has(code)).length;
    if (missing > codes.length * 0.3) return null;
    const glyphs = [];
    let pen = 0, prev = 0, right = 0;
    for (const code of codes) {
      const g = font.chars.get(code) || font.chars.get(63);
      if (!g) continue;
      if (prev) pen += font.kern.get(`${prev},${code}`) || 0;
      glyphs.push([g, pen]);
      right = Math.max(right, pen + g.xo + g.w, pen + g.xa);
      pen += g.xa;
      prev = code;
    }
    const canvas = document.createElement('canvas');
    canvas.width = Math.max(1, Math.ceil(right));
    canvas.height = font.line;
    const ctx = canvas.getContext('2d');
    for (const [g, x] of glyphs) {
      if (g.w && g.h) ctx.drawImage(font.img, g.x, g.y, g.w, g.h, x + g.xo, g.yo, g.w, g.h);
    }
    canvas.className = 'gd-text';
    return canvas;
  },
};

function paintGD(scope) {
  if (styleKey !== 'gd' || !GD.fontsReady) return;
  scope.querySelectorAll('[data-gd]').forEach(el => {
    const canvas = GD.render(el.dataset.text || '', el.dataset.gd);
    if (!canvas) return;
    el.replaceChildren(canvas);
    el.classList.add('gd-painted');
    el.setAttribute('aria-label', el.dataset.text);
  });
}

function unpaintGD(scope) {
  scope.querySelectorAll('.gd-painted').forEach(el => {
    el.textContent = el.dataset.text || '';
    el.classList.remove('gd-painted');
  });
}

function setText(el, text) {
  if (!el || el.dataset.text === text) return;
  label(el, text);
  if (el.dataset.gd) paintGD(el.parentElement || el);
}

/* ---------- particles ---------- */

const FX = {
  canvas: $('fx'), ctx: null, kind: 'none', parts: [], bursts: [], raf: 0, last: 0, dpr: 1, w: 0, h: 0,

  init() {
    this.ctx = this.canvas.getContext('2d');
    const resize = () => {
      this.dpr = Math.min(devicePixelRatio || 1, 2);
      this.w = innerWidth;
      this.h = innerHeight;
      this.canvas.width = this.w * this.dpr;
      this.canvas.height = this.h * this.dpr;
    };
    addEventListener('resize', resize);
    resize();
  },

  setKind(kind) {
    if (reducedMotion) kind = 'none';
    if (kind === this.kind) return;
    this.kind = kind;
    this.parts = [];
    this.wake();
  },

  wake() {
    if (!this.raf && (this.kind !== 'none' || this.bursts.length)) {
      this.last = performance.now();
      this.raf = requestAnimationFrame(t => this.frame(t));
    }
  },

  accent: '#ffffff',

  spawn(r) {
    const k = this.kind, rand = (a, b) => a + Math.random() * (b - a);
    const p = { x: rand(r.left, r.right), y: rand(r.top, r.bottom), vx: 0, vy: 0, life: 0, max: rand(3, 7), size: rand(2, 5), rot: rand(0, 6.3), spin: rand(-1, 1), seed: Math.random() * 100 };
    if (k === 'squares') Object.assign(p, { y: r.bottom, vy: rand(-26, -12), size: rand(4, 10), max: rand(4, 8) });
    else if (k === 'embers') Object.assign(p, { y: rand(r.bottom - 30, r.bottom + 10), vy: rand(-45, -20), size: rand(1.5, 3.5), max: rand(2, 4.5) });
    else if (k === 'snow') Object.assign(p, { y: r.top - 10, vy: rand(14, 34), size: rand(1.5, 4), max: rand(6, 11) });
    else if (k === 'sparks') Object.assign(p, { vy: rand(-18, -6), vx: rand(-6, 6), size: rand(1, 2.6), max: rand(2, 5) });
    else if (k === 'hearts') Object.assign(p, { y: r.bottom, vy: rand(-30, -14), size: rand(7, 13), max: rand(4, 7) });
    else if (k === 'rain') Object.assign(p, { x: r.left + Math.floor(rand(0, (r.right - r.left) / 16)) * 16, y: r.top - 20, vy: rand(70, 140), size: 13, max: rand(2.5, 5) });
    else if (k === 'dust') Object.assign(p, { vx: rand(-8, 8), vy: rand(-8, 4), size: rand(2, 6), max: rand(5, 10) });
    else if (k === 'stars') Object.assign(p, { size: rand(.8, 2.4), max: rand(2, 6), shoot: Math.random() < .06 });
    if (p.shoot) Object.assign(p, { vx: rand(180, 280), vy: rand(60, 110), x: rand(r.left, (r.left + r.right) / 2), y: rand(r.top, r.top + 40), max: 1 });
    this.parts.push(p);
  },

  burst(rect, count, palette) {
    if (reducedMotion) return;
    const colors = palette || [this.accent, '#ffffff', '#ffd84a', '#5ad1ff', '#ff6fae', '#7dff00'];
    for (let i = 0; i < count; i++) {
      const angle = -Math.PI / 2 + (Math.random() - .5) * Math.PI * 1.25;
      const speed = 220 + Math.random() * 420;
      this.bursts.push({
        x: rect.left + rect.width * (.25 + Math.random() * .5), y: rect.top + rect.height * .45,
        vx: Math.cos(angle) * speed, vy: Math.sin(angle) * speed, rot: Math.random() * 6, spin: (Math.random() - .5) * 14,
        w: 5 + Math.random() * 6, h: 3 + Math.random() * 5, life: 0, max: 1.6 + Math.random() * 1.2,
        color: colors[i % colors.length], square: styleKey === 'gd' || styleKey === 'arcade',
      });
    }
    this.wake();
  },

  frame(t) {
    const dt = Math.min((t - this.last) / 1000, .05);
    this.last = t;
    const ctx = this.ctx;
    ctx.setTransform(this.dpr, 0, 0, this.dpr, 0, 0);
    ctx.clearRect(0, 0, this.w, this.h);

    if (this.kind !== 'none' && !body.classList.contains('idle')) {
      const b = overlay.getBoundingClientRect();
      const r = { left: b.left - 40, right: b.right + 40, top: b.top - 30, bottom: b.bottom + 20 };
      const target = this.kind === 'rain' ? 34 : this.kind === 'stars' ? 46 : 28;
      if (this.parts.length < target && Math.random() < .35) this.spawn(r);
      this.drawAmbient(ctx, dt);
    }

    for (const p of this.bursts) {
      p.life += dt;
      p.vy += 760 * dt;
      p.vx *= 1 - 1.3 * dt;
      p.x += p.vx * dt;
      p.y += p.vy * dt;
      p.rot += p.spin * dt;
      ctx.save();
      ctx.globalAlpha = Math.max(0, 1 - p.life / p.max);
      ctx.translate(p.x, p.y);
      ctx.rotate(p.rot);
      ctx.fillStyle = p.color;
      if (p.square) {
        ctx.fillRect(-p.w / 2, -p.w / 2, p.w, p.w);
        ctx.strokeStyle = 'rgba(0,0,0,.6)';
        ctx.strokeRect(-p.w / 2, -p.w / 2, p.w, p.w);
      } else {
        ctx.fillRect(-p.w / 2, -p.h / 2, p.w, p.h * Math.abs(Math.cos(p.rot * 2)) + 1);
      }
      ctx.restore();
    }
    this.bursts = this.bursts.filter(p => p.life < p.max && p.y < this.h + 40);

    if (this.kind !== 'none' || this.bursts.length) {
      this.raf = requestAnimationFrame(next => this.frame(next));
    } else {
      this.raf = 0;
      ctx.clearRect(0, 0, this.w, this.h);
    }
  },

  drawAmbient(ctx, dt) {
    const accent = this.accent;
    for (const p of this.parts) {
      p.life += dt;
      p.x += (p.vx + (this.kind === 'snow' || this.kind === 'embers' ? Math.sin(p.life * 2 + p.seed) * 14 : 0)) * dt;
      p.y += p.vy * dt;
      p.rot += p.spin * dt;
      const fade = Math.min(1, p.life / .6, (p.max - p.life) / .8);
      if (fade <= 0) continue;
      ctx.save();
      ctx.globalAlpha = Math.max(0, fade);
      ctx.translate(p.x, p.y);
      switch (this.kind) {
        case 'squares':
          ctx.rotate(p.rot);
          ctx.globalAlpha *= .55;
          ctx.fillStyle = '#ffffff';
          ctx.fillRect(-p.size / 2, -p.size / 2, p.size, p.size);
          break;
        case 'embers':
          ctx.globalAlpha *= .6 + Math.random() * .4;
          ctx.fillStyle = p.seed > 50 ? '#ffb347' : '#ff5a1f';
          ctx.shadowColor = '#ff7a1f';
          ctx.shadowBlur = 10;
          ctx.beginPath(); ctx.arc(0, 0, p.size, 0, 7); ctx.fill();
          break;
        case 'snow':
          ctx.globalAlpha *= .85;
          ctx.fillStyle = '#fff';
          ctx.beginPath(); ctx.arc(0, 0, p.size, 0, 7); ctx.fill();
          break;
        case 'sparks':
          ctx.fillStyle = accent;
          ctx.shadowColor = accent;
          ctx.shadowBlur = 12;
          ctx.beginPath(); ctx.arc(0, 0, p.size, 0, 7); ctx.fill();
          break;
        case 'sparkles': {
          const s = p.size * 1.8 * Math.sin(Math.PI * Math.min(1, p.life / p.max));
          ctx.rotate(p.rot * .3);
          ctx.fillStyle = styleKey === 'royal' ? '#ffd56b' : `hsl(${(p.seed * 3.6 + p.life * 60) % 360} 100% 80%)`;
          ctx.beginPath();
          ctx.moveTo(0, -s * 2); ctx.quadraticCurveTo(0, 0, s * 2, 0); ctx.quadraticCurveTo(0, 0, 0, s * 2);
          ctx.quadraticCurveTo(0, 0, -s * 2, 0); ctx.quadraticCurveTo(0, 0, 0, -s * 2);
          ctx.fill();
          break;
        }
        case 'hearts':
          ctx.rotate(Math.sin(p.life * 2 + p.seed) * .3);
          ctx.globalAlpha *= .75;
          ctx.fillStyle = p.seed > 50 ? '#ff8fc7' : '#c7a5ff';
          ctx.font = `${p.size * 2}px sans-serif`;
          ctx.textAlign = 'center';
          ctx.fillText(p.seed > 75 ? '✿' : '♥', 0, 0);
          break;
        case 'rain':
          ctx.fillStyle = 'rgba(51,255,102,.55)';
          ctx.font = '13px Consolas, monospace';
          for (let i = 0; i < 6; i++) {
            ctx.globalAlpha = Math.max(0, fade) * (1 - i / 6) * .7;
            ctx.fillText(String.fromCharCode(0x30a0 + ((p.seed * 7 + i * 13 + Math.floor(p.life * 8)) % 96)), 0, -i * 15);
          }
          break;
        case 'pixels':
          ctx.globalAlpha *= Math.floor(p.life * 4) % 2 ? .9 : .25;
          ctx.fillStyle = p.seed > 66 ? accent : p.seed > 33 ? '#00fff9' : '#ff00c1';
          ctx.fillRect(0, 0, 4, 4);
          break;
        case 'dust':
          ctx.globalAlpha *= .35;
          ctx.fillStyle = styleKey === 'aurora' ? accent : '#ffe2b8';
          ctx.shadowColor = ctx.fillStyle;
          ctx.shadowBlur = 8;
          ctx.beginPath(); ctx.arc(0, 0, p.size, 0, 7); ctx.fill();
          break;
        case 'stars':
          if (p.shoot) {
            const grad = ctx.createLinearGradient(0, 0, -p.vx * .25, -p.vy * .25);
            grad.addColorStop(0, '#fff');
            grad.addColorStop(1, 'transparent');
            ctx.strokeStyle = grad;
            ctx.lineWidth = 2;
            ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(-p.vx * .25, -p.vy * .25); ctx.stroke();
          } else {
            ctx.globalAlpha *= .4 + .6 * Math.abs(Math.sin(p.life * 3 + p.seed));
            ctx.fillStyle = p.seed > 70 ? '#ffd6ff' : '#fff';
            ctx.beginPath(); ctx.arc(0, 0, p.size, 0, 7); ctx.fill();
          }
          break;
      }
      ctx.restore();
    }
    this.parts = this.parts.filter(p => p.life < p.max);
  },
};

/* ---------- sound ---------- */

let audio = null;
function chime() {
  try {
    audio ??= new AudioContext();
    if (audio.state === 'suspended') audio.resume();
    const notes = SOUND[styleKey] || [660, 990];
    const square = styleKey === 'arcade' || styleKey === 'gd';
    const t = audio.currentTime + .02;
    notes.forEach((freq, i) => {
      const osc = audio.createOscillator(), gain = audio.createGain();
      const at = t + i * .09;
      osc.type = square ? 'square' : 'sine';
      osc.frequency.value = freq;
      gain.gain.setValueAtTime(0, at);
      gain.gain.linearRampToValueAtTime(square ? .07 : .16, at + .012);
      gain.gain.exponentialRampToValueAtTime(.0008, at + .38);
      osc.connect(gain).connect(audio.destination);
      osc.start(at);
      osc.stop(at + .42);
    });
  } catch {}
}

/* ---------- building blocks ---------- */

function diffIcon(item, size) {
  const [name, color, frame] = DIFF[item.difficulty] || DIFF[0];
  const el = node('span', `diff diff-${size}`);
  el.style.setProperty('--dc', color);
  el.title = name;
  const short = node('b', 'diff-short', name.split(' ').map(word => word[0]).join(''));
  const sprite = GD.sprite(`diffIcon_${frame}_btn_001.png`);
  if (sprite) {
    el.classList.add('has-sprite');
    el.append(sprite);
  }
  el.append(short);
  if (size === 'big' && item.stars > 0) el.append(starCount(item));
  return el;
}

function starCount(item) {
  const el = node('span', 'stars');
  el.append(label(node('span', 'stars-n'), String(item.stars), 'big'));
  const icon = GD.sprite(item.platformer ? 'GJ_moonsIcon_001.png' : 'GJ_starsIcon_001.png', 'gd-sprite star-icon');
  el.append(icon || node('i', 'star-glyph', item.platformer ? '☾' : '★'));
  return el;
}

function platformChip(key, name) {
  const [platform, color] = PLATFORMS[key] || ['', 'var(--accent)'];
  const chip = node('span', `requester plat-${key || 'none'}`);
  chip.style.setProperty('--pc', color);
  if (C.showPlatform && platform) chip.append(node('i', 'plat-name', platform));
  chip.append(node('span', 'req-name', name));
  return chip;
}

function metaSpan(prefix, value, cls = '', font) {
  const span = node('span', cls);
  if (prefix) span.append(node('span', 'meta-k', prefix));
  span.append(label(node('strong', 'meta-v'), value, font));
  return span;
}

function scene() {
  const el = node('div', 'scene');
  el.setAttribute('aria-hidden', 'true');
  el.append(node('i', 'scene-bg'), node('i', 'scene-ground'), node('i', 'scene-spike'), node('i', 'scene-cube'));
  return el;
}

function buildNow(s) {
  const p = s.playing;
  nowEl.replaceChildren();
  if (!p.active) {
    nowEl.className = 'now panel waiting';
    const copyEl = node('div', 'now-copy');
    const title = node('h1', 'level-name marquee');
    title.append(label(node('span', 'txt'), copy.waitTitle, 'big'));
    copyEl.append(label(node('div', 'eyebrow'), copy.waiting, 'gold'), title);
    nowEl.append(scene(), copyEl, node('div', 'waiting-orb'), node('div', 'flash'));
    paintGD(nowEl);
    return;
  }

  nowEl.className = `now panel${p.practice ? ' practice' : ''}`;
  const main = node('div', 'now-body');
  if (C.showDifficulty && p.known) main.append(diffIcon(p, 'big'));

  const copyEl = node('div', 'now-copy');
  const eyebrow = node('div', 'eyebrow');
  eyebrow.append(label(node('span', 'eyebrow-text'), copy.now, 'gold'));
  if (C.showAttempts && p.practice) eyebrow.append(node('span', 'badge practice-badge', copy.practice));
  if (p.platformer) eyebrow.append(node('span', 'badge plat-badge', copy.platformer));
  const title = node('h1', 'level-name marquee');
  title.append(label(node('span', 'txt'), p.name || `Nivel ${p.id}`, 'big'));

  const meta = node('div', 'meta');
  if (C.showAuthor && p.author) meta.append(metaSpan('', `${copy.by}${p.author}`, 'author', 'gold'));
  if (C.showLevelID && p.id > 0) meta.append(metaSpan('ID ', String(p.id), 'level-id'));
  if (C.showDifficulty && p.known && p.length >= 0 && !p.platformer && LENGTHS[p.length]) meta.append(metaSpan('', LENGTHS[p.length], 'length'));
  if (C.showAttempts) meta.append(metaSpan(`${copy.attempt} `, String(p.attempts || 0), 'attempts'));
  if (C.showRequester && p.requester) {
    const req = node('span', 'meta-req');
    req.append(node('span', 'meta-k', copy.req), platformChip(p.platform, p.requester));
    meta.append(req);
  }
  copyEl.append(eyebrow, title, meta);
  main.append(copyEl);
  nowEl.append(scene(), main);

  if (C.showProgress && !p.platformer) {
    const wrap = node('div', 'progress-wrap');
    const head = node('div', 'progress-head');
    head.append(label(node('span', 'progress-label'), p.practice ? copy.practice : copy.progress));
    head.append(node('span', 'best-label'), label(node('span', 'percent'), `${p.percent}%`, 'big'));
    const bar = node('div', 'progress');
    const fill = node('i', 'fill');
    fill.style.width = `${p.percent}%`;
    bar.append(fill, node('b', 'best-tick'));
    wrap.append(head, bar);
    nowEl.append(wrap);
  }
  nowEl.append(node('div', 'flash'));
  paintGD(nowEl);
}

function swapNow(s, animate) {
  const token = ++swapToken;
  const finish = () => {
    if (token !== swapToken) return;
    buildNow(S);
    if (animate) {
      restart(nowEl, 'swap-in');
      // swap-in would otherwise outrank the marquee and celebration animations.
      setTimeout(() => { if (token === swapToken) nowEl.classList.remove('swap-in'); }, 1700);
    }
    fitMarquees(nowEl);
    updateLive(S, true);
  };
  if (animate && nowEl.childElementCount && !body.classList.contains('anim-none')) {
    nowEl.classList.remove('swap-in');
    nowEl.classList.add('swap-out');
    setTimeout(finish, 260);
  } else {
    finish();
  }
}

function updateLive(s, fresh = false) {
  const p = s.playing;
  if (!p.active) {
    prevPercent = null;
    return;
  }
  setText(nowEl.querySelector('.attempts .meta-v'), String(p.attempts));
  setText(nowEl.querySelector('.percent'), `${p.percent}%`);
  const fill = nowEl.querySelector('.progress .fill');
  if (fill) fill.style.width = `${p.percent}%`;
  // best changes on death or completion: patched in place so a celebration is never rebuilt away.
  const showBest = C.showAttempts && p.best > 0 && !p.practice;
  const bestLabel = nowEl.querySelector('.best-label');
  if (bestLabel) {
    bestLabel.hidden = !showBest;
    bestLabel.textContent = `${copy.bestLabel} ${p.best}%`;
  }
  const tick = nowEl.querySelector('.best-tick');
  if (tick) {
    tick.hidden = !showBest || p.best >= 100;
    tick.style.left = `${p.best}%`;
  }

  const attemptKey = `${p.id}:${p.attempts}`;
  if (prevPercent !== null && !fresh) {
    const bar = nowEl.querySelector('.progress');
    const milestone = Math.floor(p.percent / 25) * 25;
    if (bar && milestone > lastMilestone && milestone > 0 && milestone < 100) restart(bar, 'milestone');
    lastMilestone = milestone;

    if (C.showCelebration && !p.practice && !p.platformer) {
      if (p.percent >= 100 && prevPercent < 100 && celebratedKey !== attemptKey) {
        celebratedKey = attemptKey;
        celebrate('complete');
      } else if (p.best > 0 && p.best < 100 && p.percent > p.best && prevPercent <= p.best && bestKey !== attemptKey) {
        bestKey = attemptKey;
        celebrate('best');
      }
    }
  } else {
    lastMilestone = Math.floor(p.percent / 25) * 25;
  }
  prevPercent = p.percent;
}

function celebrate(kind) {
  const flash = nowEl.querySelector('.flash');
  if (!flash) return;
  const complete = kind === 'complete';
  flash.replaceChildren(label(node('span', 'flash-text'), complete ? copy.complete : copy.best, 'gold'));
  if (complete) flash.append(node('i', 'ring'), node('i', 'ring r2'), node('i', 'ring r3'));
  paintGD(flash);
  flash.className = 'flash';
  nowEl.classList.remove('swap-in');
  restart(flash, complete ? 'show-complete' : 'show-best');
  restart(nowEl, complete ? 'celebrating' : 'beating');
  const rect = nowEl.getBoundingClientRect();
  FX.burst(rect, complete ? 170 : 45);
  if (complete && C.alertSound) chime();
}

function buildItem(item, index, s) {
  const li = node('li', 'queue-item panel');
  li.dataset.key = String(item.entry || item.id);
  li.append(label(node('span', 'number'), String(index + 1), 'gold'));
  if (C.showDifficulty && item.known) li.append(diffIcon(item, 'small'));
  else li.append(node('span', 'diff diff-small diff-empty'));

  const copyEl = node('div', 'queue-copy');
  const name = node('div', 'queue-name marquee');
  name.append(label(node('span', 'txt'), item.name || `Nivel ${item.id}`, 'big'));
  const meta = node('div', 'queue-meta');
  if (C.showAuthor && item.author) meta.append(label(node('span', 'q-author'), `${copy.by}${item.author}`, 'gold'));
  if (C.showLevelID) meta.append(node('span', 'q-id', `ID ${item.id}`));
  if (C.showDifficulty && item.known && item.stars > 0) meta.append(starCount(item));
  if (item.queue && item.queue !== 'General') meta.append(node('span', 'q-queue', item.queue));
  if (item.receivedAt > 0) meta.append(node('span', 'ago', copy.ago(Math.max(0, s.serverTime - item.receivedAt))));
  copyEl.append(name, meta);
  li.append(copyEl);
  if (C.showRequester && item.requester) li.append(platformChip(item.platform, item.requester));
  return li;
}

function renderQueue(s) {
  const items = s.queue;
  if (!items.length) {
    if (queueEl.firstElementChild?.classList.contains('empty')) return;
    const empty = node('li', 'empty panel swap-in');
    empty.append(label(node('span'), copy.empty));
    queueEl.replaceChildren(empty);
    return;
  }

  const scale = overlay.getBoundingClientRect().width / (overlay.offsetWidth || 1) || 1;
  const old = new Map();
  for (const li of queueEl.children) {
    if (li.dataset.key) old.set(li.dataset.key, { li, rect: li.getBoundingClientRect() });
  }
  const flags = [C.showAuthor, C.showLevelID, C.showRequester, C.showDifficulty, C.showPlatform, GD.sheetReady];
  const next = items.map((item, index) => {
    const key = String(item.entry || item.id);
    const sig = JSON.stringify([item.id, item.name, item.author, item.requester, item.platform, item.known,
      item.difficulty, item.stars, item.platformer, item.queue, flags]);
    const prev = old.get(key);
    if (prev && prev.li.dataset.sig === sig) {
      old.delete(key);
      const number = prev.li.querySelector('.number');
      if (number && number.dataset.text !== String(index + 1)) {
        label(number, String(index + 1), 'gold');
        paintGD(prev.li);
      }
      const ago = prev.li.querySelector('.ago');
      if (ago) ago.textContent = copy.ago(Math.max(0, s.serverTime - item.receivedAt));
      return { li: prev.li, rect: prev.rect };
    }
    const li = buildItem(item, index, s);
    li.dataset.sig = sig;
    li.classList.add('swap-in');
    li.style.animationDelay = `${Math.min(index * 60, 300)}ms`;
    // a moved node replays its css animations, so the entrance class must not outlive it.
    li.addEventListener('animationend', event => {
      if (event.target !== li) return;
      li.classList.remove('swap-in');
      li.style.animationDelay = '';
    });
    paintGD(li);
    return { li, rect: null, fresh: !prev };
  });

  old.forEach(entry => entry.li.remove());
  for (const child of [...queueEl.children]) if (!child.dataset.key) child.remove();
  next.forEach((entry, index) => {
    const at = queueEl.children[index];
    if (at !== entry.li) queueEl.insertBefore(entry.li, at || null);
  });
  if (body.classList.contains('anim-none') || reducedMotion) {
    fitMarquees(queueEl);
    return;
  }
  for (const entry of next) {
    if (!entry.rect) continue;
    const rect = entry.li.getBoundingClientRect();
    const dx = (entry.rect.left - rect.left) / scale, dy = (entry.rect.top - rect.top) / scale;
    if (Math.abs(dx) > 1 || Math.abs(dy) > 1) {
      entry.li.animate([{ transform: `translate(${dx}px,${dy}px)` }, { transform: 'none' }],
        { duration: 520, easing: 'cubic-bezier(.22,1,.36,1)' });
    }
  }
  fitMarquees(queueEl);
}

function fitMarquees(scope) {
  requestAnimationFrame(() => {
    scope.querySelectorAll('.marquee').forEach(el => {
      const inner = el.firstElementChild;
      if (!inner || inner.classList.contains('gd-painted')) return;
      el.classList.remove('scrolling');
      const over = inner.scrollWidth - el.clientWidth;
      if (over > 4) {
        el.style.setProperty('--marquee', `${-over - 8}px`);
        el.style.setProperty('--marquee-time', `${Math.max(6, over / 26 + 5)}s`);
        el.classList.add('scrolling');
      }
    });
  });
}

function toast(item) {
  const t = node('div', 'toast panel');
  const [platform, color] = PLATFORMS[item.platform] || ['', 'var(--accent)'];
  t.style.setProperty('--pc', color);
  const icon = node('div', 'toast-icon');
  if (C.showDifficulty && item.known) icon.append(diffIcon(item, 'small'));
  else icon.append(node('span', 'toast-glyph', platform ? platform[0] : '+'));
  const text = node('div', 'toast-copy');
  text.append(
    label(node('div', 'toast-title'), copy.newReq, 'gold'),
    label(node('div', 'toast-name'), item.name || `ID ${item.id}`, 'big'),
    node('div', 'toast-sub', [C.showRequester && item.requester && `${copy.req}${item.requester}`, C.showPlatform && platform].filter(Boolean).join('  ·  ')),
  );
  t.append(icon, text, node('i', 'toast-timer'));
  toastsEl.prepend(t);
  paintGD(t);
  while (toastsEl.children.length > 3) toastsEl.lastElementChild.remove();
  setTimeout(() => {
    t.classList.add('out');
    setTimeout(() => t.remove(), 450);
  }, 5600);
  if (C.alertSound) chime();
}

function checkLatest(s) {
  const latest = s.latest || {};
  const entry = latest.entry || 0;
  if (!C.showAlerts) pendingToast = null;
  if (pendingToast && entry !== pendingToast.entry) pendingToast = null;
  if (seenLatest === null) {
    seenLatest = entry;
    return;
  }
  if (entry > seenLatest) {
    seenLatest = entry;
    if (C.showAlerts) pendingToast = { entry, since: performance.now() };
  }
  // give the level lookup a moment so the alert shows a name instead of an id.
  if (pendingToast && latest.entry === pendingToast.entry
      && (latest.known || performance.now() - pendingToast.since > 2600)) {
    pendingToast = null;
    toast(latest);
  }
}

/* ---------- config ---------- */

function withOverrides(config) {
  const c = { ...config };
  if (overrides.style) c.style = overrides.style;
  if (overrides.layout) c.layout = overrides.layout;
  if (overrides.animation) c.animation = overrides.animation;
  if (Number.isFinite(overrides.scale)) c.scale = Math.min(2, Math.max(.3, overrides.scale));
  return c;
}

function applyConfig(c) {
  const key = JSON.stringify(c);
  if (key === lastConfig) return false;
  lastConfig = key;
  C = c;
  styleKey = c.style || 'glass';
  copy = { ...COPY.base, ...(COPY[styleKey] || {}) };

  const rootStyle = document.documentElement.style;
  rootStyle.setProperty('--alpha', c.opacity);
  rootStyle.setProperty('--radius', `${c.roundness}px`);
  rootStyle.setProperty('--scale', c.scale);
  for (const prop of ['--accent', '--accent-rgb', '--panel-rgb', '--text', '--muted', '--fill']) body.style.removeProperty(prop);
  if (c.customColors) {
    body.style.setProperty('--accent', c.accent);
    body.style.setProperty('--accent-rgb', hexRgb(c.accent));
    body.style.setProperty('--panel-rgb', hexRgb(c.background));
    body.style.setProperty('--text', c.text);
    body.style.setProperty('--muted', `rgba(${hexRgb(c.text)},.66)`);
    body.style.setProperty('--fill', `linear-gradient(90deg,${c.accent},${c.text})`);
  }
  const flags = [preview || forceDemo ? 'preview' : '', embedded ? 'embedded' : '', c.customColors ? 'custom-colors' : ''];
  body.className = [`style-${styleKey}`, `layout-${c.layout}`, `anim-${c.animation}`, ...flags].filter(Boolean).join(' ');
  if (!c.showAlerts) toastsEl.replaceChildren();

  if (styleKey === 'gd') GD.ensureFonts();
  // the sheet decodes to ~16 mb; a gallery of iframes only pays that for the gd card.
  if (c.showDifficulty && (!embedded || styleKey === 'gd')) GD.ensureSheet();
  FX.accent = getComputedStyle(body).getPropertyValue('--accent').trim() || '#ffffff';
  FX.setKind(c.showParticles ? PARTICLES[styleKey] || 'none' : 'none');

  const brand = overlay.querySelector('.brand');
  unpaintGD(document);
  label(brand, copy.brand, 'gold');
  paintGD(overlay.querySelector('.header'));
  return true;
}

function rerender() {
  lastConfig = '';
  nowKey = '';
  queueKey = '';
  queueEl.replaceChildren();
  if (S) update(S, true);
}

/* ---------- demo ---------- */

const DEMO_LEVELS = [
  { id: 128451093, name: 'Celestial Drift', author: 'PaimonCreator', difficulty: 8, stars: 10, length: 3 },
  { id: 112358132, name: 'Neon Reverie', author: 'Nova', difficulty: 5, stars: 8, length: 2 },
  { id: 314159265, name: 'Afterglow', author: 'Luma', difficulty: 3, stars: 4, length: 1 },
  { id: 271828182, name: 'Skyline Rush', author: 'Kairo', difficulty: 10, stars: 10, length: 4 },
  { id: 161803398, name: 'Crimson Spiral of the Endless Night', author: 'Vexa', difficulty: 9, stars: 10, length: 3 },
  { id: 141421356, name: 'Pastel Dreams', author: 'Miko', difficulty: 2, stars: 3, length: 1 },
  { id: 173205080, name: 'Quantum Leap', author: 'Orion', difficulty: 4, stars: 6, length: 2 },
  { id: 223606797, name: 'Frostbite', author: 'Yuki', difficulty: 6, stars: 10, length: 3 },
];
const DEMO_USERS = [['tu_chat', 'twitch'], ['viewer_one', 'youtube'], ['gd_player', 'kick'], ['stream_chat', 'tiktok'], ['web_fan', 'web']];
const demo = {
  entry: 40, tick: 0, attempts: 12, best: 41, percent: 0, hold: 0, level: 0, played: 3, received: 9,
  queue: [],
  make(offset) {
    const level = DEMO_LEVELS[(this.level + offset) % DEMO_LEVELS.length];
    const [requester, platform] = DEMO_USERS[(this.entry + offset) % DEMO_USERS.length];
    return { ...level, entry: ++this.entry, requester, platform, known: true, platformer: false, queue: 'General', receivedAt: Math.floor(Date.now() / 1000) - 40 * offset };
  },
};
for (let i = 1; i <= 5; i++) demo.queue.push(demo.make(i));

function demoState(raw) {
  demo.tick++;
  if (demo.hold > 0) {
    if (--demo.hold === 0) {
      demo.level = (demo.level + 1) % DEMO_LEVELS.length;
      demo.queue.shift();
      demo.played++;
      demo.percent = 0;
      demo.attempts = 1 + Math.floor(Math.random() * 30);
      demo.best = 20 + Math.floor(Math.random() * 50);
    }
  } else if (demo.percent > 8 && Math.random() < .045) {
    demo.best = Math.max(demo.best, demo.percent);
    demo.percent = 0;
    demo.attempts++;
  } else {
    demo.percent = Math.min(100, demo.percent + 1 + Math.floor(Math.random() * 3));
    if (demo.percent === 100) demo.hold = 7;
  }
  if (demo.tick % 13 === 0 && demo.queue.length < 7) {
    demo.queue.push(demo.make(demo.queue.length + 2));
    demo.received++;
  }
  const level = DEMO_LEVELS[demo.level];
  const config = raw?.config || {
    style: 'glass', layout: 'cards', animation: 'flow', nextCount: 4, scale: 1, opacity: .86, roundness: 22,
    accent: '#a670ff', background: '#0a0d1d', text: '#ffffff', customColors: false, showLevelID: true, showAuthor: true,
    showRequester: true, showProgress: true, showQueueCount: true, showDifficulty: true, showPlatform: true,
    showAttempts: true, showStats: true, showAlerts: true, alertSound: false, showCelebration: true, showParticles: true, hideWhenIdle: false,
  };
  return {
    ...(raw || {}), config, serverTime: Math.floor(Date.now() / 1000), queueName: 'General', random: false, accepting: true,
    playing: { ...level, active: true, known: true, requester: 'tu_chat', platform: 'twitch', percent: demo.percent, best: demo.best, attempts: demo.attempts, practice: false, platformer: false },
    queue: demo.queue.slice(0, config.nextCount || 4),
    latest: demo.queue[demo.queue.length - 1] || { entry: 0 },
    pending: demo.queue.length,
    stats: { received: demo.received, played: demo.played, averageWait: 260 + demo.tick % 120 },
  };
}

/* ---------- main loop ---------- */

function update(raw, forced = false) {
  const useDemo = forceDemo || (preview && raw && !raw.playing?.active && !raw.queue?.length);
  const s = forced ? raw : useDemo ? demoState(raw) : raw;
  s.config = withOverrides(s.config);
  S = s;
  const restyled = applyConfig(s.config);
  if (restyled) {
    nowKey = '';
    queueKey = '';
    queueEl.replaceChildren();
  }
  const c = C, p = s.playing;

  const count = $('queue-count');
  count.hidden = !c.showQueueCount;
  count.textContent = copy.count(s.pending, s.queueName);
  const closed = $('closed');
  closed.hidden = s.accepting !== false;
  closed.textContent = copy.closed;
  $('next-label').textContent = s.random ? copy.random : copy.next;
  $('order-label').textContent = s.random ? copy.randomHint : '';

  const level = JSON.stringify([p.active, p.id]);
  const key = JSON.stringify([level, p.name, p.author, p.requester, p.platform, p.known, p.difficulty, p.stars, p.length,
    p.practice, p.platformer, GD.sheetReady, GD.fontsReady]);
  if (key !== nowKey) {
    const animate = level !== nowLevel || restyled;
    nowKey = key;
    nowLevel = level;
    if (animate) {
      prevPercent = null;
      celebratedKey = '';
      bestKey = '';
    }
    swapNow(s, animate);
  } else {
    updateLive(s);
  }

  const qKey = JSON.stringify([s.queue, s.serverTime >> 4]);
  if (qKey !== queueKey) {
    queueKey = qKey;
    renderQueue(s);
  }

  checkLatest(s);
  statsEl.hidden = !c.showStats || !s.stats;
  if (!statsEl.hidden) statsEl.textContent = copy.stats(s.stats);
  body.classList.toggle('idle', !!c.hideWhenIdle && !p.active && !s.queue.length && !preview);
  body.classList.toggle('closed-queue', s.accepting === false);
}

let demoFetchTick = 0;
let cachedRaw = null;
async function poll() {
  try {
    if (!forceDemo || !cachedRaw || ++demoFetchTick % 8 === 0) {
      const response = await fetch('/api/state', { cache: 'no-store' });
      if (!response.ok) throw new Error(String(response.status));
      cachedRaw = await response.json();
    }
    body.classList.remove('offline');
    // update() replaces s.config: a top-level copy keeps the cache pristine
    // without deep-cloning the whole queue every poll.
    update({ ...cachedRaw });
  } catch {
    if (forceDemo || preview) update(demoState(null));
    body.classList.add('offline');
  } finally {
    setTimeout(poll, 650);
  }
}

FX.init();
poll();
