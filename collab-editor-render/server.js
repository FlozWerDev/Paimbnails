// Paimon Collab — Real-time collaborative GD editor server (Render.com)
//
// Host-owned private rooms. A host creates a room seeded with their level,
// peers join, ops are relayed with LWW per object, and when the host leaves
// the room closes for everyone (peers get a "room_closed" message; the host's
// local copy is the canonical final state).
//
// Transport: HTTP long-polling (the mod's web::WebRequest path; cocos2d's
// WebSocket header isn't usable from Geode mods).
//
// Env vars (configure in Render dashboard):
//   COLLAB_MAX_PEERS         max editors per room          (default 10)
//   COLLAB_MAX_OBJECTS       max objects per room          (default 50000)
//   COLLAB_MAX_BODY          max request body size         (default 4mb)
//   COLLAB_MAX_OPS_PER_SEC   per-client op rate limit      (default 8000)
//   COLLAB_MAX_OPS_PER_REQUEST  advertised chunk size      (default 2000)
//   COLLAB_DIGEST_INTERVAL_MS   state digest period        (default 10s)
//   KEEP_ALIVE_URL           public URL to self-ping       (optional; falls
//                            back to RENDER_EXTERNAL_URL which Render sets)
//   KEEP_ALIVE_INTERVAL_MS   self-ping period              (default 10 min)
//   PORT                     provided automatically by Render

import express from "express";
import { createHash, randomBytes, timingSafeEqual } from "node:crypto";

const IS_PRODUCTION = process.env.NODE_ENV === "production";

const CONFIG = {
  maxPeers: boundedInt(process.env.COLLAB_MAX_PEERS, 2, 50, 10),
  maxRooms: boundedInt(process.env.COLLAB_MAX_ROOMS, 1, 1000, 100),
  maxObjects: boundedInt(process.env.COLLAB_MAX_OBJECTS, 100, 100000, 50000),
  maxRoomBytes: boundedInt(process.env.COLLAB_MAX_ROOM_BYTES, 1024 * 1024, 128 * 1024 * 1024, 32 * 1024 * 1024),
  maxSaveBytes: boundedInt(process.env.COLLAB_MAX_SAVE_BYTES, 1024, 256 * 1024, 64 * 1024),
  maxBody: process.env.COLLAB_MAX_BODY || "4mb",
  port: int(process.env.PORT, 10000),
  // How long a /api/poll request is held open before returning empty.
  longPollMs: 25000,
  // A client not seen (no poll/ops) for this long is dropped.
  clientTimeoutMs: 45000,
  // When the HOST times out (network blip, sleep, server hiccup) the room is
  // NOT closed immediately: it stays alive this long so the host can reclaim
  // it via create-room with the same code. Explicit leave/close still closes
  // the room at once.
  hostGraceMs: 90000,
  // Empty rooms are purged after this idle time.
  roomIdleMs: 5 * 60 * 1000,
  // Max ops a single client may send per second (anti-flood). Mass pastes of
  // prepared assets are legit traffic (thousands of adds at once), so this is
  // sized for them; the client paces itself against the value we advertise in
  // join_ok ("limits").
  maxOpsPerSec: boundedInt(process.env.COLLAB_MAX_OPS_PER_SEC, 10, 20000, 8000),
  maxOpBytesPerSec: boundedInt(process.env.COLLAB_MAX_OP_BYTES_PER_SEC, 64 * 1024, 64 * 1024 * 1024, 8 * 1024 * 1024),
  // Advertised per-request chunk size for v3 clients.
  maxOpsPerRequest: boundedInt(process.env.COLLAB_MAX_OPS_PER_REQUEST, 1, 5000, 2000),
  // State digest broadcast period: lets clients verify they hold exactly the
  // same objects as the room and auto-resync when they don't.
  digestMs: int(process.env.COLLAB_DIGEST_INTERVAL_MS, 10000),
  // Accepted ops are buffered per origin and broadcast on this cadence instead
  // of one fan-out per request, so bursts (mass paste / spam) don't saturate
  // peers. Repeated updates to the same object within the window collapse to
  // the latest. Higher = fewer/bigger frames, more latency.
  opCoalesceMs: int(process.env.COLLAB_OP_COALESCE_MS, 50),
  // Chat: max messages per client per 5s window, max text length.
  maxChatPer5s: 12,
  maxChatLen: 200,
  // Voice: max relay frames per client per second and max base64 payload per
  // frame. 4 frames/s of ~250ms mu-law 12kHz mono ≈ 4 KB raw / ~5.5 KB base64.
  maxVoicePerSec: 8,
  maxVoiceLen: 24000,
  // Per-client queue caps. Voice frames are dropped first (stale audio is
  // useless); the general cap protects memory against dead slow clients.
  maxQueuedVoice: 40,
  maxQueueLen: 6000,
  maxQueueBytes: boundedInt(process.env.COLLAB_MAX_QUEUE_BYTES, 512 * 1024, 32 * 1024 * 1024, 8 * 1024 * 1024),
  // Snapshot is sent in chunks of this many objects to avoid huge frames.
  snapshotChunkSize: 100,
  snapshotChunkBytes: 512 * 1024,
  // Self-ping target (Render injects RENDER_EXTERNAL_URL automatically).
  keepAliveUrl: process.env.KEEP_ALIVE_URL || process.env.RENDER_EXTERNAL_URL || "",
  // Render free spins down after ~15 min idle; ping well under that.
  keepAliveMs: int(process.env.KEEP_ALIVE_INTERVAL_MS, 10 * 60 * 1000),
};

function int(value, fallback) {
  const n = parseInt(value, 10);
  return Number.isFinite(n) ? n : fallback;
}

function boundedInt(value, min, max, fallback) {
  return Math.max(min, Math.min(max, int(value, fallback)));
}

function now() {
  return Date.now();
}

function newToken() {
  return randomBytes(32).toString("base64url");
}

function hashToken(value) {
  return createHash("sha256").update(String(value || ""), "utf8").digest();
}

function bearerToken(req) {
  const header = String(req.get("authorization") || "");
  return header.startsWith("Bearer ") ? header.slice(7).trim() : "";
}

function tokenMatches(req, expectedHash) {
  const token = bearerToken(req);
  return !!token && Buffer.isBuffer(expectedHash) && timingSafeEqual(hashToken(token), expectedHash);
}

