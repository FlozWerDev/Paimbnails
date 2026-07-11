// End-to-end check for the HTTP long-poll collab server.
// Host A creates a private room seeded with one object; B joins and receives
// it via snapshot; A adds another object, B gets it via op_batch; late joiner
// C also gets both via snapshot; joining a non-existent room is rejected; the
// host leaving closes the room (other peers get room_closed).
const BASE = "http://localhost:10000";
const sessions = new Map();
const roomName = (value) => {
  const raw = String(value || "").toUpperCase().replace(/[^A-Z0-9]/g, "");
  return raw.length >= 10 ? raw : `PAIM${raw.padEnd(8, "X")}`;
};
const sessionKey = (room, client) => `${roomName(room)}:${client}`;

const post = async (path, body) => {
  const payload = { ...body };
  if (payload.roomCode) payload.roomCode = roomName(payload.roomCode);
  if (payload.room) payload.room = roomName(payload.room);
  if (path === "/api/create-room" || path === "/api/join") payload.protocol = 6;
  const token = payload.room && payload.client ? sessions.get(sessionKey(payload.room, payload.client)) : "";
  const r = await fetch(BASE + path, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      ...(token ? { Authorization: `Bearer ${token}` } : {}),
    },
    body: JSON.stringify(payload),
  });
  const json = await r.json();
  if (r.status === 200 && json.clientId && json.sessionToken && payload.roomCode) {
    sessions.set(sessionKey(payload.roomCode, json.clientId), json.sessionToken);
  }
  return { status: r.status, json };
};
const poll = async (room, client) => {
  const normalized = roomName(room);
  const token = sessions.get(sessionKey(normalized, client));
  const r = await fetch(`${BASE}/api/poll?room=${normalized}&client=${client}`, {
    headers: token ? { Authorization: `Bearer ${token}` } : {},
  });
  return (await r.json()).messages;
};
// Polls until a message of the wanted type shows up (digest broadcasts can
// answer a parked long-poll on their own, so a single poll may miss it).
const pollFor = async (room, client, type, tries = 8) => {
  for (let i = 0; i < tries; i++) {
    const msgs = await poll(room, client);
    const found = msgs.find((m) => m.t === type);
    if (found) return found;
    if (msgs.some((m) => m.t === "error")) return null; // room gone
  }
  return null;
};

let failures = 0;
const check = (cond, label) => {
  console.log(`${cond ? "PASS" : "FAIL"} - ${label}`);
  if (!cond) failures++;
};

// Joining a room that doesn't exist must fail.
const missing = await post("/api/join", { roomCode: "GHOST", username: "x" });
check(missing.status === 404 && missing.json.error.code === "room_not_found", "join unknown room rejected");

// Host A creates room TEST with one seed object.
const a = await post("/api/create-room", {
  roomCode: "TEST",
  username: "A",
  accountID: 101,
  iconID: 42,
  iconType: 1,
  color1: 3,
  color2: 8,
  glowColor: 12,
  glowEnabled: true,
  initialObjects: [{ gid: "1:0", save: "1,1,2,15,2,45", version: 1 }],
});
check(a.json.clientId === 1 && a.json.isHost === true, "A creates room as host clientId=1");

// Creating the same room twice fails.
const dup = await post("/api/create-room", { roomCode: "TEST", username: "A2" });
check(dup.status === 409 && dup.json.error.code === "room_exists", "duplicate create rejected");

// B joins; should see the seed object in its snapshot.
const b = await post("/api/join", {
  roomCode: "TEST", username: "B",
  accountID: 202, iconID: 17, iconType: 0, color1: 6, color2: 9,
});
check(b.json.clientId === 2 && b.json.isHost === false, "B joins clientId=2 not host");
check(typeof a.json.sessionToken === "string" && a.json.sessionToken.length >= 32 &&
      typeof a.json.resumeToken === "string" && a.json.resumeToken.length >= 32,
  "host receives separate high-entropy session and resume tokens");

// A guessed clientId is not a credential: an unauthenticated caller cannot
// impersonate the host and close the room.
const forgedCloseResponse = await fetch(`${BASE}/api/close-room`, {
  method: "POST",
  headers: { "Content-Type": "application/json" },
  body: JSON.stringify({ room: roomName("TEST"), client: a.json.clientId }),
});
const forgedClose = await forgedCloseResponse.json();
check(forgedCloseResponse.status === 409 && forgedClose.error.code === "not_joined",
  "guessed host clientId without bearer token is rejected");
