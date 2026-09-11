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
