# Title Subtitle Implementation Notes

Practical notes from implementing `Gravitium War` subtitle animation on the title screen.

## Where It Lives

- Title screen logic: `src/tyrian2.c` in `titleScreen()`.
- Subtitle conversion/blit helpers in `src/tyrian2.c`:
  - `title_screen_prepare_subtitle_indexed(...)`
  - `title_screen_blit_subtitle(...)`

## Asset Path and Fallback

- Primary subtitle image path:
  - `data/tyrian2000/logo-gravitium-war.png`
- Fallback path:
  - `data/tyrian2000/logo-title.png`
- Runtime load uses `IMG_Load(...)`, then converts once to indexed 8-bit pixels
  (nearest match against current title palette) before blitting to `VGAScreen`.

## Positioning and Animation Model

- Current placement uses:
  - `subtitle_y = 74`
  - `subtitle_right_x = 306`
  - `subtitle_x_final = subtitle_right_x - subtitle_w`
- Slide-in animation happens during logo rise:
  - `subtitle_x` interpolates from off-screen right to `subtitle_x_final`.

This means horizontal anchoring is right-edge based, not centered. If width changes, the subtitle still tucks under the logo tail.

Because subtitle pixels are now written into the same 8-bit surface as the rest of
the title, it follows palette fades and does not leak into other menus.

## Size Tuning

- Current runtime subtitle size is `160x21` (`data/tyrian2000/logo-gravitium-war.png`).
- Source extraction lives in:
  - `new_src/logo-title.png` (full art)
  - `new_src/logo-gravitium-war.png` (cropped high-res)

## Regeneration Script

- Script: `tools/prepare_assets.py`
- It now outputs both:
  - `data/tyrian2000/logo-gravitium-war.png`
  - `data/tyrian2000/logo-title.png`
- Current cap is:
  - `MAX_W = 160`
  - `MAX_H = 21`

## Debugging Note

- Remote screenshots via `tools/gamectl.py screenshot ...` capture `VGAScreen`.
- Subtitle now renders directly into `VGAScreen`, so it should appear in those screenshots.

## Build/Run Notes on macOS

- This repo has both uppercase and lowercase makefile names (`Makefile`, `makefile`, `Makefile.web`, `makefile.web`).
- Plain `make`/`gmake` may try built-in remake rules (`tangle`), causing unrelated errors.
- For quick UI iteration, prefer the web loop documented in `docs/web-verification-loop.md`.
- Reliable build command:

```bash
gmake -f Makefile --no-builtin-rules --no-builtin-variables -j4
```

- Useful launch command for quick title checks:

```bash
python3 tools/gamectl.py launch --no-build --start-menu=title -- --no-sound --no-joystick
```
