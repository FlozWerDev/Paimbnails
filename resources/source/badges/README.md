# Role badge sources

These SVGs are the editable sources for the 100x100 role badges stored directly
under `resources/`. They extend the existing Admin and Moderator badge family
without changing the runtime badge loader.

Regenerate a badge with ImageMagick from the repository root:

```powershell
magick -background none -density 384 resources/source/badges/paim_Helper.svg -resize 100x100 resources/paim_Helper.png
```

Use the same command for `Idea` and `Vip`. The source directory is not included
in `mod.json`, so only the rendered PNGs are packaged.
