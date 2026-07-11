# Paimon Collab — Render server

Real-time collaborative Geometry Dash editor backend for the Paimbnails mod.

A persistent Node.js process keeps authoritative room state in memory. It orders
every operation with a global sequence number, resolves conflicts with
Last-Write-Wins per object, and fans changes out to each client. Transport is
**HTTP long-polling** (the mod's WebSocket option is blocked — cocos2d's
WebSocket header is not shipped to Geode mods — so the client uses the same
proven `web::WebRequest` HTTP path the rest of the mod uses).

## Why Render (vs Cloudflare)

Render runs a long-lived Node process, so in-memory room state and long-poll
fan-out work without Durable Objects or a paid plan. The free tier sleeps after
~15 min of inactivity (cold start ~50s); rooms reset on restart, which is fine
for ad-hoc collab sessions.

## Run locally

Requires Node.js 24 LTS:

```powershell
cd collab-editor-render
npm ci
npm run dev
# In another shell:
npm test
```

Server listens on `http://localhost:10000`.

## Deploy to Render

1. Push the repo (this folder included) to GitHub.
2. Render → **New → Blueprint**, pick the repo. `render.yaml` provisions the
   service automatically. (Or **New → Web Service**, Root Directory
   `collab-editor-render`, build `npm ci --omit=dev`, start `npm start`.)
3. After deploy you get a URL like `https://paimon-collab.onrender.com`.
   Set `kServerBaseUrl` in `CollabTypes.hpp` to that HTTPS origin and rebuild
   the mod. Do not allow plaintext HTTP for a public deployment.

## Environment variables

| Var | Default | Meaning |
|---|---|---|
| `COLLAB_MAX_PEERS` | `10` | Max editors per room |
| `COLLAB_MAX_ROOMS` | `100` | Max live rooms across the process |
| `COLLAB_MAX_OBJECTS` | `50000` | Max objects stored per room |
| `COLLAB_MAX_ROOM_BYTES` | `33554432` | Max serialized object bytes per room |
| `COLLAB_MAX_SAVE_BYTES` | `65536` | Max serialized bytes for one object |
| `COLLAB_MAX_QUEUE_BYTES` | `8388608` | Max queued outbound bytes per client |
| `COLLAB_MAX_OP_BYTES_PER_SEC` | `8388608` | Per-client operation bandwidth cap |
| `COLLAB_MAX_BODY` | `4mb` | Max request body size |
| `KEEP_ALIVE_URL` | `RENDER_EXTERNAL_URL` | Public URL to self-ping; empty disables |
| `KEEP_ALIVE_INTERVAL_MS` | `600000` | Self-ping period (10 min) |
| `PORT` | provided by Render | HTTP port |

## Room model: host-owned private rooms

- A host calls `POST /api/create-room` with the level's current objects as
  `initialObjects`. The response includes a random `sessionToken` and a
  separate host-only `resumeToken`.
- Peers call `POST /api/join` with the same `roomCode`. The server rejects
  unknown rooms with `room_not_found` (no auto-create).
- New peers receive the current state via `snapshot` chunks; live edits arrive
  as `op_batch` messages.
- When the host calls `POST /api/close-room` (or simply leaves / disconnects),
  the server emits `room_closed` to every peer and destroys the room. Peers
  exit the editor; the host's local level is the canonical final state.

## Keep-alive

Render free spins services down after ~15 min idle. The server self-pings
`/health` every `KEEP_ALIVE_INTERVAL_MS` using `KEEP_ALIVE_URL` (which falls
back to `RENDER_EXTERNAL_URL`, injected automatically by Render). This keeps a
running instance warm but **can't wake one that's already asleep** — for full
"always on" coverage, also set up an external uptime monitor (Uptime Robot,
cron-job.org, etc.) hitting `/health` every 5 minutes.

## HTTP API

- `GET /` / `GET /health` — service info / health check (also used by the
  self-ping and the Render health probe).
