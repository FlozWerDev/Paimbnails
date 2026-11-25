#!/usr/bin/env python3
"""Stdio-to-Streamable-HTTP MCP bridge for Geometry Dash Editor MCP (Paimbnails)."""

import json
import os
import sys
import urllib.error
import urllib.request

MCP_URL = os.environ.get("GD_MCP_URL", "http://127.0.0.1:21690/mcp")
SESSION_ID = None


def forward(line: str) -> None:
    global SESSION_ID
    headers = {
        "Content-Type": "application/json",
        "Accept": "application/json, text/event-stream",
    }
    if SESSION_ID:
        headers["Mcp-Session-Id"] = SESSION_ID
    req = urllib.request.Request(
        MCP_URL,
        data=line.encode("utf-8"),
        headers=headers,
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            sid = resp.headers.get("Mcp-Session-Id")
            if sid:
                SESSION_ID = sid
            body = resp.read().decode("utf-8").strip()
            if body:
                sys.stdout.write(body + "\n")
                sys.stdout.flush()
    except urllib.error.HTTPError as e:
        err_body = e.read().decode("utf-8", errors="replace").strip()
        if err_body:
            sys.stdout.write(err_body + "\n")
            sys.stdout.flush()
    except Exception as ex:
        try:
            msg = json.loads(line)
            rid = msg.get("id")
        except Exception:
            rid = None
        if rid is not None:
            out = {
                "jsonrpc": "2.0",
                "id": rid,
                "error": {
                    "code": -32000,
                    "message": f"GD MCP server unreachable at {MCP_URL}: {ex}",
                },
            }
            sys.stdout.write(json.dumps(out) + "\n")
            sys.stdout.flush()


def main() -> None:
    for raw in sys.stdin:
        line = raw.strip()
        if line:
            forward(line)


if __name__ == "__main__":
    main()