function sanitizeText(value, max) {
  return String(value || "")
    .replace(/[\u0000-\u001f\u007f-\u009f<>]/g, "")
    .trim()
    .slice(0, max);
}

function validGid(value) {
  return typeof value === "string" && value.length <= 48 && /^[0-9]+:[0-9]+$/.test(value);
}

function validVersion(value) {
  return Number.isSafeInteger(value) && value >= 1 && value <= 2147483647;
}

function objectBytes(gid, save) {
  return Buffer.byteLength(gid, "utf8") + Buffer.byteLength(save, "utf8") + 64;
}

function roomLabel(code) {
  return createHash("sha256").update(code).digest("hex").slice(0, 10);
}

// Room codes are case-insensitive and separator-agnostic: hyphens/spaces are
// cosmetic, so a code typed as "PAIM-AB-CDE", "paimabcde" or "PAIM AB CDE" all
// resolve to the same room.
function normCode(value) {
  return String(value || "").toUpperCase().replace(/[^A-Z0-9]/g, "");
}

function log(...args) {
  console.log(`[collab ${new Date().toISOString()}]`, ...args);
}

// ---------------------------------------------------------------------------
// State digest: FNV-1a in two independent 32-bit lanes over "gid|version|save"
// per object, XOR-aggregated over the room. The client computes the identical
// value (objectSyncHash in CollabTypes.hpp) — any byte of divergence flips the
// digest and triggers an automatic resync on the client.
// ---------------------------------------------------------------------------

function fnv1a32(str, seed) {
  let h = seed >>> 0;
  for (let i = 0; i < str.length; i++) {
    h = Math.imul(h ^ (str.charCodeAt(i) & 0xff), 16777619) >>> 0;
  }
  return h >>> 0;
}

function objectSyncHash(gid, version, save) {
  const input = `${gid}|${version}|${save}`;
  return [fnv1a32(input, 0x811c9dc5), fnv1a32(input, 0xcbf29ce4)];
}

function hex8(n) {
  return (n >>> 0).toString(16).padStart(8, "0");
}

function roomDigest(room) {
  let a = 0;
  let b = 0;
  for (const [gid, rec] of room.objects) {
    if (!rec.hash) rec.hash = objectSyncHash(gid, rec.version, rec.save);
    a = (a ^ rec.hash[0]) >>> 0;
    b = (b ^ rec.hash[1]) >>> 0;
  }
  return hex8(a) + hex8(b);
}

function advertisedLimits() {
  return { maxOpsPerSec: CONFIG.maxOpsPerSec, maxOpsPerRequest: CONFIG.maxOpsPerRequest };
}

// ---------------------------------------------------------------------------
// Room model
// ---------------------------------------------------------------------------

class Room {
  constructor(code) {
    this.code = code;
    this.clients = new Map(); // clientId -> ClientCtx
    this.objects = new Map(); // gid -> { save, version, editor }
    this.tombstones = new Map(); // gid -> version of the delete that removed it
    // originClientId -> { by, ops:[], updateIdx: Map(gid->index) } buffered for
    // the next coalesced broadcast.
    this.pendingOps = new Map();
    this.seq = 0;
    this.bytes = 0;
    this.nextClientId = 1;
    this.hostClientId = 0;
    this.hostResumeHash = null;
    this.permissions = {
      allowSong: false,
      allowOptions: false,
      allowLevelSettings: false,
      // Non-hosts may view/tinker locally but cannot change the room level.
      viewOnly: false,
    };
    this.lastActivity = now();
    // Set when the host times out; the room waits hostGraceMs for a reclaim
    // (create-room with the same code) before closing for everyone.
    this.hostLostAt = 0;
  }

  isEmpty() {
    return this.clients.size === 0;
  }

  peerList() {
    // Hide clients marked for kick so the host UI updates immediately even if
    // the victim is still waiting to poll their "kicked" message.
    return [...this.clients.values()]
      .filter((c) => !c.kicked)
      .map((c) => ({
        clientId: c.clientId,
        username: c.username,
        isHost: c.clientId === this.hostClientId,
        accountID: c.accountID,
        iconID: c.iconID,
        iconType: c.iconType,
        color1: c.color1,
        color2: c.color2,
        glowColor: c.glowColor,
        glowEnabled: c.glowEnabled,
      }));
  }
}

const rooms = new Map();

// ---------------------------------------------------------------------------
// Presence: account-keyed registry so a host can invite friends who are online
// (mod running, logged in) but not currently inside any room. Independent of
// rooms — this is what makes Globed-style invites reach people anywhere.
// ---------------------------------------------------------------------------

const presence = new Map(); // accountID -> PresenceCtx

function makePresence(accountID, username) {
  const token = newToken();
  return { token, ctx: {
    accountID,
    username: sanitizeText(username || `player${accountID}`, 32),
    tokenHash: hashToken(token),
    queue: [],
    waiter: null,
    lastSeen: now(),
  }};
}

function enqueuePresence(p, msg) {
  if (p.queue.length >= 40) p.queue.shift();
  p.queue.push(msg);
  if (p.waiter && p.queue.length) {
    const { res, timer } = p.waiter;
    clearTimeout(timer);
    p.waiter = null;
    res.json({ messages: p.queue.splice(0) });
  }
}

function createRoom(code) {
  if (rooms.has(code) || rooms.size >= CONFIG.maxRooms) return null;
  const room = new Room(code);
  rooms.set(code, room);
  log(`room created: id=${roomLabel(code)}`);
  return room;
}

function destroyRoom(room) {
  rooms.delete(room.code);
  log(`room destroyed: id=${roomLabel(room.code)}`);
}

function closeRoom(room, reason) {
  log(`closing room: id=${roomLabel(room.code)} reason=${reason} peers=${room.clients.size}`);
  for (const c of room.clients.values()) {
    enqueue(c, { t: "room_closed", reason });
  }
  destroyRoom(room);
}