const hostPeer = b.json.peers.find((p) => p.clientId === 1);
check(
  hostPeer && hostPeer.accountID === 101 && hostPeer.iconID === 42 &&
  hostPeer.iconType === 1 && hostPeer.color1 === 3 && hostPeer.color2 === 8 &&
  hostPeer.glowColor === 12 && hostPeer.glowEnabled === true,
  "peer list preserves account and icon appearance"
);
const bInit = await poll("TEST", 2);
const bSnap = bInit.find((m) => m.t === "snapshot" && m.objects.length > 0);
check(!!bSnap && bSnap.objects[0].gid === "1:0", "B gets seed object in snapshot");

// A sends another op while B is long-polling.
const bWait = poll("TEST", 2);
const ack = await post("/api/ops", {
  room: "TEST",
  client: 1,
  ops: [{ kind: "add", gid: "1:1", version: 1, save: "1,2,2,30,2,45" }],
});
// seq advances on the coalesced broadcast (~opCoalesceMs later), not in the
// ops response itself, so only the accepted count is asserted here.
check(ack.json.count === 1, "A op accepted count=1");
const bMsgs = await bWait;
let opMsg = bMsgs.find((m) => m.t === "op_batch");
if (!opMsg) opMsg = await pollFor("TEST", 2, "op_batch");
check(!!opMsg && opMsg.ops[0].gid === "1:1" && opMsg.origin === 1, "B receives A's add via poll");

// Late joiner C gets both objects via snapshot.
const c = await post("/api/join", { roomCode: "TEST", username: "C" });
const cMsgs = await poll("TEST", c.json.clientId);
const cSnap = cMsgs.find((m) => m.t === "snapshot" && m.objects.length >= 2);
const gids = cSnap ? cSnap.objects.map((o) => o.gid).sort() : [];
check(gids.length === 2 && gids[0] === "1:0" && gids[1] === "1:1", "late joiner C gets both objects");

// Limits are advertised so v3 clients can pace themselves.
check(
  a.json.limits && a.json.limits.maxOpsPerSec > 0 && a.json.limits.maxOpsPerRequest > 0 &&
  b.json.limits && b.json.limits.maxOpsPerSec > 0,
  "create/join responses advertise limits"
);

// State digest: the server periodically broadcasts count+hash; it must match
// an independent computation over the two objects currently in the room.
// (Run the server with COLLAB_DIGEST_INTERVAL_MS=300 to keep this fast.)
const fnv1a32 = (str, seed) => {
  let h = seed >>> 0;
  for (let i = 0; i < str.length; i++) h = Math.imul(h ^ (str.charCodeAt(i) & 0xff), 16777619) >>> 0;
  return h >>> 0;
};
const objHash = (gid, version, save) => {
  const input = `${gid}|${version}|${save}`;
  return [fnv1a32(input, 0x811c9dc5), fnv1a32(input, 0xcbf29ce4)];
};
const hex8 = (n) => (n >>> 0).toString(16).padStart(8, "0");
let ea = 0, eb = 0;
for (const [gid, v, save] of [["1:0", 1, "1,1,2,15,2,45"], ["1:1", 1, "1,2,2,30,2,45"]]) {
  const h = objHash(gid, v, save);
  ea = (ea ^ h[0]) >>> 0;
  eb = (eb ^ h[1]) >>> 0;
}
const expectedDigest = hex8(ea) + hex8(eb);
let digestMsg = null;
for (let tries = 0; tries < 8 && !digestMsg; tries++) {
  const msgs = await poll("TEST", c.json.clientId);
  digestMsg = msgs.find((m) => m.t === "digest");
}
check(!!digestMsg && digestMsg.count === 2 && digestMsg.hash === expectedDigest,
  `digest broadcast matches independent hash (${expectedDigest})`);

// Old clients do not receive sessions they cannot protect.
const oldClientResponse = await fetch(`${BASE}/api/join`, {
  method: "POST",
  headers: { "Content-Type": "application/json" },
  body: JSON.stringify({ roomCode: roomName("TEST"), username: "old", protocol: 5 }),
});
const oldClient = await oldClientResponse.json();
check(oldClientResponse.status === 426 && oldClient.error.code === "upgrade_required",
  "pre-token protocol clients are rejected");

