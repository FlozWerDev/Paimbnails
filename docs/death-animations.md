# Death animations

Open a level, pause, and choose **VFX** in the left button menu. Choose one of
12 styles, **Random** (avoids consecutive repeats), or **Original**. Selection
is saved immediately. **Replay** previews the chosen shader without resuming
the level or playing a sound. Original leaves Geometry Dash's equipped death
effect in control; it has no shader preview. The adjacent **SFX** button keeps
its existing sound library controls.

Styles: Supernova, Vortex, Prism, Shockwave, Embers, Frost, Glitch, Solar Flare,
Lotus, Atom, Rift, Stardust. Effects last 0.85 seconds, with time-based easing
and a soft fade. Some use the dying player's color; others have a themed palette.

The `Death Effects` module controls both selectors. The gameplay performance
option that disables mod visuals suppresses custom animations. The default is
Original. The saved integer `death-animation-style` accepts -1 (Original),
0–11 (styles), and 12 (Random); invalid values resolve to Original.

Implementation replaces `PlayerObject::playDeathEffect` only for the two local
PlayLayer players, falling back to the native call if the shader is unavailable.
Menu/editor effects keep their normal behavior. Positions are converted into
the object layer's coordinates to follow camera transforms. Effects live outside
the player, expire automatically, are capped at four concurrent nodes, and are
cleared on reset and exit. The shader is prewarmed on reset when enabled and
uses one small quad per death, without a full-screen render target.

Validation before release:

- Compile the Windows target and verify the packaged shader resource.
- In game, choose each style and die; check sound, respawn and selection persistence.
- Check practice resets, dual deaths, camera zoom/rotation and rapid retries.
- Check Original, disabled module, mod-visual performance option, and noclip.
- Open the popup while paused, select Random, replay, close and resume.
- Check pause button layout with other pause-menu mods and on mobile.

A headless Mesa OpenGL ES test compiled and linked the shader and rendered all
12 styles at five times each: transparent endpoints, visible intermediate
frames and transparent corners. This does not replace the in-game checks above.
