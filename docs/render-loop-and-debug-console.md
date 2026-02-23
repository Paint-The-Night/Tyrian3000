# Render Loop and Debug Console Notes

This note captures behavior that is easy to miss when adding overlays or AI automation.

## Two Different Frame Models

- In-game (`JE_main`): continuous gameplay loop.
  - `/Users/garyperrigo/Code/Tyrian3000/src/tyrian2.c`
- Title screen (`titleScreen`): menu/event-driven loop with explicit idle waiting and palette staging.
  - `/Users/garyperrigo/Code/Tyrian3000/src/tyrian2.c`

Do not assume title behaves like gameplay. It can block waiting for input between renders.

## Why the Debug Console Broke on Title

The title path draws from static background buffers and uses palette fades/ranges. Overlay logic that mutates the base 8-bit frame buffer directly can cause visual corruption (ghosting/darkening/tint artifacts), especially on title.

## Current Cross-Mode Fix

- Console is drawn on a scratch surface, never directly mutating `VGAScreen`.
  - `/Users/garyperrigo/Code/Tyrian3000/src/video.c`
- Console panel uses a solid fill to avoid palette-tinted artifacts.
  - `/Users/garyperrigo/Code/Tyrian3000/src/debug_console.c`
- Title loop keeps rendering while console is active and skips title menu handling in that state.
  - `/Users/garyperrigo/Code/Tyrian3000/src/tyrian2.c`

## Remote Control / MCP Notes

- In-process socket control lives in:
  - `/Users/garyperrigo/Code/Tyrian3000/src/remote_control.c`
- CLI wrapper:
  - `/Users/garyperrigo/Code/Tyrian3000/tools/gamectl.py`
- MCP wrapper:
  - `/Users/garyperrigo/Code/Tyrian3000/tools/game_mcp_server.py`

Important behavior:

- `wait_frames` depends on presented frames. If a UI path is not presenting, frame waits can stall.
- `send-key` / `send-keys` default wait is `0` frames to stay reliable across title and gameplay.

## Screenshot Format Note

Remote screenshots are written as BMP (32-bit RGB BMP via SDL save path). Some tools in this workflow may not preview BMP directly. Convert to PNG when needed (for example with `ffmpeg`).