// Malformed IDs and oversized object saves are ignored before entering room
// state or outbound queues.
const invalidOps = await post("/api/ops", {
  room: "TEST", client: a.json.clientId,
  ops: [
    { kind: "add", gid: "../../bad", version: 1, save: "1,1" },
    { kind: "add", gid: "1:999", version: 1, save: "x".repeat(70_000) },
  ],
});
check(invalidOps.status === 200 && invalidOps.json.count === 0,
  "malformed and oversized object operations are rejected");

// Host leaves -> room closes, peers get room_closed.
// Drain anything B still has queued (peers update from C joining) so the next
// poll actually establishes a long-poll waiter on the server.
await poll("TEST", 2);
const bWait2 = pollFor("TEST", 2, "room_closed");
await new Promise((r) => setTimeout(r, 200));
await post("/api/leave", { room: "TEST", client: 1 });
const closeMsg = await bWait2;
check(!!closeMsg && closeMsg.reason === "host_left", "host leaving closes room (peer gets room_closed)");

// Now joining TEST again must fail since the room was destroyed.
const after = await post("/api/join", { roomCode: "TEST", username: "B2" });
check(after.status === 404, "joining closed room fails");

// Explicit /api/close-room: host doesn't get its own room_closed, peer does.
const a2 = await post("/api/create-room", { roomCode: "ROOM2", username: "A", initialObjects: [] });
const b2 = await post("/api/join", { roomCode: "ROOM2", username: "B" });
await poll("ROOM2", b2.json.clientId); // drain B's initial snapshot/peers
await poll("ROOM2", a2.json.clientId); // drain A's peers update
const bWait3 = pollFor("ROOM2", b2.json.clientId, "room_closed");
const aWait = poll("ROOM2", a2.json.clientId);
await new Promise((r) => setTimeout(r, 200));
const closed = await post("/api/close-room", { room: "ROOM2", client: a2.json.clientId });
check(closed.status === 200 && closed.json.ok === true, "host close-room returns ok");
const bClose = await bWait3;
check(!!bClose && bClose.reason === "host_closed", "peer receives room_closed (host_closed)");
// Host's long-poll should return without a room_closed for itself.
const aFin = await aWait;
const aClose = aFin.find((m) => m.t === "room_closed");
check(!aClose, "host does not receive its own room_closed on explicit close");

// Non-host trying to close is forbidden.
const a3 = await post("/api/create-room", { roomCode: "ROOM3", username: "A", initialObjects: [] });
const b3 = await post("/api/join", { roomCode: "ROOM3", username: "B" });
const nope = await post("/api/close-room", { room: "ROOM3", client: b3.json.clientId });
check(nope.status === 403 && nope.json.error.code === "not_host", "non-host cannot close the room");
await post("/api/leave", { room: "ROOM3", client: a3.json.clientId });

// Delete tombstone: a stale add/update for a deleted object must not resurrect
// it for late joiners. Host adds an object (v1), deletes it (v2), then a late
// op re-adds the same gid with an older version (v1) -> must be rejected.
const t1 = await post("/api/create-room", { roomCode: "TOMB", username: "A", initialObjects: [] });
await post("/api/ops", { room: "TOMB", client: t1.json.clientId, ops: [{ kind: "add", gid: "9:1", version: 1, save: "1,1,2,15,2,45" }] });
await post("/api/ops", { room: "TOMB", client: t1.json.clientId, ops: [{ kind: "delete", gid: "9:1", version: 2 }] });
const stale = await post("/api/ops", { room: "TOMB", client: t1.json.clientId, ops: [{ kind: "add", gid: "9:1", version: 1, save: "1,1,2,15,2,45" }] });
check(stale.json.count === 0, "stale add for deleted gid rejected (tombstone)");
const tJoiner = await post("/api/join", { roomCode: "TOMB", username: "L" });
const tMsgs = await poll("TOMB", tJoiner.json.clientId);
const tSnap = tMsgs.find((m) => m.t === "snapshot");
const hasGhost = tSnap && tSnap.objects.some((o) => o.gid === "9:1");
check(!hasGhost, "late joiner does not see the deleted (ghost) object");
// A newer add (version above the delete) is allowed to resurrect the gid.
const resurrect = await post("/api/ops", { room: "TOMB", client: t1.json.clientId, ops: [{ kind: "add", gid: "9:1", version: 3, save: "1,1,2,15,2,45" }] });
check(resurrect.json.count === 1, "newer add resurrects gid past the tombstone");
await post("/api/leave", { room: "TOMB", client: t1.json.clientId });

console.log(failures === 0 ? "\nALL TESTS PASSED" : `\n${failures} TEST(S) FAILED`);
process.exit(failures === 0 ? 0 : 1);
