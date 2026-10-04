import assert from 'node:assert/strict';
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { createRequire } from 'node:module';
import { join } from 'node:path';

// PLAYWRIGHT_MODULE and CHROMIUM_PATH may point to an existing browser test installation.
const require = createRequire(import.meta.url);
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const root = new URL('../', import.meta.url);
const source = await readFile(new URL('src/features/twitch-requests/services/StreamOverlayServer.cpp', root), 'utf8');
const options = name => [...source.match(new RegExp(`k${name}\\[\\] = \\{([\\s\\S]*?)\\n\\};`))[1]
  .matchAll(/\{"([^"]+)", "([^"]+)"\}/g)].map(([, key, name]) => ({ key, name }));
const catalog = { styles: options('Styles'), layouts: options('Layouts'), animations: options('Animations') };
const config = {
  style: 'glass', layout: 'cards', animation: 'none', nextCount: 4, scale: 1, opacity: .86, roundness: 22,
  accent: '#a670ff', background: '#0a0d1d', text: '#ffffff', customColors: false, showLevelID: true,
  showAuthor: true, showRequester: true, showProgress: true, showQueueCount: true, showDifficulty: true,
  showPlatform: true, showAttempts: true, showStats: true, showAlerts: true, alertSound: false,
  showCelebration: true, showParticles: false, hideWhenIdle: false,
};
const item = { entry: 1, id: 101, name: 'Celestial Drift', author: 'Creator', requester: 'viewer', platform: 'twitch',
  known: true, difficulty: 8, stars: 10, length: 3, platformer: false, queue: 'General', receivedAt: 100 };
const state = {
  config, serverTime: 200, queueName: 'General', accepting: true, pending: 4, random: false,
  playing: { ...item, active: true, percent: 20, best: 54, attempts: 0, practice: false },
  queue: Array.from({ length: 4 }, (_, i) => ({ ...item, entry: i + 1, id: i + 101 })),
  latest: { ...item, entry: 4 }, stats: { received: 9, played: 3, averageWait: 312 },
};
const files = { '/overlay': 'stream-overlay.html', '/preview': 'stream-overlay.html',
  '/gallery': 'stream-overlay-gallery.html', '/overlay.js': 'stream-overlay.js', '/overlay.css': 'stream-overlay.css' };
const assets = { 'gold.fnt': 'goldFont-uhd.fnt', 'gold.png': 'goldFont-uhd.png', 'big.fnt': 'bigFont-uhd.fnt',
  'big.png': 'bigFont-uhd.png', 'sheet.plist': 'GJ_GameSheet03-hd.plist', 'sheet.png': 'GJ_GameSheet03-hd.png',
  'square01.png': 'GJ_square01-uhd.png', 'square02.png': 'GJ_square02-uhd.png',
  'bg.png': 'game_bg_01_001-hd.png', 'ground.png': 'groundSquare_01_001-uhd.png' };
