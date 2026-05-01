# Paimbnails Forum — API Reference

This document specifies the **server endpoints**, **JSON shapes** and **client flows**
used by the in-game Forum tab (`PaimonHubLayer` → `Forum`).

The client lives in:

- `src/features/forum/services/ForumApi.{hpp,cpp}` — typed client (REST + local cache).
- `src/features/forum/ui/CreatePostPopup.{hpp,cpp}` — popup to compose new posts.
- `src/features/forum/ui/PostDetailPopup.{hpp,cpp}` — full-thread viewer.
- `src/layers/PaimonHubLayer.cpp` — list, filters, sort, "+ New Post".

The base URL is whatever `HttpClient::get().getServerURL()` returns. All endpoints
are mounted under that base.

When `getServerURL()` is empty or any HTTP call fails, the client transparently
falls back to a local cache stored in the mod's `savedValues` under the key
`forum-cache` (a JSON array of `Post` objects). That keeps the UI usable while
the server is being built.

---

## 1. Auth

All write endpoints (create/like/report/delete) are sent through
`HttpClient::postWithAuth`, which adds a `X-Mod-Code` header.
The `X-Mod-Code` is the same code already used by the rest of the mod
(`HttpClient::setModCode`).

Each request additionally embeds the requesting user inside the body via
`author: { accountID, username, iconID, iconType, color1, color2, glowEnabled }`.
The server **must** validate the requesting account against its own session/auth
table. The client's `accountID` should be cross-checked against the GD account
holding the request socket — never trust the body alone.

---

## 2. Models

### 2.1 `Author`

```json
{
  "accountID": 12345,
  "username": "Flozwer",
  "iconID": 22,
  "iconType": 0,
  "color1": 12,
  "color2": 9,
  "glowEnabled": true
}
```

`iconType` follows GD's `IconType` enum (0 = cube, 1 = ship, 2 = ball, …).

### 2.2 `Reply`

```json
{
  "id": "reply_abc123",
  "postId": "post_xyz",
  "parentReplyId": "reply_zzz",  // empty string if it's a top-level reply
  "author": { ...Author },
  "content": "Nice thumbnail!",
  "createdAt": 1730000000,         // epoch seconds (UTC)
  "likes": 4,
  "likedByMe": false,
  "reportCount": 0
}
```

`parentReplyId` enables **threads**: any non-empty value points to another reply
inside the same post, and the UI renders an `-> thread` indicator.

### 2.3 `Post`

```json
{
  "id": "post_xyz",
  "author": { ...Author },
  "title": "How do you capture clean thumbnails?",
  "description": "I keep getting compression artifacts...",
  "tags": ["Question", "Tip"],
  "createdAt": 1730000000,
  "updatedAt": 1730000200,
  "likes": 12,
  "likedByMe": true,
  "replyCount": 3,
  "reportCount": 0,
  "pinned": false,
  "locked": false,
  "replies": [ ...Reply ]   // only present in `GET /posts/:id`
}
```

For list endpoints, `replies` SHOULD be omitted to keep the payload small.

---

## 3. Endpoints

### 3.1 List posts

```
GET  /api/forum/posts
```

**Query params:**

| Param  | Type           | Description                                                |
|--------|----------------|------------------------------------------------------------|
| sort   | `recent`/`top`/`liked` | Sort mode (default `recent`).                       |
| tags   | comma-separated tag names | Filter posts whose tag set includes ANY of these. |
| q      | string         | Optional case-insensitive search in title/description.    |
| page   | int (1-based)  | Pagination, default `1`.                                  |
| limit  | int            | Page size, default `20`, capped server-side (e.g. 50).    |

**Response (200):**

```json
{
  "posts": [ ...Post ],
  "page": 1,
  "limit": 20,
  "total": 137
}
```

The client also accepts a bare top-level array (`[ ...Post ]`) for convenience.

---

### 3.2 Get post (with replies)

```
GET  /api/forum/posts/:postId
```

**Response (200):** a full `Post`, including `replies`. Replies SHOULD be sorted
chronologically; threading is implicit through `parentReplyId`.

**Errors:** `404` if the post doesn't exist or is soft-deleted.

---

### 3.3 Create post

```
POST /api/forum/posts
Content-Type: application/json
X-Mod-Code: <user mod code>
```

**Body:**

```json
{
  "title": "string (1..120)",
  "description": "string (0..4000)",
  "tags": ["Tag1", "Tag2"],
  "author": { ...Author },
  "localId": "local-abc..."   // client-generated id for optimistic updates
}
```

**Response (200):** the new `Post` object as stored on the server (with the real
`id`). The client will swap the optimistic local copy for the server one.

**Validation rules (recommended):**

- `title` not empty after trim.
- Up to 5 tags. Reject control chars / scripts.
- Banned authors cannot create posts (`HttpClient::checkBan`).

