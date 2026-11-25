# Geometry Dash Editor MCP (Paimbnails)

Local **Model Context Protocol (MCP)** server embedded inside Geometry Dash (`127.0.0.1:21690/mcp`) giving any AI assistant full control over the Level Editor, real-time **1:1 OpenGL rendering**, and complete GD 2.2091 trigger/object documentation.

---

## 1. Connecting Any AI Client

### Streamable HTTP (Antigravity, Claude Code, Cursor, VS Code, MCP Inspector)
```json
{
  "mcpServers": {
    "geometry-dash": {
      "type": "http",
      "url": "http://127.0.0.1:21690/mcp"
    }
  }
}
```

### Stdio Bridge (Claude Desktop & stdio-only clients)
```json
{
  "mcpServers": {
    "geometry-dash": {
      "command": "python3",
      "args": ["tools/gd_mcp_bridge.py"],
      "env": {
        "GD_MCP_URL": "http://127.0.0.1:21690/mcp"
      }
    }
  }
}
```
> Tip: While Geometry Dash is running, you can also download `bridge.py` from `http://127.0.0.1:21690/bridge.py`, check health at `http://127.0.0.1:21690/health`, or grab a live 1:1 PNG at `http://127.0.0.1:21690/render.png`.

---

## 2. MCP Tools Summary (29 Tools)

| Category | Tools |
|---|---|
| **1:1 Real-Time Vision** | `render_1to1` (returns PNG image + exact world bounds, optional coordinate rulers `show_coords` & `show_hitboxes` / `highlight_group`) |
| **Level & State** | `get_editor_state`, `list_local_levels`, `open_level_in_editor`, `save_level` |
| **Inspection & QA** | `get_objects` (supports `compact=true`), `get_object_raw`, `search_object_catalog`, `get_gd_reference`, `inspect_structure_bounds`, `audit_level_quality`, `get_sprite_metrics`, `validate_gameplay_path` |
| **Build & Decorate** | `place_object`, `place_objects_batch` (supports `trigger` + `params` too), `update_objects`, `delete_objects`, `transform_objects`, `mirror_or_clone_structure`, `select_objects`, `undo`, `redo` |
| **Triggers & Groups** | `place_trigger`, `manage_groups` |
| **Colors & Settings** | `get_color_channels`, `set_color_channel` (single or batch `channels=[...]`), `get_level_settings`, `set_level_settings`, `set_editor_camera` |

---

## 3. Coordinate System & 1:1 Scale

- **1 Block** = `30 x 30` GD units.
- **Ground Surface**: `worldY = 0`. A block sitting on the ground has center `worldY = 15`. Row `N` (0-indexed) has center `worldY = 15 + N * 30`.
- **Unified World Y**: All MCP tools (`place_object`, `place_objects_batch`, `place_trigger`, `get_objects`, `delete_objects`, `select_objects`, `transform_objects`, `set_editor_camera`, `render_1to1`, `inspect_structure_bounds`, `audit_level_quality`, `mirror_or_clone_structure`) use `worldY` by default (`world_coords=true`).
- **1:1 Viewport (`zoom = 1.0`)**: `320` GD units tall (~10.67 blocks) × `569` GD units wide (16:9).
- **Z-Layers**: `B5=-5`, `B4=-3`, `B3=-1`, `B2=1`, `B1=3`, `Default=0`, `T1=5`, `T2=7`, `T3=9`, `T4=11`.
- **Special Color Channels**: `1000=BG`, `1001=G1`, `1002=Line`, `1003=3DL`, `1004=Obj`, `1005=P1`, `1006=P2`, `1007=LBG`, `1009=G2`, `1010=Black`, `1011=White`, `1012=Lighter`.

---

## 4. Complete GD 2.2091 Triggers Reference

### Color & Visual Triggers
- **899 — Color**: Changes color channel (`23=targetColorID`, `7/8/9=RGB`, `10=duration`, `35=opacity`, `17=blending`, `15=P1`, `16=P2`, `50=copyColorID`, `49=copyHSV`, `60=copyOpacity`).
- **1006 — Pulse**: Pulses channel/group (`51=target`, `52=0:channel/1:group`, `48=0:color/1:HSV`, `45=fadeIn`, `46=hold`, `47=fadeOut`, `65=mainOnly`, `66=detailOnly`, `86=exclusive`).
- **1007 — Alpha**: Fades group opacity (`51=targetGroupID`, `35=opacity`, `10=duration`).
- **2903 — Gradient**: 2-color or 4-corner gradient (`174=gradientID`, `203..206=cornerColorIDs`, `207=vertexMode`, `208=blendingMode`, `209=layer`).
- **3029 / 3030 / 3031**: BG Change / Ground Change / Middleground Change.
- **1818 / 1819**: BG Effect On / Off.

