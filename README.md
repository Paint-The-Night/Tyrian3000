# Tyrian 3000

**The year is 20,031. The galaxy still needs saving. Now with better menus.**

Tyrian 3000 is an enhanced fork of [OpenTyrian2000](https://github.com/KScl/opentyrian2000),
itself an open-source port of the legendary DOS vertical scrolling shooter
**Tyrian**. You play as Trent Hawkins, ace fighter-pilot, blasting through
MicroSol's forces one pixel at a time.

Tyrian 3000 aims to stay true to the original experience while adding
quality-of-life improvements and — eventually — brand new episodes and content.

## What's New in Tyrian 3000

- Added Debug Menu
- Added resolution check command to Debug Menu
- Added Remote Control (for AI development and automation tools)
- Added launch flags to open directly into menus
- Added scaler resolution display in Graphics menu
- Added MCP wrapper + `gamectl` CLI for automation workflow

## Building from Source

### Requirements

- **SDL2** and **SDL2_net**
- **pkg-config**
- A C compiler (GCC or Clang)

On macOS:

```bash
brew install sdl2 sdl2_net pkg-config
```

On Debian/Ubuntu:

```bash
sudo apt install libsdl2-dev libsdl2-net-dev pkg-config build-essential
```

### Game Data

Tyrian 3000 requires the **Tyrian 2000** data files (released as freeware).
Download and extract them into a `data/tyrian2000` directory:

```bash
curl -LO https://www.camanis.net/tyrian/tyrian2000.zip
mkdir -p data
unzip tyrian2000.zip -d data
```

### Build & Run

```bash
make TYRIAN_DIR=data/tyrian2000
./opentyrian2000
```

For a debug build:

```bash
make debug TYRIAN_DIR=data/tyrian2000
./opentyrian2000
```

## AI Remote Control

Tyrian 3000 supports an in-process remote control socket for AI-driven testing.

Start the game with:

```bash
./opentyrian2000 --remote-control --remote-socket=/tmp/tyrian3000-remote.sock
```

Included helper CLI:

```bash
./tools/gamectl.py launch
./tools/gamectl.py send-keys down down enter
./tools/gamectl.py screenshot /tmp/tyrian.bmp
./tools/gamectl.py game-state
./tools/gamectl.py stop
```

`gamectl` talks JSON over a Unix domain socket and supports:
- launch/stop lifecycle
- key and text injection
- frame waits
- debug-console command execution
- palette-correct RGB screenshots

### MCP wrapper (Claude/Codex tooling)

This repo also includes a stdio MCP wrapper:

```bash
./tools/game_mcp_server.py
```

It exposes tools:
- `game_launch`
- `game_stop`
- `game_ping`
- `game_state`
- `game_wait`
- `game_send_key`
- `game_send_keys`
- `game_send_text`
- `game_console`
- `game_screenshot`

Local Claude settings are pre-wired in `/Users/garyperrigo/Code/Tyrian3000/.claude/settings.local.json`.

## Controls

| Key | Action |
|-----|--------|
| Arrow keys | Ship movement |
| Space | Fire weapons |
| Enter | Toggle rear weapon mode |
| Ctrl / Alt | Fire left / right sidekick |
| Alt+Enter | Toggle fullscreen |

## Docs

Contributor and AI-agent notes live in:

- [docs/README.md](docs/README.md)

## Network Multiplayer

Networked games are initiated via the command line by both players simultaneously:

```
opentyrian2000 --net HOSTNAME --net-player-name NAME --net-player-number NUM
```

Uses UDP port 1333. UDP hole punching is supported, so port forwarding is
usually unnecessary.

## Lineage

```
Tyrian (1995, DOS)
 └─ OpenTyrian (open-source C port)
     └─ OpenTyrian2000 (Tyrian 2000 compatibility)
         └─ Tyrian 3000 ← you are here
```

## Links

- **Tyrian 3000**: https://github.com/garyperrigo/Tyrian3000
- **OpenTyrian2000**: https://github.com/KScl/opentyrian2000
- **OpenTyrian**: https://github.com/opentyrian/opentyrian
- **Tyrian 2000 data**: https://www.camanis.net/tyrian/tyrian2000.zip
- **Community forums**: https://tyrian2k.proboards.com/board/5

## License

Source code is licensed under the **GNU General Public License v2** (or later).
See [COPYING](COPYING) for the full license text.

The Tyrian 2000 game data files are freeware, released by the original author,
and are distributed separately from this source code.