---

### 3.4 Delete post

```
POST /api/forum/posts/:postId/delete
```

(We use POST instead of HTTP DELETE because the existing `HttpClient` doesn't
expose `DELETE`; the server can also accept real `DELETE` if desired.)

**Body:**

```json
{ "postId": "post_xyz" }
```

**Auth:** only the post's author or a moderator/admin may delete. Soft-delete
is preferred; mark `deleted=true` and exclude from list responses.

**Response:** `{ "ok": true }` on success.

---

### 3.5 Toggle post like

```
POST /api/forum/posts/:postId/like
```

**Body:** `{ "postId": "post_xyz" }`

The endpoint is a **toggle**: if the user already liked the post, the like is
removed; otherwise it's added. The server returns the new state:

```json
{ "ok": true, "likes": 13, "likedByMe": true }
```

---

### 3.6 Report post

```
POST /api/forum/posts/:postId/report
```

**Body:**

```json
{
  "postId": "post_xyz",
  "reason": "Reason text",
  "author": { ...Author }
}
```

Server-side: store a row in a `reports` table for moderation. Auto-hide the
post when `reportCount` crosses a threshold.

---

### 3.7 List replies (optional dedicated endpoint)

```
GET  /api/forum/posts/:postId/replies?page=1&limit=50
```

Most clients use `GET /posts/:postId` directly which includes replies; this
endpoint is provided so paginated reply loading remains possible.

---

### 3.8 Create reply / thread

```
POST /api/forum/posts/:postId/replies
```

**Body:**

```json
{
  "postId": "post_xyz",
  "parentReplyId": "",        // empty = top-level reply, otherwise reply-to-reply
  "content": "string (1..2000)",
  "author": { ...Author },
  "localId": "local-..."
}
```

**Response:** the new `Reply` object.

---

### 3.9 Toggle reply like

```
POST /api/forum/replies/:replyId/like
```

**Body:** `{ "replyId": "reply_abc123" }`. Behaviour mirrors §3.5.

---

### 3.10 Report reply

```
POST /api/forum/replies/:replyId/report
```

**Body:**

```json
{
  "replyId": "reply_abc",
  "reason": "Reason text",
  "author": { ...Author }
}
```

---

### 3.11 Delete reply

```
POST /api/forum/replies/:replyId/delete
```

**Body:** `{ "replyId": "reply_abc" }`. Same auth rules as §3.4.

---

### 3.12 List tags (predef + popular)

```
GET  /api/forum/tags
```

**Response (200):**

```json
{ "tags": ["Guide", "Tip", "Question", "Bug", ...] }
```

(or a bare array). The client falls back to a hard-coded list when this fails.

---

## 4. Client flows

### 4.1 Open the forum

1. `PaimonHubLayer::buildForumTab` builds the tag picker, sort bar and post list.
2. `refreshForumPosts` builds a `ListFilter` from the current sort/tag selection
   and calls `ForumApi::listPosts`.
3. The callback runs `renderPosts(...)` which builds a scrollable list of
   `PostCell`s with the author icon (`SimplePlayer` from GD), username,
   relative time, title, description preview, tag chips and `Likes`/`Replies`
   stats.
4. Tapping a card fetches the full post via `ForumApi::getPost` and shows
   `PostDetailPopup`.

### 4.2 Create a post

1. `+ New Post` → `CreatePostPopup::create(availableTags, onCreated)`.
2. The popup shows real `geode::Popup` chrome (close button + border + title).
3. The user fills title + description and toggles tag chips. Tapping `+` opens
   an inline tag-add overlay so new tags can be created without leaving the popup.
4. `Post` runs `ForumApi::createPost(...)`, which:
   - inserts the new post in the local cache **immediately** (optimistic),
   - issues `POST /api/forum/posts` if a server is configured,
   - replaces the optimistic copy with the server response on success.
5. `onCreated` callback re-runs `refreshTagButtons` + `refreshForumPosts` in the hub.

### 4.3 View / reply / report / delete

`PostDetailPopup::rebuild` renders:

