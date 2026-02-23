#!/usr/bin/env python3
"""MCP wrapper for Tyrian 3000 remote control."""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parent.parent
GAMECTL = ROOT / "tools" / "gamectl.py"
SERVER_NAME = "tyrian3000-game"
SERVER_VERSION = "0.1.0"


TOOLS: list[dict[str, Any]] = [
    {
        "name": "game_launch",
        "description": "Build (optional) and launch Tyrian 3000 with remote control enabled.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "build": {"type": "boolean", "default": True},
                "data": {"type": "string", "default": "data/tyrian2000"},
                "binary": {"type": "string", "default": "./opentyrian2000"},
                "wait_start": {"type": "number", "default": 60.0},
            },
        },
    },
    {
        "name": "game_stop",
        "description": "Stop the running Tyrian 3000 process started by gamectl.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "game_ping",
        "description": "Ping the game remote-control server.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "game_state",
        "description": "Get current game remote-control state snapshot.",
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "game_wait",
        "description": "Wait N rendered frames.",
        "inputSchema": {
            "type": "object",
            "properties": {"frames": {"type": "integer", "minimum": 1}},
            "required": ["frames"],
        },
    },
    {
        "name": "game_send_key",
        "description": "Send one key to the game.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "key": {"type": "string"},
                "action": {"type": "string", "enum": ["tap", "down", "up"], "default": "tap"},
                "repeat": {"type": "integer", "minimum": 1, "default": 1},
                "wait_between": {"type": "integer", "minimum": 0, "default": 0},
            },
            "required": ["key"],
        },
    },
    {
        "name": "game_send_keys",
        "description": "Send a sequence of key taps to the game.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "keys": {"type": "array", "items": {"type": "string"}, "minItems": 1},
                "wait_between": {"type": "integer", "minimum": 0, "default": 0},
            },
            "required": ["keys"],
        },
    },
    {
        "name": "game_send_text",
        "description": "Send text input to the game (SDL text event).",
        "inputSchema": {
            "type": "object",
            "properties": {"text": {"type": "string"}},
            "required": ["text"],
        },
    },
    {
        "name": "game_console",
        "description": "Execute a debug-console command in game.",
        "inputSchema": {
            "type": "object",
            "properties": {"command": {"type": "string"}},
            "required": ["command"],
        },
    },
    {
        "name": "game_screenshot",
        "description": "Capture a screenshot to an absolute or workspace-relative path.",
        "inputSchema": {
            "type": "object",
            "properties": {"path": {"type": "string"}},
            "required": ["path"],
        },
    },
]


def send_message(msg: dict[str, Any]) -> None:
    body = json.dumps(msg, separators=(",", ":")).encode("utf-8")
    sys.stdout.buffer.write(f"Content-Length: {len(body)}\r\n\r\n".encode("ascii"))
    sys.stdout.buffer.write(body)
    sys.stdout.buffer.flush()


def read_message() -> dict[str, Any] | None:
    headers: dict[str, str] = {}
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            return None
        if line in (b"\r\n", b"\n"):
            break
        if b":" not in line:
            continue
        key, value = line.split(b":", 1)
        headers[key.decode("ascii").strip().lower()] = value.decode("ascii").strip()

    length_raw = headers.get("content-length")
    if not length_raw:
        return None
    length = int(length_raw)
    payload = sys.stdin.buffer.read(length)
    if not payload:
        return None
    return json.loads(payload.decode("utf-8"))


def ok_result(req_id: Any, result: dict[str, Any]) -> None:
    send_message({"jsonrpc": "2.0", "id": req_id, "result": result})


def err_result(req_id: Any, code: int, message: str) -> None:
    send_message({"jsonrpc": "2.0", "id": req_id, "error": {"code": code, "message": message}})


def text_tool_result(text: str, is_error: bool = False) -> dict[str, Any]:
    result: dict[str, Any] = {"content": [{"type": "text", "text": text}]}
    if is_error:
        result["isError"] = True
    return result


def run_gamectl(args: list[str]) -> str:
    cmd = [str(GAMECTL)] + args
    proc = subprocess.run(  # noqa: S603
        cmd,
        cwd=ROOT,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    out = proc.stdout.strip()
    err = proc.stderr.strip()
    if proc.returncode != 0:
        detail = err or out or f"exit code {proc.returncode}"
        raise RuntimeError(detail)
    return out or "{}"


def handle_tool_call(name: str, args: dict[str, Any]) -> str:
    if name == "game_launch":
        argv = ["launch"]
        if not bool(args.get("build", True)):
            argv.append("--no-build")
        if "data" in args:
            argv.extend(["--data", str(args["data"])])
        if "binary" in args:
            argv.extend(["--binary", str(args["binary"])])
        if "wait_start" in args:
            argv.extend(["--wait-start", str(args["wait_start"])])
        return run_gamectl(argv)

    if name == "game_stop":
        return run_gamectl(["stop"])

    if name == "game_ping":
        return run_gamectl(["ping"])

    if name == "game_state":
        return run_gamectl(["game-state"])

    if name == "game_wait":
        return run_gamectl(["wait", str(int(args["frames"]))])

    if name == "game_send_key":
        argv = ["send-key", str(args["key"])]
        if "action" in args:
            argv.extend(["--action", str(args["action"])])
        if "repeat" in args:
            argv.extend(["--repeat", str(int(args["repeat"]))])
        if "wait_between" in args:
            argv.extend(["--wait-between", str(int(args["wait_between"]))])
        return run_gamectl(argv)

    if name == "game_send_keys":
        keys = [str(k) for k in args["keys"]]
        argv = ["send-keys", *keys]
        if "wait_between" in args:
            argv.extend(["--wait-between", str(int(args["wait_between"]))])
        return run_gamectl(argv)

    if name == "game_send_text":
        return run_gamectl(["send-text", str(args["text"])])

    if name == "game_console":
        return run_gamectl(["console", str(args["command"])])

    if name == "game_screenshot":
        return run_gamectl(["screenshot", str(args["path"])])

    raise RuntimeError(f"unknown tool: {name}")


def handle_request(msg: dict[str, Any]) -> None:
    method = msg.get("method")
    req_id = msg.get("id")

    if method == "initialize":
        ok_result(
            req_id,
            {
                "protocolVersion": "2024-11-05",
                "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
                "capabilities": {"tools": {}},
            },
        )
        return

    if method == "notifications/initialized":
        return

    if method == "tools/list":
        ok_result(req_id, {"tools": TOOLS})
        return

    if method == "tools/call":
        params = msg.get("params", {})
        name = str(params.get("name", ""))
        arguments = params.get("arguments", {}) or {}
        if not isinstance(arguments, dict):
            err_result(req_id, -32602, "arguments must be an object")
            return

        try:
            out = handle_tool_call(name, arguments)
            ok_result(req_id, text_tool_result(out))
        except Exception as err:  # noqa: BLE001
            ok_result(req_id, text_tool_result(f"error: {err}", is_error=True))
        return

    if req_id is not None:
        err_result(req_id, -32601, f"method not found: {method}")


def main() -> int:
    while True:
        msg = read_message()
        if msg is None:
            return 0
        handle_request(msg)


if __name__ == "__main__":
    raise SystemExit(main())
