# Images as gradient points

In Icon Gradients, select a channel and a point, then press **Image** to import
an image for that point. **Add** creates another point; it can keep a solid color
or receive a different image. Linear and radial gradients blend the images and
colors using the positions of the points. Drag points to change that blend.

Image points display an **I**. Press **Color** to return the selected point to its
saved solid color, then adjust it with the color picker. Copy, paste, and saved
gradients preserve each point's image. Imported files are copied into the mod's
local assets so moving the original file does not break the gradient.

Images are static and resized to 256 × 256 within a shared texture atlas, with
up to 24 points. Transparent images retain their alpha. Missing or unsupported
images fall back to the point's saved color. Existing whole-channel image fills
are read as the same image on every point.

## Performance review

The initial implementation added work to every CCSprite draw: a Geode metadata
lookup (which may create that metadata) plus four uniform-name lookups for each
image sprite. The draw hook now checks a registry containing only image sprites;
it never accesses node metadata or resolves uniform names while drawing. State
destruction removes the registry entry without retaining the sprite.

Atlases now allocate only the required rows and columns. One image takes 256 KiB
instead of 6 MiB, and two take 512 KiB. Resolution stays at 256 pixels per image.
Recently used atlases have a 16 MiB LRU budget; resized source images have an
8 MiB budget. Sprites retain active atlases independently of the LRU so eviction
cannot invalidate their textures. Original decoded images are released after
resizing. Reopening a recently used configuration reuses its atlas, and changing
image combinations can reuse the resized sources instead of decoding them again.

The shaders calculate animated image coordinates once per fragment instead of
once per image point (up to 24 times in radial mode). Fully transparent mask pixels
exit early, and single-point radial gradients skip weighting. Radial weights use
a squared dot product instead of calculating and then squaring a square root.

These are code-derived reductions, not measured FPS gains. No build or game
benchmark was run for this review. The first uncached import still decodes and
uploads on the main thread. Atlas builds taking at least 8 ms emit an
`[IconGradients] Atlas build` debug log with duration, source-decode count, and
uploaded size, allowing those remaining import stalls to be identified in-game.