function makeClient(clientId, username, appearance = {}) {
  const token = newToken();
  return { token, ctx: {
    clientId,
    username: sanitizeText(username || `editor${clientId}`, 32),
    sessionHash: hashToken(token),
    // Display-only presence. This is intentionally not used to authorize any
    // room operation; clientId remains the sole room identity.
    accountID: boundedInt(appearance.accountID, 0, 2147483647, 0),
    iconID: boundedInt(appearance.iconID, 0, 100000, 0),
    iconType: boundedInt(appearance.iconType, 0, 8, 0),
    color1: boundedInt(appearance.color1, 0, 1000, 0),
    color2: boundedInt(appearance.color2, 0, 1000, 0),
    glowColor: boundedInt(appearance.glowColor, 0, 1000, 0),
    glowEnabled: Boolean(appearance.glowEnabled),
    queue: [],
    queuedBytes: 0,
    queuedVoice: 0,
    needsResync: false,
    waiter: null,
    lastSeen: now(),
    opWindowStart: now(),
    opCount: 0,
    opBytes: 0,
    chatWindowStart: now(),
    chatCount: 0,
    voiceWindowStart: now(),
    voiceCount: 0,
    selectWindowStart: now(),
    selectCount: 0,
    inviteWindowStart: now(),
    inviteCount: 0,
    kicked: false,
  }};
}

function authedRoomClient(req, roomCode, clientId) {
  const room = rooms.get(normCode(roomCode));
  const client = room ? room.clients.get(int(clientId, 0)) : null;
  if (!room || !client || !tokenMatches(req, client.sessionHash)) return { room: null, client: null };
  return { room, client };
}

// Sliding-window rate limit helper. Returns true when the client is allowed
// another `kind` action within `windowMs`.
function allowRate(client, kindPrefix, windowMs, max) {
  const t = now();
  if (t - client[kindPrefix + "WindowStart"] >= windowMs) {
    client[kindPrefix + "WindowStart"] = t;
    client[kindPrefix + "Count"] = 0;
  }
  client[kindPrefix + "Count"] += 1;
  return client[kindPrefix + "Count"] <= max;
}

// ---------------------------------------------------------------------------
// Per-client message queue + long-poll
// ---------------------------------------------------------------------------

function enqueue(client, msg) {
  const bytes = Buffer.byteLength(JSON.stringify(msg), "utf8");
  if (bytes > CONFIG.maxQueueBytes) {
    if (msg.t !== "voice") client.needsResync = true;
    return;
  }
  if (!Object.prototype.hasOwnProperty.call(msg, "_queueBytes")) {
    Object.defineProperty(msg, "_queueBytes", { value: bytes, enumerable: false });
  }
  // Voice frames are best-effort: a client that isn't draining its queue gets
  // stale audio dropped instead of an ever-growing backlog.
  if (msg.t === "voice") {
    if (client.queuedVoice >= CONFIG.maxQueuedVoice) return;
    client.queuedVoice += 1;
  }
  while (client.queue.length >= CONFIG.maxQueueLen || client.queuedBytes + bytes > CONFIG.maxQueueBytes) {
    // General backstop; drop oldest voice first, then oldest message.
    const idx = client.queue.findIndex((m) => m.t === "voice");
    if (idx >= 0) {
      const [dropped] = client.queue.splice(idx, 1);
      client.queuedBytes = Math.max(0, client.queuedBytes - (dropped?._queueBytes || 0));
      client.queuedVoice -= 1;
    } else {
      // Dropping object data desyncs the client permanently; flag it for an
      // automatic resync (handled on the coalesce timer) instead of losing
      // edits in silence.
      const dropped = client.queue.shift();
      client.queuedBytes = Math.max(0, client.queuedBytes - (dropped?._queueBytes || 0));
      if (dropped && (dropped.t === "op_batch" || dropped.t === "snapshot" || dropped.t === "resync")) {
        client.needsResync = true;
      }
    }
    if (client.queue.length === 0) break;
  }
  client.queue.push(msg);
  client.queuedBytes += bytes;
  flushWaiter(client);
}

function drainQueue(client) {
  client.queuedVoice = 0;
  client.queuedBytes = 0;
  return client.queue.splice(0);
}

function flushWaiter(client) {
  if (client.waiter && client.queue.length) {
    const { res, timer } = client.waiter;
    clearTimeout(timer);
    client.waiter = null;
    res.json({ messages: drainQueue(client) });
  }
}

function broadcast(room, msg, exceptClientId = 0) {
  for (const c of room.clients.values()) {
    if (c.clientId === exceptClientId) continue;
    enqueue(c, msg);
  }
}

function broadcastPeers(room) {
  broadcast(room, { t: "peers", peers: room.peerList() });
}

function sendSnapshot(client, room) {
  const chunks = [];
  let chunk = [];
  let bytes = 128;
  for (const [gid, rec] of room.objects) {
    const item = { gid, save: rec.save, version: rec.version };
    const itemBytes = objectBytes(gid, rec.save);
    if (chunk.length && (chunk.length >= CONFIG.snapshotChunkSize || bytes + itemBytes > CONFIG.snapshotChunkBytes)) {
      chunks.push(chunk);
      chunk = [];
      bytes = 128;
    }
    chunk.push(item);
    bytes += itemBytes;
  }
  if (chunk.length || chunks.length === 0) chunks.push(chunk);
  const chunkCount = chunks.length;
  for (let i = 0; i < chunkCount; i++) {
    enqueue(client, {
      t: "snapshot",
      chunkIndex: i,
      chunkCount,
      objects: chunks[i],
    });
  }
}

// ---------------------------------------------------------------------------
// Operation handling (Last-Write-Wins per object)
// ---------------------------------------------------------------------------

function tombstoneOp(room, gid, version) {
  room.tombstones.set(gid, version);
  // Keep the tombstone map bounded on churny sessions; recent tombstones (the
  // ones that actually matter for in-flight stale ops) are kept last.
  if (room.tombstones.size > CONFIG.maxObjects) {
    const drop = room.tombstones.size - CONFIG.maxObjects;
    const it = room.tombstones.keys();
    for (let i = 0; i < drop; i++) room.tombstones.delete(it.next().value);
  }
}