### Motion & Transform Triggers
- **901 — Move**: Moves group (`51=targetGroupID`, `28=moveX`, `29=moveY`, `10=duration`, `30=easing(0..18)`, `85=easingRate`, `58=lockPlayerX`, `59=lockPlayerY`, `141=lockCameraX`, `142=lockCameraY`, `100=useTargetMove`, `101=targetMode`, `71=targetPosGroup`, `394=smallStep`, `144=silent`).
- **1346 — Rotate**: Rotates group around center (`51=targetGroup`, `71=centerGroup`, `68=degrees`, `69=times360`, `10=duration`, `30=easing`, `70=lockObjectRotation`, `105=aimTargetID`).
- **1347 — Follow**: Follows center group (`51=target`, `71=center`, `72=xMod`, `73=yMod`, `10=duration`).
- **1814 — Follow Player Y**: Tracks player Y (`51=target`, `90=speed`, `91=delay`, `92=offset`, `93=maxSpeed`, `10=duration`).
- **2067 — Scale**: Scales group around center (`51=target`, `71=center`, `150=scaleX`, `151=scaleY`, `10=duration`, `30=easing`).
- **3016 / 3660 / 3661 — Advanced Follow / Edit / Retarget**: Physics-like velocity/steering follower.
- **3032 / 3033 — Keyframe Point / Keyframe Animation**: Path-based keyframe motion.
- **1585 — Animate**: Triggers monster/object animation (`51=target`, `76=animID`).

### Area Effects (2.2)
- **3006 Area Move**, **3007 Area Rotate**, **3008 Area Scale**, **3009 Area Fade**, **3010 Area Tint**, **3011–3015 Edit Area**, **3024 Area Stop**.

### Spawning, Logic & State Triggers
- **1049 — Toggle**: Enable/disable group (`51=target`, `56=activate(0/1)`).
- **1268 — Spawn**: Spawns group triggers (`51=target`, `63=delay`, `556=delayVariance`, `107=spawnOrdered`, `442=remap`).
- **1616 — Stop**: Stops/pauses/resumes triggers (`51=target`, `580=0:stop/1:pause/2:resume`).
- **1595 — Touch**: Activates on tap/hold (`51=target`, `81=holdMode`, `82=toggleMode`, `89=dualMode`, `198=playerControl`).
- **1611 — Count / 1811 — Instant Count / 1817 — Pickup**: Counter triggers (`80=itemID`, `77=count`, `51=target`, `88=comparison`).
- **1815 — Collision / 1816 — Collision Block / 3609 — Instant Collision / 3640 — Collision State**: Hitbox collision triggers.
- **3607 — Sequence**, **1912 — Random**, **2068 — Advanced Random**, **3604 — Event**.
- **3614 — Time**, **3615 — Time Event**, **3617 — Time Control**, **3618 — Reset**.
- **3619 — Item Edit**, **3620 — Item Compare**, **3641 — Persistent Item**, **3655 — Object Control**, **3662 — Link Visible**, **1829 — Spawn Particle**.

### Camera & Shaders (2.2)
- **1913 Camera Zoom**, **1914 Camera Static**, **1916 Camera Offset**, **2015 Camera Rotate**, **2062 Camera Edge**, **2925 Camera Mode**, **2016 Camera Guide**, **1520 Shake**.
- **2904 Shader Setup**, **2905 Shock Wave**, **2907 Shock Line**, **2909 Glitch**, **2910 Chromatic**, **2911 Chromatic Glitch**, **2912 Pixelate**, **2913 Lens Circle**, **2914 Radial Blur**, **2915 Motion Blur**, **2916 Bulge**, **2917 Pinch**, **2919 Grayscale**, **2920 Sepia**, **2921 Invert Color**, **2922 Hue**, **2923 Edit Color**, **2924 Split Screen**.

### Audio & Gameplay
- **1934 Song**, **3605 Edit Song**, **3602 SFX**, **3603 Edit SFX**, **3606 Audio Line Guide**, **3608 BPM Guide**.
- **1917 Reverse**, **1932 Player Control**, **1935 Timewarp**, **2066 Gravity**, **2899 Options**, **2900 Rotate Gameplay**, **3022 Teleport**, **3600 End**, **3612 Platformer End/Checkpoint**, **3613 UI**, **1612/1613 Hide/Show Player**, **1812/1813 Trail**, **31 Start Pos**.
- **Enter Effects**: `22..28`, `55..59`, `1931 Enter Stop`, `3017..3021 Custom Enter Move/Rotate/Scale/Fade/Tint`.
