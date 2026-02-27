# Title Logo Extraction (Direct Data File)

This documents how to extract the title-screen `Tyrian` logo directly from game data (not from a screenshot).

## Source Mapping

- Title screen draw call uses:
  - `blit_sprite(..., PLANET_SHAPES, 146)` in `src/tyrian2.c`
- `PLANET_SHAPES` is table index `3` in `src/sprite.h`.
- Main sprite tables are loaded from `data/tyrian2000/tyrian.shp` in `JE_loadMainShapeTables()` (`src/sprite.c`).

So the logo sprite is:

- `file`: `data/tyrian2000/tyrian.shp`
- `table_index`: `3` (`PLANET_SHAPES`)
- `sprite_index`: `146`

## Palette Used

To match title-screen colors, use the same palette family as the title background:

- Title screen calls `JE_loadPic(..., 4, ...)` (`src/tyrian2.c`)
- `pcxpal` mapping (`src/pcxmast.c`) maps pic `4` to palette index `8`
- Palette data comes from `data/tyrian2000/palette.dat`
- VGA 6-bit to 8-bit conversion is:
  - `c8 = (c6 << 2) | (c6 >> 4)` (same as `JE_loadPals` in `src/palette.c`)

## Sprite Decode Format

The packed sprite stream format (from `blit_sprite` in `src/sprite.c`):

- `255` + next byte `N`: skip `N` transparent pixels
- `254`: move to next row
- `253`: skip one transparent pixel
- otherwise: byte is a palette index for one opaque pixel

## Output Generated

Current extracted files:

- `build/extracts/tyrian-logo-direct.png`
- `build/extracts/tyrian-logo-direct.meta.txt`

## Repeatable Extraction

1. Decode the target sprite from `tyrian.shp` (table 3, sprite 146) into RGBA using palette index 8 from `palette.dat`.
2. Write raw RGBA bytes plus width/height metadata.
3. Convert raw RGBA to PNG.

Example conversion command used for step 3:

```bash
ffmpeg -v error -f rawvideo -pix_fmt rgba -s 304x121 \
  -i build/extracts/tyrian-logo-direct.rgba \
  -frames:v 1 -f image2pipe -vcodec png - \
  > build/extracts/tyrian-logo-direct.png
```