function applyOps(room, client, ops) {
  const accepted = [];
  for (const op of ops) {
    if (!op || !validGid(op.gid) || !["add", "update", "move", "delete"].includes(op.kind)) continue;
    if (!validVersion(op.version)) continue;
    const version = op.version;
    const existing = room.objects.get(op.gid);

    if (existing && version < existing.version) continue;
    if (existing && version === existing.version && existing.editor >= client.clientId) continue;

    // Reject ops that are stale relative to a delete (prevents deleted objects
    // resurrecting for new joiners when an older add/update arrives late).
    if (!existing) {
      const tomb = room.tombstones.get(op.gid);
      if (tomb !== undefined && version <= tomb) continue;
    }

    if (op.kind === "delete") {
      if (existing) {
        room.objects.delete(op.gid);
        room.bytes = Math.max(0, room.bytes - (existing.bytes || objectBytes(op.gid, existing.save)));
      }
      tombstoneOp(room, op.gid, version);
      accepted.push({ kind: "delete", gid: op.gid, version });
    } else {
      // "move" is an update with optional x/y for cheap remote apply; still
      // requires a full save string so digest/LWW stay correct.
      if (op.kind === "add" && !existing && room.objects.size >= CONFIG.maxObjects) continue;
      const save = typeof op.save === "string" ? op.save : "";
      const saveBytes = Buffer.byteLength(save, "utf8");
      if (!save || saveBytes > CONFIG.maxSaveBytes) continue;
      const bytes = objectBytes(op.gid, save);
      const oldBytes = existing ? (existing.bytes || objectBytes(op.gid, existing.save)) : 0;
      if (room.bytes - oldBytes + bytes > CONFIG.maxRoomBytes) continue;
      const kind = op.kind === "move" ? "move" : op.kind === "add" ? "add" : "update";
      const rec = {
        save,
        version,
        editor: client.clientId,
        hash: objectSyncHash(op.gid, version, save),
        bytes,
      };
      room.objects.set(op.gid, rec);
      room.bytes = room.bytes - oldBytes + bytes;
      room.tombstones.delete(op.gid); // resurrected by a newer op
      const out = { kind, gid: op.gid, version, save };
      if (op.kind === "move" && Number.isFinite(Number(op.x)) && Number.isFinite(Number(op.y))) {
        out.x = Number(op.x);
        out.y = Number(op.y);
      }
      accepted.push(out);
    }
  }
  return accepted;
}

// Buffer accepted ops per origin so a burst of requests collapses into a single
// broadcast. Repeated updates to the same gid keep only the latest; adds and
// deletes are structural and stay in arrival order.
function bufferAccepted(room, clientId, by, accepted) {
  let bucket = room.pendingOps.get(clientId);
  if (!bucket) {
    bucket = { by, ops: [], updateIdx: new Map() };
    room.pendingOps.set(clientId, bucket);
  }
  bucket.by = by;
  for (const op of accepted) {
    // Coalesce move+update on the same gid: only the latest state matters.
    if (op.kind === "update" || op.kind === "move") {
      const at = bucket.updateIdx.get(op.gid);
      if (at !== undefined && bucket.ops[at] &&
          (bucket.ops[at].kind === "update" || bucket.ops[at].kind === "move")) {
        bucket.ops[at] = op;
        continue;
      }
      bucket.updateIdx.set(op.gid, bucket.ops.length);
    } else {
      bucket.updateIdx.delete(op.gid);
    }
    bucket.ops.push(op);
  }
}

function flushPendingOps() {
  for (const room of rooms.values()) {
    if (room.pendingOps.size === 0) continue;
    for (const [origin, bucket] of room.pendingOps) {
      if (bucket.ops.length === 0) continue;
      const seq = ++room.seq;
      broadcast(room, { t: "op_batch", seq, origin, by: bucket.by, ops: bucket.ops }, origin);
    }
    room.pendingOps.clear();
  }
}

// A client whose queue overflowed lost object data: instead of leaving it
// silently desynced, wipe its stale queue and rebuild it from a fresh
// snapshot. The host is skipped (its copy is canonical; the digest check
// handles that case by re-seeding the room from the host).
function healOverflowedClients() {
  for (const room of rooms.values()) {
    for (const client of room.clients.values()) {
      if (!client.needsResync) continue;
      client.needsResync = false;
      if (client.clientId === room.hostClientId) continue;
      client.queue = client.queue.filter(
        (m) => m.t !== "op_batch" && m.t !== "snapshot" && m.t !== "resync"
      );
      client.queuedVoice = client.queue.reduce((n, m) => n + (m.t === "voice" ? 1 : 0), 0);
      client.queuedBytes = client.queue.reduce((n, m) => n + (m._queueBytes || 0), 0);
      enqueue(client, { t: "resync" });
      sendSnapshot(client, room);
      log(`queue overflow: auto-resync client=${client.clientId} room=${roomLabel(room.code)}`);
    }
  }
}

setInterval(() => {
  flushPendingOps();
  healOverflowedClients();
}, CONFIG.opCoalesceMs);

// Periodic state digest so every client can verify it mirrors the room
// exactly and self-heal when it doesn't.
setInterval(() => {
  for (const room of rooms.values()) {
    if (room.isEmpty()) continue;
    broadcast(room, { t: "digest", seq: room.seq, count: room.objects.size, hash: roomDigest(room) });
  }
}, CONFIG.digestMs);

// ---------------------------------------------------------------------------
// HTTP API
// ---------------------------------------------------------------------------

const app = express();
app.set("trust proxy", 1);
app.disable("x-powered-by");
app.use((_req, res, next) => {
  res.set({
    "Cache-Control": "no-store",
    "Content-Security-Policy": "default-src 'none'; frame-ancestors 'none'; base-uri 'none'",
    "Cross-Origin-Resource-Policy": "same-origin",
    "Referrer-Policy": "no-referrer",
    "X-Content-Type-Options": "nosniff",
    "X-Frame-Options": "DENY",
  });
  if (IS_PRODUCTION) res.set("Strict-Transport-Security", "max-age=31536000; includeSubDomains");
  next();
});
app.use("/api", (req, res, next) => {
  if (req.method === "POST" && !req.is("application/json")) {
    return res.status(415).json({ error: { code: "bad_content_type", message: "application/json required" } });
  }
  next();
});
app.use(express.json({ limit: CONFIG.maxBody }));
app.use((err, _req, res, next) => {
  if (!err) return next();
  const status = err.type === "entity.too.large" ? 413 : 400;
  return res.status(status).json({ error: { code: status === 413 ? "body_too_large" : "bad_json", message: "Invalid request body" } });
});

const ipWindows = new Map();
function ipRateLimit(name, windowMs, max) {
  return (req, res, next) => {
    const key = `${name}:${req.ip}`;
    const t = now();
    let entry = ipWindows.get(key);
    if (!entry || t - entry.start >= windowMs) entry = { start: t, count: 0 };
    entry.count++;
    ipWindows.set(key, entry);
    if (entry.count > max) {
      res.set("Retry-After", String(Math.ceil((windowMs - (t - entry.start)) / 1000)));
      return res.status(429).json({ error: { code: "rate_limited", message: "Too many requests" } });
    }
    next();
  };
}
const limitCreate = ipRateLimit("create", 60_000, 5);
const limitJoin = ipRateLimit("join", 60_000, 30);
const limitPresence = ipRateLimit("presence", 60_000, 20);

