#!/usr/bin/env python3
"""Tyrian 3000 remote control helper."""

from __future__ import annotations

import argparse
import json
import os
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent.parent
STATE_PATH = Path("/tmp/tyrian3000-gamectl-state.json")
DEFAULT_SOCKET = "/tmp/tyrian3000-remote.sock"
DEFAULT_LOG = Path("/tmp/tyrian3000-game.log")


def read_state() -> dict[str, Any]:
    if not STATE_PATH.exists():
        return {}
    try:
        return json.loads(STATE_PATH.read_text())
    except Exception:
        return {}


def write_state(state: dict[str, Any]) -> None:
    STATE_PATH.write_text(json.dumps(state, indent=2))


def pid_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def call_remote(command: dict[str, Any], socket_path: str, timeout: float = 10.0) -> dict[str, Any]:
    payload = (json.dumps(command, separators=(",", ":")) + "\n").encode("utf-8")

    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
        sock.settimeout(timeout)
        sock.connect(socket_path)
        sock.sendall(payload)

        buf = bytearray()
        deadline = time.time() + timeout
        while True:
            if time.time() > deadline:
                raise TimeoutError("timed out waiting for response")
            chunk = sock.recv(4096)
            if not chunk:
                break
            buf.extend(chunk)
            if b"\n" in chunk:
                break

    line = buf.split(b"\n", 1)[0].decode("utf-8", errors="replace").strip()
    if not line:
        raise RuntimeError("empty response from game")
    data = json.loads(line)
    if not data.get("ok", False):
        raise RuntimeError(data.get("error", "command failed"))
    return data


def resolve_socket(cli_socket: str | None) -> str:
    if cli_socket:
        return cli_socket

    state = read_state()
    socket_path = state.get("socket")
    if not socket_path:
        return DEFAULT_SOCKET
    return str(socket_path)


def cmd_launch(args: argparse.Namespace) -> int:
    socket_path = resolve_socket(args.socket)
    try:
        os.unlink(socket_path)
    except FileNotFoundError:
        pass

    if args.build:
        target = "debug" if args.debug else "all"
        make_cmd = ["make", target, f"TYRIAN_DIR={args.data}"]
        subprocess.run(make_cmd, cwd=ROOT, check=True)

    binary = Path(args.binary)
    if not binary.is_absolute():
        binary = (ROOT / binary).resolve()
    if not binary.exists():
        raise FileNotFoundError(f"binary not found: {binary}")

    log_path = Path(args.log)
    log_file = log_path.open("ab")

    cmd = [
        str(binary),
        "--remote-control",
        f"--remote-socket={socket_path}",
        f"--data={args.data}",
    ]
    if args.start_menu == "setup":
        cmd.append("--start-setup-menu")
    elif args.start_menu == "graphics":
        cmd.append("--start-graphics-menu")
    if args.start_menu_option:
        cmd.append(f"--start-menu-option={args.start_menu_option}")
    if args.start_menu_enter:
        cmd.append("--start-menu-enter")
    cmd.extend(args.extra_args)

    proc = subprocess.Popen(  # noqa: S603
        cmd,
        cwd=ROOT,
        stdout=log_file,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )

    state = {
        "pid": proc.pid,
        "socket": socket_path,
        "binary": str(binary),
        "data": args.data,
        "log": str(log_path),
        "launched_at": int(time.time()),
    }
    write_state(state)

    deadline = time.time() + args.wait_start
    last_err: Exception | None = None
    while time.time() < deadline:
        if not pid_alive(proc.pid):
            break
        try:
            call_remote({"cmd": "ping"}, socket_path, timeout=1.0)
            print(json.dumps(state, indent=2))
            return 0
        except Exception as err:  # noqa: BLE001
            last_err = err
            time.sleep(0.1)

    raise RuntimeError(f"game did not become ready: {last_err}")


def cmd_stop(args: argparse.Namespace) -> int:
    state = read_state()
    pid = state.get("pid")
    socket_path = resolve_socket(args.socket)

    try:
        call_remote({"cmd": "quit"}, socket_path, timeout=2.0)
    except Exception:
        pass

    if isinstance(pid, int) and pid_alive(pid):
        deadline = time.time() + args.wait
        while time.time() < deadline and pid_alive(pid):
            time.sleep(0.1)
        if pid_alive(pid):
            os.kill(pid, signal.SIGTERM)
            time.sleep(0.2)
        if pid_alive(pid):
            os.kill(pid, signal.SIGKILL)

    if STATE_PATH.exists():
        STATE_PATH.unlink()

    return 0


def cmd_ping(args: argparse.Namespace) -> int:
    socket_path = resolve_socket(args.socket)
    data = call_remote({"cmd": "ping"}, socket_path, timeout=args.timeout)
    print(json.dumps(data))
    return 0


def cmd_game_state(args: argparse.Namespace) -> int:
    socket_path = resolve_socket(args.socket)
    data = call_remote({"cmd": "get_state"}, socket_path, timeout=args.timeout)
    print(json.dumps(data, indent=2))
    return 0


def cmd_wait(args: argparse.Namespace) -> int:
    socket_path = resolve_socket(args.socket)
    data = call_remote({"cmd": "wait_frames", "frames": args.frames}, socket_path, timeout=args.timeout)
    print(json.dumps(data))
    return 0


def cmd_screenshot(args: argparse.Namespace) -> int:
    socket_path = resolve_socket(args.socket)
    out = str(Path(args.path).resolve())
    data = call_remote({"cmd": "screenshot", "path": out}, socket_path, timeout=args.timeout)
    print(json.dumps(data))
    return 0