const types = { html: 'text/html', js: 'application/javascript', css: 'text/css', png: 'image/png', plist: 'application/xml', fnt: 'text/plain' };
const server = createServer(async (req, res) => {
  const path = new URL(req.url, 'http://localhost').pathname;
  try {
    if (path === '/api/state' || path === '/api/styles') {
      res.setHeader('Content-Type', 'application/json');
      res.end(JSON.stringify(path === '/api/state' ? state : catalog));
      return;
    }
    let file;
    if (files[path]) file = new URL(`resources/overlay/${files[path]}`, root);
    else if (path.startsWith('/gd/') && assets[path.slice(4)] && process.env.GD_RESOURCES) {
      file = join(process.env.GD_RESOURCES, assets[path.slice(4)]);
    }
    if (!file) { res.writeHead(404); res.end(); return; }
    const bytes = await readFile(file);
    res.setHeader('Content-Type', `${types[String(file).split('.').pop()] || 'text/plain'}; charset=utf-8`);
    res.end(bytes);
  } catch { res.writeHead(404); res.end(); }
});
await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;
let browser;
try {
  browser = await chromium.launch({ headless: true, ...(process.env.CHROMIUM_PATH ? { executablePath: process.env.CHROMIUM_PATH } : {}) });
  const page = await browser.newPage({ viewport: { width: 1920, height: 1080 } });
  const errors = [];
  page.on('pageerror', error => errors.push(error.message));
  await page.addInitScript(() => {
    const schedule = window.setTimeout;
    window.setTimeout = (fn, ...args) => fn.name === 'poll' ? 0 : schedule(fn, ...args);
  });
  await page.goto(`${origin}/overlay`);
  await page.waitForFunction(() => !!document.querySelector('.level-name'));
  const apply = patch => page.evaluate(patch => {
    const next = structuredClone(S);
    for (const [key, value] of Object.entries(patch)) {
      next[key] = value && !Array.isArray(value) && typeof value === 'object' ? { ...next[key], ...value } : value;
    }
    update(next, true);
  }, patch);
  assert.equal(catalog.styles.length, 19);
  for (const { key: style } of catalog.styles) {
    for (const { key: layout } of catalog.layouts) {
      await apply({ config: { style, layout } });
      const result = await page.evaluate(() => {
        const visible = el => getComputedStyle(el).display !== 'none';
        return { title: !!nowEl.querySelector('.level-name'), queue: visible(queueEl),
          heading: visible(document.querySelector('.next-title')), star: nowEl.querySelector('.star-icon')?.getBoundingClientRect().width || 0 };
      });
      assert.ok(result.title, `${style}/${layout}: current level`);
      assert.equal(result.queue, !['spotlight', 'corner'].includes(layout), `${style}/${layout}: queue visibility`);
      assert.equal(result.heading, !['spotlight', 'corner', 'ticker', 'banner'].includes(layout), `${style}/${layout}: queue heading`);
      assert.ok(result.star <= 20, `${style}/${layout}: star size ${result.star}`);
    }
  }
  console.log('Passed 133 style/layout combinations');

  await apply({ config: { style: 'inferno', layout: 'cards', customColors: true, background: '#123456', text: '#abcdef' } });
  assert.equal(await page.locator('#now').evaluate(el => getComputedStyle(el).backgroundColor), 'rgba(18, 52, 86, 0.86)');
  assert.equal(await page.locator('.level-name').evaluate(el => getComputedStyle(el).color), 'rgb(171, 205, 239)');
  await apply({ config: { customColors: false } });
  await page.evaluate(() => { window.firstQueueItem = queueEl.firstElementChild; });
  await apply({ queue: [...state.queue].reverse() });
  assert.ok(await page.evaluate(() => queueEl.lastElementChild === window.firstQueueItem));

  await apply({ config: { style: 'gd', layout: 'cards' } });
  if (process.env.GD_RESOURCES) {
    await page.waitForFunction(() => GD.fontsReady && GD.sheetReady);
    assert.equal(await page.locator('#now .diff-big>.gd-sprite').count(), 1);
    assert.ok(await page.locator('#now .gd-text').count() > 0);
    assert.ok(await page.locator('#now .star-icon').evaluate(el => el.getBoundingClientRect().width <= 20));
    console.log('Passed GD bitmap fonts and sprite loading');
  }
  await apply({ config: { style: 'glass', layout: 'cards' }, playing: { attempts: 2 } });
  assert.equal(await page.locator('.attempts .meta-v').textContent(), '2');
  await apply({ playing: { percent: 60 } });
  assert.equal(await page.locator('.flash.show-best').count(), 1);
  await apply({ playing: { percent: 100 } });
  assert.equal(await page.locator('.flash.show-complete').count(), 1);
  await apply({ playing: { practice: true, percent: 0 } });
  await apply({ playing: { percent: 100 } });
  assert.equal(await page.locator('.flash.show-complete').count(), 0);
  await apply({ playing: { platformer: true } });
  assert.equal(await page.locator('.progress-wrap').count(), 0);

  await apply({ config: { showRequester: false }, latest: { entry: 5, known: true } });
  assert.equal(await page.locator('.toast').count(), 1);
  assert.ok(!(await page.locator('.toast-sub').textContent()).includes('viewer'));
  await apply({ latest: { entry: 6, known: false } });
  await apply({ config: { showAlerts: false }, latest: { known: true } });
  assert.equal(await page.locator('.toast').count(), 0);
  await apply({ config: { showAuthor: false, showLevelID: false, showDifficulty: false, showAttempts: false, showStats: false, showQueueCount: false } });
  assert.equal(await page.locator('#now .author, #now .level-id, #now .diff, #now .attempts, .queue-item .q-author, .queue-item .q-id').count(), 0);
  assert.ok(await page.locator('#stats').isHidden());
  assert.ok(await page.locator('#queue-count').isHidden());
  await apply({ config: { hideWhenIdle: true }, playing: { active: false }, queue: [] });
  assert.ok(await page.locator('body').evaluate(el => el.classList.contains('idle')));
  await apply({ playing: { active: true, name: '<img src=x onerror=alert(1)>' } });
  assert.equal(await page.locator('.level-name img').count(), 0);
  assert.equal(await page.locator('.level-name').textContent(), '<img src=x onerror=alert(1)>');
  console.log('Passed live updates, celebrations, privacy toggles, idle mode and text escaping');

  for (const { key: animation } of catalog.animations) {
    await apply({ config: { animation } });
    await page.waitForFunction(() => !nowEl.classList.contains('swap-in') && !nowEl.classList.contains('swap-out'));
    await page.waitForFunction(() => Number(getComputedStyle(nowEl).opacity) > .99);
    assert.equal(await page.locator('#now.swap-out').count(), 0, animation);
  }
  console.log('Passed 11 entrance animations');
  await page.goto(`${origin}/gallery`);
  await page.waitForFunction(() => document.querySelectorAll('.card').length === 19);
  await page.selectOption('#layout', 'ticker');
  await page.selectOption('#anim', 'bounce');
  assert.match(await page.locator('.card').nth(1).locator('code').textContent(), /style=gd&layout=ticker&anim=bounce$/);
  assert.equal(await page.locator('.card iframe').count(), 19);
  assert.deepEqual(errors, []);
  console.log('Passed gallery links; no browser script errors');
} finally {
  await browser?.close();
  server.closeAllConnections();
  await new Promise(resolve => server.close(resolve));
}