setInterval(() => {
  const cutoff = now() - 10 * 60 * 1000;
  for (const [key, entry] of ipWindows) if (entry.start < cutoff) ipWindows.delete(key);
}, 10 * 60 * 1000).unref();

app.get("/", (_req, res) => res.json({ ok: true, service: "paimon-collab" }));

app.get("/health", (_req, res) => {
  res.json({ ok: true });
});

app.post("/api/create-room", limitCreate, (req, res) => {
  const { roomCode, username, initialObjects, resumeToken, protocol } = req.body || {};
  if (int(protocol, 0) < 6) {
    return res.status(426).json({ error: { code: "upgrade_required", message: "Client update required" } });
  }
  const code = normCode(roomCode);
  if (code.length < 10 || code.length > 32) {
    return res.status(400).json({ error: { code: "bad_room", message: "Invalid room code" } });
  }
  const room = createRoom(code);
  if (!room) {
    if (!rooms.has(code)) {
      return res.status(503).json({ error: { code: "server_full", message: "Room capacity reached" } });
    }
    // The code is taken. If its host slot is vacant (host timed out and the
    // grace window is open), the caller reclaims the room as the new host:
    // the objects are wiped (the host re-seeds from their editor) and the
    // remaining peers rebuild via resync. This is what lets a host survive a
    // network blip without the room closing for everyone.
    const existing = rooms.get(code);
    if (existing && !existing.clients.has(existing.hostClientId) &&
        typeof resumeToken === "string" && resumeToken.length >= 32 &&
        timingSafeEqual(hashToken(resumeToken), existing.hostResumeHash || hashToken("invalid"))) {
      const clientId = existing.nextClientId++;
      const made = makeClient(clientId, username, req.body);
      const client = made.ctx;
      existing.clients.set(clientId, client);
      existing.hostClientId = clientId;
      existing.hostLostAt = 0;
      existing.objects.clear();
      existing.bytes = 0;
      existing.tombstones.clear();
      existing.pendingOps.clear();
      existing.seq++;
      existing.lastActivity = now();
      broadcast(existing, { t: "resync" }, clientId);
      res.json({
        clientId,
        sessionToken: made.token,
        isHost: true,
        seq: existing.seq,
        permissions: existing.permissions,
        maxObjects: CONFIG.maxObjects,
        limits: advertisedLimits(),
        peers: existing.peerList(),
      });
      broadcastPeers(existing);
      log(`host reclaim: room=${roomLabel(code)} client=${clientId} (${client.username}) peers=${existing.clients.size}`);
      return;
    }
    return res.status(409).json({ error: { code: "room_exists", message: "Room already exists" } });
  }

  if (Array.isArray(initialObjects)) {
    for (const obj of initialObjects) {
      if (!obj || !validGid(obj.gid) || typeof obj.save !== "string" || !validVersion(obj.version)) continue;
      if (room.objects.size >= CONFIG.maxObjects) break;
      if (Buffer.byteLength(obj.save, "utf8") > CONFIG.maxSaveBytes) continue;
      const version = obj.version;
      const bytes = objectBytes(obj.gid, obj.save);
      if (room.bytes + bytes > CONFIG.maxRoomBytes) break;
      room.objects.set(obj.gid, {
        save: obj.save,
        version,
        editor: 0,
        hash: objectSyncHash(obj.gid, version, obj.save),
        bytes,
      });
      room.bytes += bytes;
    }
  }
  const clientId = room.nextClientId++;
  const made = makeClient(clientId, username, req.body);
  const client = made.ctx;
  const hostResumeToken = newToken();
  room.hostResumeHash = hashToken(hostResumeToken);
  room.clients.set(clientId, client);
  room.hostClientId = clientId;
  room.lastActivity = now();

  res.json({
    clientId,
    sessionToken: made.token,
    resumeToken: hostResumeToken,
    isHost: true,
    seq: room.seq,
    permissions: room.permissions,
    maxObjects: CONFIG.maxObjects,
    limits: advertisedLimits(),
    peers: room.peerList(),
  });

  // The host already has the objects locally; just announce the peer list.
  broadcastPeers(room);
  log(`host create: room=${roomLabel(code)} client=${clientId} (${client.username}) seeded=${room.objects.size}`);
});

app.post("/api/join", limitJoin, (req, res) => {
  const { roomCode, username, protocol } = req.body || {};
  if (int(protocol, 0) < 6) {
    return res.status(426).json({ error: { code: "upgrade_required", message: "Client update required" } });
  }
  const code = normCode(roomCode);
  if (code.length < 10 || code.length > 32) {
    return res.status(400).json({ error: { code: "bad_room", message: "Invalid room code" } });
  }
  const room = rooms.get(code);
  if (!room) {
    return res.status(404).json({ error: { code: "room_not_found", message: "Room does not exist" } });
  }
  if (room.clients.size >= CONFIG.maxPeers) {
    return res.status(403).json({ error: { code: "room_full", message: "Room is full" } });
  }

  const clientId = room.nextClientId++;
  const made = makeClient(clientId, username, req.body);
  const client = made.ctx;
  room.clients.set(clientId, client);
  room.lastActivity = now();

  res.json({
    clientId,
    sessionToken: made.token,
    isHost: false,
    seq: room.seq,
    permissions: room.permissions,
    maxObjects: CONFIG.maxObjects,
    limits: advertisedLimits(),
    peers: room.peerList(),
  });

  sendSnapshot(client, room);
  broadcastPeers(room);
  log(`join: room=${roomLabel(code)} client=${clientId} (${client.username}) peers=${room.clients.size}`);
});