def cmd_send_key(args: argparse.Namespace) -> int:
    socket_path = resolve_socket(args.socket)
    for _ in range(args.repeat):
        call_remote({"cmd": "send_key", "key": args.key, "action": args.action}, socket_path, timeout=args.timeout)
        if args.wait_between > 0:
            call_remote({"cmd": "wait_frames", "frames": args.wait_between}, socket_path, timeout=args.timeout)
    print("{\"ok\":true}")
    return 0


def cmd_send_keys(args: argparse.Namespace) -> int:
    socket_path = resolve_socket(args.socket)
    for key in args.keys:
        call_remote({"cmd": "send_key", "key": key, "action": "tap"}, socket_path, timeout=args.timeout)
        if args.wait_between > 0:
            call_remote({"cmd": "wait_frames", "frames": args.wait_between}, socket_path, timeout=args.timeout)
    print("{\"ok\":true}")
    return 0


def cmd_send_text(args: argparse.Namespace) -> int:
    socket_path = resolve_socket(args.socket)
    data = call_remote({"cmd": "send_text", "text": args.text}, socket_path, timeout=args.timeout)
    print(json.dumps(data))
    return 0


def cmd_console(args: argparse.Namespace) -> int:
    socket_path = resolve_socket(args.socket)
    data = call_remote({"cmd": "console_exec", "command": args.command}, socket_path, timeout=args.timeout)
    print(json.dumps(data, indent=2))
    return 0


def cmd_local_state(_: argparse.Namespace) -> int:
    print(json.dumps(read_state(), indent=2))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Tyrian 3000 remote control tool")
    sub = parser.add_subparsers(dest="subcommand", required=True)

    launch = sub.add_parser("launch", help="build and launch the game with remote control enabled")
    launch.add_argument("--socket", default=None)
    launch.add_argument("--binary", default="./opentyrian2000")
    launch.add_argument("--data", default="data/tyrian2000")
    launch.add_argument("--log", default=str(DEFAULT_LOG))
    launch.add_argument("--wait-start", type=float, default=60.0)
    launch.add_argument("--start-menu", choices=["title", "setup", "graphics"], default="title")
    launch.add_argument("--start-menu-option", default=None)
    launch.add_argument("--start-menu-enter", action="store_true")
    launch.add_argument("--debug", action=argparse.BooleanOptionalAction, default=False)
    launch.add_argument("--build", action=argparse.BooleanOptionalAction, default=True)
    launch.add_argument("extra_args", nargs=argparse.REMAINDER, help="extra args passed to the game")
    launch.set_defaults(func=cmd_launch)

    stop = sub.add_parser("stop", help="stop the running game")
    stop.add_argument("--socket", default=None)
    stop.add_argument("--wait", type=float, default=2.0)
    stop.set_defaults(func=cmd_stop)

    ping = sub.add_parser("ping", help="ping the game")
    ping.add_argument("--socket", default=None)
    ping.add_argument("--timeout", type=float, default=5.0)
    ping.set_defaults(func=cmd_ping)

    game_state = sub.add_parser("game-state", help="query game state from the remote server")
    game_state.add_argument("--socket", default=None)
    game_state.add_argument("--timeout", type=float, default=5.0)
    game_state.set_defaults(func=cmd_game_state)

    wait = sub.add_parser("wait", help="wait for N rendered frames")
    wait.add_argument("frames", type=int)
    wait.add_argument("--socket", default=None)
    wait.add_argument("--timeout", type=float, default=20.0)
    wait.set_defaults(func=cmd_wait)

    screenshot = sub.add_parser("screenshot", help="capture a BMP screenshot")
    screenshot.add_argument("path")
    screenshot.add_argument("--socket", default=None)
    screenshot.add_argument("--timeout", type=float, default=20.0)
    screenshot.set_defaults(func=cmd_screenshot)

    send_key = sub.add_parser("send-key", help="send one key input")
    send_key.add_argument("key")
    send_key.add_argument("--action", choices=["tap", "down", "up"], default="tap")
    send_key.add_argument("--repeat", type=int, default=1)
    send_key.add_argument("--wait-between", type=int, default=0)
    send_key.add_argument("--socket", default=None)
    send_key.add_argument("--timeout", type=float, default=10.0)
    send_key.set_defaults(func=cmd_send_key)

    send_keys = sub.add_parser("send-keys", help="send a sequence of key taps")
    send_keys.add_argument("keys", nargs="+")
    send_keys.add_argument("--wait-between", type=int, default=0)
    send_keys.add_argument("--socket", default=None)
    send_keys.add_argument("--timeout", type=float, default=10.0)
    send_keys.set_defaults(func=cmd_send_keys)

    send_text = sub.add_parser("send-text", help="send text input")
    send_text.add_argument("text")
    send_text.add_argument("--socket", default=None)
    send_text.add_argument("--timeout", type=float, default=5.0)
    send_text.set_defaults(func=cmd_send_text)

    console = sub.add_parser("console", help="run a debug-console command in-process")
    console.add_argument("command")
    console.add_argument("--socket", default=None)
    console.add_argument("--timeout", type=float, default=5.0)
    console.set_defaults(func=cmd_console)

    local_state = sub.add_parser("local-state", help="print local launcher state")
    local_state.set_defaults(func=cmd_local_state)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return int(args.func(args))
    except KeyboardInterrupt:
        return 130
    except Exception as err:  # noqa: BLE001
        print(f"error: {err}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