- Header: author icon + username + relative time + absolute timestamp.
- Tags chips.
- Description through `MDTextArea` (markdown-friendly).
- Action row: `Like`, `Report`, and `Delete` (only for the post's author).
- Replies scroll: each reply shows author icon + username + relative time +
  short content + actions (`Like`, `Reply`, `Report`, `Delete` if owner).
- Footer: a `Reply` input + `Reply` send button. Tapping `Reply` on an existing
  reply sets `m_replyTo` so the new reply is posted as a child (thread).

All actions go through `ForumApi::*` which keeps the local cache in sync and
falls back to the cache when the server is unreachable.

---

## 5. Recommended database schema (server side)

```sql
-- posts
CREATE TABLE forum_posts (
  id            TEXT PRIMARY KEY,
  account_id    INTEGER NOT NULL,
  username      TEXT NOT NULL,
  icon_id       INTEGER NOT NULL DEFAULT 1,
  icon_type     INTEGER NOT NULL DEFAULT 0,
  color1        INTEGER NOT NULL DEFAULT 0,
  color2        INTEGER NOT NULL DEFAULT 3,
  glow_enabled  INTEGER NOT NULL DEFAULT 0,
  title         TEXT NOT NULL,
  description   TEXT NOT NULL DEFAULT '',
  created_at    INTEGER NOT NULL,
  updated_at    INTEGER NOT NULL,
  pinned        INTEGER NOT NULL DEFAULT 0,
  locked        INTEGER NOT NULL DEFAULT 0,
  deleted       INTEGER NOT NULL DEFAULT 0,
  report_count  INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE forum_post_tags (
  post_id  TEXT NOT NULL,
  tag      TEXT NOT NULL,
  PRIMARY KEY (post_id, tag)
);

-- replies
CREATE TABLE forum_replies (
  id              TEXT PRIMARY KEY,
  post_id         TEXT NOT NULL,
  parent_reply_id TEXT NOT NULL DEFAULT '',
  account_id      INTEGER NOT NULL,
  username        TEXT NOT NULL,
  icon_id         INTEGER NOT NULL DEFAULT 1,
  icon_type       INTEGER NOT NULL DEFAULT 0,
  color1          INTEGER NOT NULL DEFAULT 0,
  color2          INTEGER NOT NULL DEFAULT 3,
  glow_enabled    INTEGER NOT NULL DEFAULT 0,
  content         TEXT NOT NULL,
  created_at      INTEGER NOT NULL,
  deleted         INTEGER NOT NULL DEFAULT 0,
  report_count    INTEGER NOT NULL DEFAULT 0
);

-- likes (reactions)
CREATE TABLE forum_likes (
  account_id  INTEGER NOT NULL,
  target_kind TEXT NOT NULL,    -- 'post' | 'reply'
  target_id   TEXT NOT NULL,
  created_at  INTEGER NOT NULL,
  PRIMARY KEY (account_id, target_kind, target_id)
);

-- reports
CREATE TABLE forum_reports (
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  reporter_id   INTEGER NOT NULL,
  reporter_name TEXT NOT NULL,
  target_kind   TEXT NOT NULL,
  target_id     TEXT NOT NULL,
  reason        TEXT NOT NULL,
  created_at    INTEGER NOT NULL
);
```

When the server returns a `Post`/`Reply`, it should compute `likedByMe` based on
the requester's `accountID` join against `forum_likes`.

---

## 5.1 User Status (online/offline)

### 5.1.1 Heartbeat

```
POST /api/forum/users/heartbeat
Content-Type: application/json
X-Mod-Code: <user mod code>
```

**Body:**

```json
{
  "author": { ...Author },
  "timestamp": 1730000000
}
```

The server must:
1. Verify the `X-Mod-Code` and `author.accountID`.
2. Update (or insert) a row in `forum_user_status` with:
   - `account_id` = `author.accountID`
   - `username` = `author.username`
   - `last_seen_at` = `timestamp` (or server time)
   - `online` = `1`
3. Return `{ "ok": true }`.

### 5.1.2 Get user status

```
GET /api/forum/users/:accountID/status
```

**Response (200):**

```json
{
  "accountID": 12345,
  "online": true,
  "lastSeen": 1730000000
}
```

`online` is `true` only if the user's `last_seen_at` is within the last **5 minutes** (configurable threshold). Otherwise `online: false` and `lastSeen` contains the user's last known `last_seen_at`.

### 5.1.3 Recommended schema

```sql
CREATE TABLE forum_user_status (
  account_id    INTEGER PRIMARY KEY,
  username      TEXT NOT NULL,
  last_seen_at  INTEGER NOT NULL DEFAULT 0,
  online        INTEGER NOT NULL DEFAULT 0
);
```

A background job or query-time check should compute `online` by comparing `last_seen_at` against the current time minus the timeout window (e.g. 300 seconds).

---

## 6. Pagination & sort

- `recent` → `ORDER BY pinned DESC, created_at DESC`
- `top`    → `ORDER BY pinned DESC, (likes - report_count) DESC, created_at DESC`
- `liked`  → `ORDER BY pinned DESC, likes DESC, created_at DESC`

Cap `limit` to `50` and reject negative `page`. Always return the same envelope
shape so the client can iterate without branching.

---

## 7. Versioning

The current API version is `v1`. If breaking changes are introduced later,
move the routes under `/api/v2/forum/...` and bump the client's `ForumApi`
class with a versioned `BASE_PATH` constant. Keep `v1` alive for one minor
release window of the mod.