app.get("/api/poll", (req, res) => {
  const { room, client } = authedRoomClient(req, req.query.room, req.query.client);
  if (!room || !client) {
    return res.json({ messages: [{ t: "error", code: "not_joined", message: "Rejoin required" }] });
  }
  client.lastSeen = now();
  room.lastActivity = now();

  // Pending kick: deliver the notice and drop the client so they don't get a
  // bare not_joined (which would trigger reconnect recovery on the client).
  if (client.kicked) {
    const messages = drainQueue(client);
    if (!messages.some((m) => m.t === "kicked")) {
      messages.unshift({ t: "kicked", reason: "host_kicked" });
    }
    dropClient(room, client);
    return res.json({ messages });
  }

  if (client.queue.length) {
    return res.json({ messages: drainQueue(client) });
  }

  // A newer poll supersedes any request already parked: answer the old one
  // (empty) instead of leaving it hanging until its HTTP timeout.
  if (client.waiter) {
    const { res: oldRes, timer: oldTimer } = client.waiter;
    clearTimeout(oldTimer);
    client.waiter = null;
    try { oldRes.json({ messages: [] }); } catch { /* ignore */ }
  }

  const timer = setTimeout(() => {
    if (client.waiter && client.waiter.res === res) {
      client.waiter = null;
      res.json({ messages: [] });
    }
  }, CONFIG.longPollMs);
  client.waiter = { res, timer };

  req.on("close", () => {
    if (client.waiter && client.waiter.res === res) {
      clearTimeout(timer);
      client.waiter = null;
    }
  });
});

app.post("/api/ops", (req, res) => {
  const { room: rc, client: cid, ops } = req.body || {};
  const { room, client } = authedRoomClient(req, rc, cid);
  if (!room || !client) {
    return res.status(409).json({ error: { code: "not_joined", message: "Rejoin required" } });
  }
  client.lastSeen = now();
  room.lastActivity = now();
  if (!Array.isArray(ops) || ops.length === 0) return res.json({ seq: room.seq, count: 0 });
  if (ops.length > CONFIG.maxOpsPerRequest) {
    return res.status(413).json({ error: { code: "too_many_ops", message: "Operation batch too large" } });
  }
  // View-only guests cannot mutate room state (host is always allowed).
  if (client.clientId !== room.hostClientId && room.permissions.viewOnly) {
    return res.json({ seq: room.seq, count: 0, viewOnly: true });
  }

  const t = now();
  if (t - client.opWindowStart >= 1000) {
    client.opWindowStart = t;
    client.opCount = 0;
    client.opBytes = 0;
  }
  client.opCount += ops.length;
  client.opBytes += Buffer.byteLength(JSON.stringify(ops), "utf8");
  if (client.opCount > CONFIG.maxOpsPerSec || client.opBytes > CONFIG.maxOpBytesPerSec) {
    return res.status(429).json({ error: { code: "rate_limited", message: "Too many operations" } });
  }

  const accepted = applyOps(room, client, ops);
  if (accepted.length > 0) {
    // Log at receive time so the timestamp reflects when the op actually
    // arrived (not the coalesced broadcast ~opCoalesceMs later).
    let adds = 0, updates = 0, deletes = 0;
    for (const op of accepted) {
      if (op.kind === "delete") deletes++;
      else if (op.kind === "update") updates++;
      else adds++;
    }
    log(
      `ops recv: room=${roomLabel(room.code)} by=${client.username}#${client.clientId} +${adds} ~${updates} -${deletes} objects=${room.objects.size}`
    );
    // Queue for the next coalesced flush instead of broadcasting per request.
    bufferAccepted(room, client.clientId, client.username, accepted);
  }
  res.json({ seq: room.seq, count: accepted.length });
});

app.post("/api/chat", (req, res) => {
  const { room: rc, client: cid, text } = req.body || {};
  const { room, client } = authedRoomClient(req, rc, cid);
  if (!room || !client) {
    return res.status(409).json({ error: { code: "not_joined", message: "Rejoin required" } });
  }
  client.lastSeen = now();
  room.lastActivity = now();

  const msg = sanitizeText(text, CONFIG.maxChatLen);
  if (!msg) return res.json({ ok: true });
  if (!allowRate(client, "chat", 5000, CONFIG.maxChatPer5s)) {
    return res.status(429).json({ error: { code: "rate_limited", message: "Too many messages" } });
  }

  // Broadcast to everyone including the sender: the client renders its own
  // message through the same path, so ordering matches for all peers.
  broadcast(room, { t: "chat", from: client.clientId, name: client.username, text: msg });
  res.json({ ok: true });
});

app.post("/api/voice", (req, res) => {
  const { room: rc, client: cid, seq, data } = req.body || {};
  const { room, client } = authedRoomClient(req, rc, cid);
  if (!room || !client) {
    return res.status(409).json({ error: { code: "not_joined", message: "Rejoin required" } });
  }
  client.lastSeen = now();
  room.lastActivity = now();

  const payload = typeof data === "string" ? data : "";
  if (!payload || payload.length > CONFIG.maxVoiceLen || !/^[A-Za-z0-9+/_=-]+$/.test(payload)) return res.json({ ok: true });
  if (!allowRate(client, "voice", 1000, CONFIG.maxVoicePerSec)) {
    // Silently accept (voice is best-effort; erroring would spam the client).
    return res.json({ ok: true });
  }

  broadcast(
    room,
    { t: "voice", from: client.clientId, name: client.username, seq: int(seq, 0), data: payload },
    client.clientId
  );
  res.json({ ok: true });
});

// Ephemeral peer-selection presence (alk-style draw-selection-overlay).
// Not part of the room's LWW object state — fire-and-forget, rate limited.
app.post("/api/select", (req, res) => {
  const { room: rc, client: cid, rects } = req.body || {};
  const { room, client } = authedRoomClient(req, rc, cid);
  if (!room || !client) {
    return res.status(409).json({ error: { code: "not_joined", message: "Rejoin required" } });
  }
  client.lastSeen = now();
  room.lastActivity = now();

  // ~10 updates/s is plenty for selection presence.
  if (!allowRate(client, "select", 1000, 12)) {
    return res.json({ ok: true });
  }

  const out = [];
  if (Array.isArray(rects)) {
    for (const r of rects) {
      if (!r || typeof r !== "object") continue;
      const x = Number(r.x);
      const y = Number(r.y);
      const w = Number(r.w);
      const h = Number(r.h);
      if (![x, y, w, h].every(Number.isFinite)) continue;
      if (w <= 0 || h <= 0 || w > 1e6 || h > 1e6) continue;
      out.push({ x, y, w, h });
      if (out.length >= 64) break;
    }
  }

  broadcast(
    room,
    { t: "select", from: client.clientId, name: client.username, rects: out },
    client.clientId
  );
  res.json({ ok: true });
});