- `POST /api/create-room` — `{ roomCode, username, protocol: 6, initialObjects }`
  → `{ clientId, sessionToken, resumeToken, isHost: true, ... }`. Fails with
  `room_exists` (409) if the code is taken. `initialObjects` is an array of
  `{ gid, save, version }` seeding the room state.
  Clients may also send `accountID`, `iconID`, `iconType`, `color1`,
  `color2`, `glowColor`, and `glowEnabled`. They are display-only presence
  data used by the in-game room list; they never grant room permissions.
- `POST /api/join` — `{ roomCode, username, protocol: 6 }` →
  `{ clientId, sessionToken, isHost: false, ... }`. Fails with
  `room_not_found` (404) when the room doesn't exist. Side effect: queues a
  snapshot + peer list for the new client.
- `GET /api/poll?room=&client=` — long-poll (held up to 25s) →
  `{ messages: [ ... ] }`. Re-call immediately after each response.
- `POST /api/ops` — `{ room, client, ops: [ { kind, gid, version, save? } ] }`
  → `{ seq, count }`. Applies LWW and queues `op_batch` for other clients.
- `POST /api/perms` — host only — `{ room, client, permissions }`.
- `POST /api/close-room` — host only — `{ room, client }`. Destroys the room
  and notifies every peer with `room_closed`.
- `POST /api/leave` — `{ room, client }`. If `client` is the host, this closes
  the room for everyone.

Every room endpoint after create/join requires
`Authorization: Bearer <sessionToken>`. A `clientId` is public identity only
and never authorizes an action. Host reclaim additionally requires the
separate `resumeToken`. Tokens and full room codes are not written to logs.

### Server → client messages (delivered through `/api/poll`)

```jsonc
{ "t": "snapshot", "chunkIndex": 0, "chunkCount": 1,
  "objects": [ { "gid": "1:1", "save": "...", "version": 1 } ] }
{ "t": "op_batch", "seq": 43, "origin": 5,
  "ops": [ { "kind": "add|update|delete", "gid": "1:1", "version": 2, "save": "..." } ] }
{ "t": "peers", "peers": [ { "clientId": 1, "username": "A", "isHost": true } ] }
// New clients also receive accountID, iconID, iconType, color1, color2,
// glowColor, and glowEnabled on every peer object.
{ "t": "perms", "permissions": { "allowSong": false, ... } }
{ "t": "room_closed", "reason": "host_left|host_closed" }
{ "t": "error", "code": "not_joined", "message": "..." }
```

## Conflict resolution (Last-Write-Wins per object)

Each object has a `version` counter. The server only accepts an op whose
`version` is `>=` the stored one; ties break by higher `clientId`. A global
`seq` is assigned to each accepted batch so clients apply remote ops in order
and ignore stale ones.

## Global object IDs

`gid = "<clientId>:<localSeq>"`. GD's `m_uniqueID` is per-session and not stable
across machines, so the mod maps `gid <-> GameObject*` locally and never uses
raw `m_uniqueID` for network identity.

## Security notes / limits

- Rooms live in memory only; a restart (or free-tier sleep) clears them.
- Handshakes are rate-limited per source IP; rooms, bodies, object saves,
  operation bandwidth, and outbound queues have independent memory caps.
- The Collab Editor has no global access gate: anyone with the updated mod can
  create a room or join one when they know its random room code.
- IDs, versions, text, voice payloads, selection rectangles, and operation
  kinds are validated server-side. Client checks are UX only.
- Room secrecy depends on sharing room codes only with intended collaborators.
  New clients generate 60-bit random room codes.
- Presence account IDs are self-asserted display data, not proof of Geometry
  Dash account ownership. Never use them for authorization or moderation.
- TLS protects traffic in transit, but chat, voice, and level data are not
  end-to-end encrypted; the server process can inspect relayed content.
- Application limits reduce abuse, but volumetric DDoS still requires the
  hosting provider or an upstream protection service.
- State remains in one process. Do not scale above one instance without moving
  rooms, tokens, limits, and queues to an atomic shared store such as Redis.