app.post("/api/perms", (req, res) => {
  const { room: rc, client: cid, permissions } = req.body || {};
  const { room, client } = authedRoomClient(req, rc, cid);
  if (!room || !client) return res.status(409).json({ error: { code: "not_joined" } });
  client.lastSeen = now();
  if (client.clientId !== room.hostClientId) {
    return res.status(403).json({ error: { code: "not_host", message: "Only the host can change permissions" } });
  }
  room.lastActivity = now();
  if (permissions) {
    room.permissions = {
      allowSong: !!permissions.allowSong,
      allowOptions: !!permissions.allowOptions,
      allowLevelSettings: !!permissions.allowLevelSettings,
      viewOnly: !!permissions.viewOnly,
    };
    broadcast(room, { t: "perms", permissions: room.permissions });
    log(`perms: room=${roomLabel(room.code)} viewOnly=${room.permissions.viewOnly}`);
  }
  res.json({ ok: true });
});

// Host-only: remove a peer from the room without closing it.
app.post("/api/kick", (req, res) => {
  const { room: rc, client: cid, target } = req.body || {};
  const { room, client } = authedRoomClient(req, rc, cid);
  if (!room || !client) {
    return res.status(409).json({ error: { code: "not_joined", message: "Rejoin required" } });
  }
  if (client.clientId !== room.hostClientId) {
    return res.status(403).json({ error: { code: "not_host", message: "Only the host can kick" } });
  }
  client.lastSeen = now();
  room.lastActivity = now();

  const targetId = int(target, 0);
  if (targetId <= 0 || targetId === client.clientId) {
    return res.status(400).json({ error: { code: "bad_target", message: "Invalid kick target" } });
  }
  if (targetId === room.hostClientId) {
    return res.status(400).json({ error: { code: "bad_target", message: "Cannot kick the host" } });
  }
  const victim = room.clients.get(targetId);
  if (!victim) {
    return res.status(404).json({ error: { code: "not_found", message: "Peer not in room" } });
  }

  // Mark as kicked so the next poll delivers "kicked" (not a bare not_joined,
  // which would make the client try session recovery). If a long-poll is
  // parked, flush it immediately.
  victim.kicked = true;
  enqueue(victim, {
    t: "kicked",
    reason: "host_kicked",
    by: client.username,
  });
  if (victim.waiter) {
    clearTimeout(victim.waiter.timer);
    try {
      victim.waiter.res.json({ messages: drainQueue(victim) });
    } catch {
      /* ignore */
    }
    victim.waiter = null;
    // Already delivered — drop now so they vanish from the peer list.
    dropClient(room, victim);
  } else {
    // No waiter: keep the slot briefly so the next poll can pick up "kicked".
    // Hide them from everyone else right away.
    broadcastPeers(room);
    setTimeout(() => {
      if (room.clients.get(targetId) === victim) dropClient(room, victim);
    }, 8000);
  }
  log(`kick: room=${roomLabel(room.code)} host=${client.clientId} -> ${targetId} (${victim.username})`);
  res.json({ ok: true });
});

app.post("/api/close-room", (req, res) => {
  const { room: rc, client: cid } = req.body || {};
  const { room, client } = authedRoomClient(req, rc, cid);
  if (!room || !client) return res.status(409).json({ error: { code: "not_joined" } });
  if (client.clientId !== room.hostClientId) {
    return res.status(403).json({ error: { code: "not_host", message: "Only the host can close the room" } });
  }
  // Remove the host from the broadcast list — they get the result via the HTTP
  // response and don't need their own "room_closed" message.
  room.clients.delete(client.clientId);
  if (client.waiter) {
    clearTimeout(client.waiter.timer);
    try { client.waiter.res.json({ messages: drainQueue(client) }); } catch { /* ignore */ }
    client.waiter = null;
  }
  closeRoom(room, "host_closed");
  res.json({ ok: true });
});

app.post("/api/resync", (req, res) => {
  const { room: rc, client: cid } = req.body || {};
  const { room, client } = authedRoomClient(req, rc, cid);
  if (!room || !client) {
    return res.status(409).json({ error: { code: "not_joined", message: "Rejoin required" } });
  }
  client.lastSeen = now();
  room.lastActivity = now();

  // A peer re-entering just needs the current state again.
  if (client.clientId !== room.hostClientId) {
    sendSnapshot(client, room);
    return res.json({ ok: true, role: "peer" });
  }

  // Host re-entered its editor: the level was reloaded from scratch, so wipe
  // the room and let the host re-seed it. Peers wipe locally (resync) and
  // rebuild from the re-sent ops.
  room.objects.clear();
  room.bytes = 0;
  room.tombstones.clear();
  room.pendingOps.clear();
  room.seq++;
  broadcast(room, { t: "resync" }, client.clientId);
  enqueue(client, { t: "resync_ready" });
  log(`resync: room=${roomLabel(room.code)} host=${client.clientId} cleared`);
  res.json({ ok: true, role: "host" });
});

app.post("/api/leave", (req, res) => {
  const { room: rc, client: cid } = req.body || {};
  const { room, client } = authedRoomClient(req, rc, cid);
  if (room && client) dropClient(room, client);
  if (!room || !client) return res.status(401).json({ error: { code: "not_joined" } });
  res.json({ ok: true });
});

// ---------------------------------------------------------------------------
// Presence + invites
// ---------------------------------------------------------------------------

app.post("/api/presence/register", limitPresence, (req, res) => {
  const { accountID, username } = req.body || {};
  const id = int(accountID, 0);
  if (id <= 0) return res.status(400).json({ error: { code: "bad_account", message: "Invalid account" } });
  let p = presence.get(id);
  if (!p) {
    const made = makePresence(id, username);
    p = made.ctx;
    presence.set(id, p);
    log(`presence register: account=${id} (${p.username}) online=${presence.size}`);
    return res.json({ ok: true, presenceToken: made.token });
  } else if (!tokenMatches(req, p.tokenHash)) {
    return res.status(409).json({ error: { code: "already_online", message: "Account presence already registered" } });
  } else if (username) {
    p.username = sanitizeText(username, 32);
  }
  p.lastSeen = now();
  res.json({ ok: true });
});

app.get("/api/presence/poll", (req, res) => {
  const id = int(req.query.account, 0);
  const p = presence.get(id);
  // Tell an unknown client to re-register (server restarted / presence purged).
  if (!p) return res.json({ messages: [{ t: "error", code: "not_registered" }] });
  if (!tokenMatches(req, p.tokenHash)) {
    return res.status(401).json({ messages: [{ t: "error", code: "not_registered" }] });
  }
  p.lastSeen = now();

  if (p.queue.length) return res.json({ messages: p.queue.splice(0) });

  // Same supersede rule as the room poll: never leave an old request hanging.
  if (p.waiter) {
    const { res: oldRes, timer: oldTimer } = p.waiter;
    clearTimeout(oldTimer);
    p.waiter = null;
    try { oldRes.json({ messages: [] }); } catch { /* ignore */ }
  }

  const timer = setTimeout(() => {
    if (p.waiter && p.waiter.res === res) {
      p.waiter = null;
      res.json({ messages: [] });
    }
  }, CONFIG.longPollMs);
  p.waiter = { res, timer };

  req.on("close", () => {
    if (p.waiter && p.waiter.res === res) {
      clearTimeout(timer);
      p.waiter = null;
    }
  });
});

app.post("/api/presence/leave", (req, res) => {
  const id = int((req.body || {}).accountID, 0);
  const p = presence.get(id);
  if (p && tokenMatches(req, p.tokenHash)) {
    if (p.waiter) {
      clearTimeout(p.waiter.timer);
      try { p.waiter.res.json({ messages: [] }); } catch { /* ignore */ }
    }
    presence.delete(id);
  }
  if (p && !tokenMatches(req, p.tokenHash)) return res.status(401).json({ error: { code: "not_registered" } });
  res.json({ ok: true });
});

// Host invites an account to their room. The invite lands in the target's
// presence queue if they're online; the caller learns whether it was delivered.
app.post("/api/invite", (req, res) => {
  const { room: rc, client: cid, account } = req.body || {};
  const { room, client } = authedRoomClient(req, rc, cid);
  if (!room || !client) {
    return res.status(409).json({ error: { code: "not_joined", message: "Rejoin required" } });
  }
  if (client.clientId !== room.hostClientId) {
    return res.status(403).json({ error: { code: "not_host", message: "Only the host can invite" } });
  }
  client.lastSeen = now();
  if (!allowRate(client, "invite", 60_000, 10)) {
    return res.status(429).json({ error: { code: "rate_limited", message: "Too many invitations" } });
  }

  const target = int(account, 0);
  if (target <= 0) return res.status(400).json({ error: { code: "bad_account", message: "Invalid account" } });

  const p = presence.get(target);
  if (!p) return res.json({ ok: true, online: false });

  enqueuePresence(p, {
    t: "invite",
    room: room.code,
    fromName: client.username,
  });
  log(`invite: room=${roomLabel(room.code)} from=${client.username}#${client.clientId} -> account=${target}`);
  res.json({ ok: true, online: true });
});

function dropClient(room, client, { hostGrace = false } = {}) {
  if (client.waiter) {
    clearTimeout(client.waiter.timer);
    try {
      client.waiter.res.json({ messages: [] });
    } catch {
      /* ignore */
    }
    client.waiter = null;
  }
  room.clients.delete(client.clientId);
  room.pendingOps.delete(client.clientId);
  log(`leave: room=${roomLabel(room.code)} client=${client.clientId} peers=${room.clients.size}`);

  // Host leaving closes the room for everyone — peers exit the editor. A host
  // TIMEOUT (hostGrace) opens a reclaim window instead so a network blip or a
  // server hiccup doesn't kill the session for the whole room.
  if (client.clientId === room.hostClientId) {
    if (hostGrace) {
      room.hostLostAt = now();
      room.lastActivity = now();
      log(`host lost: room=${roomLabel(room.code)} waiting ${CONFIG.hostGraceMs}ms for reclaim`);
      if (!room.isEmpty()) broadcastPeers(room);
      return;
    }
    closeRoom(room, "host_left");
    return;
  }
  room.lastActivity = now();
  if (!room.isEmpty()) broadcastPeers(room);
}

// ---------------------------------------------------------------------------
// Cleanup: dead clients + idle rooms
// ---------------------------------------------------------------------------

setInterval(() => {
  const t = now();
  for (const room of [...rooms.values()]) {
    for (const client of [...room.clients.values()]) {
      if (t - client.lastSeen > CONFIG.clientTimeoutMs) dropClient(room, client, { hostGrace: true });
    }
    if (!rooms.has(room.code)) continue;
    // Host slot vacated by a timeout: close if nobody reclaimed it in time.
    if (room.hostLostAt && !room.clients.has(room.hostClientId) && t - room.hostLostAt > CONFIG.hostGraceMs) {
      closeRoom(room, "host_left");
      continue;
    }
    if (room.isEmpty() && t - room.lastActivity > CONFIG.roomIdleMs) {
      destroyRoom(room);
    }
  }
  // Drop stale presence (a poll refreshes lastSeen every ~longPollMs).
  for (const [id, p] of [...presence.entries()]) {
    if (t - p.lastSeen > CONFIG.clientTimeoutMs) presence.delete(id);
  }
}, 5000);

// Self-ping keep-alive. Useful on Render's free tier (spins down after ~15 min
// idle). This only keeps a running instance warm; it can't wake one that's
// already asleep — set up an external uptime monitor for full coverage.
async function keepAlive() {
  const base = CONFIG.keepAliveUrl.replace(/\/$/, "");
  try {
    const r = await fetch(`${base}/health`);
    log(`keep-alive ${base}/health -> ${r.status}`);
  } catch (err) {
    log(`keep-alive failed: ${err.message}`);
  }
}
if (CONFIG.keepAliveUrl) {
  setTimeout(keepAlive, 30 * 1000);
  setInterval(keepAlive, CONFIG.keepAliveMs);
}

const server = app.listen(CONFIG.port, () => {
  log(`listening on :${CONFIG.port}`);
  if (CONFIG.keepAliveUrl) {
    log(`keep-alive enabled: ${CONFIG.keepAliveUrl} every ${CONFIG.keepAliveMs}ms`);
  } else {
    log(`keep-alive disabled (set KEEP_ALIVE_URL or rely on RENDER_EXTERNAL_URL)`);
  }
});
server.requestTimeout = 40_000;
server.headersTimeout = 10_000;
server.keepAliveTimeout = 5_000;
server.maxRequestsPerSocket = 1000;
