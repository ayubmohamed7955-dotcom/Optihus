#!/usr/bin/env python3
import argparse
import socket
import sys
from datetime import datetime, timezone


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds")


def run_server(bind_host: str, port: int, expected: str, reply: str) -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((bind_host, port))
        s.listen(1)
        print(f"[{utc_now()}] server listening on {bind_host}:{port}")
        conn, addr = s.accept()
        with conn:
            print(f"[{utc_now()}] accepted connection from {addr[0]}:{addr[1]}")
            data = conn.recv(4096)
            if not data:
                print(f"[{utc_now()}] no payload received", file=sys.stderr)
                return 2
            text = data.decode("utf-8", errors="replace").strip()
            print(f"[{utc_now()}] RX: {text!r}")
            if text != expected:
                print(
                    f"[{utc_now()}] unexpected payload, expected {expected!r}",
                    file=sys.stderr,
                )
                return 3
            wire = (reply + "\n").encode("utf-8")
            conn.sendall(wire)
            print(f"[{utc_now()}] TX: {reply!r}")
    return 0


def run_client(host: str, port: int, msg: str) -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        print(f"[{utc_now()}] connecting to {host}:{port}")
        s.connect((host, port))
        wire = (msg + "\n").encode("utf-8")
        s.sendall(wire)
        print(f"[{utc_now()}] TX: {msg!r}")
        data = s.recv(4096)
        if not data:
            print(f"[{utc_now()}] no reply received", file=sys.stderr)
            return 4
        text = data.decode("utf-8", errors="replace").strip()
        print(f"[{utc_now()}] RX: {text!r}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="AP TCP smoke test (PING/PONG).")
    sub = parser.add_subparsers(dest="mode", required=True)

    s = sub.add_parser("server", help="Run simple TCP test server")
    s.add_argument("--bind", default="0.0.0.0", help="Bind IP")
    s.add_argument("--port", type=int, default=5050, help="Listen port")
    s.add_argument("--expect", default="PING", help="Expected incoming message")
    s.add_argument("--reply", default="PONG", help="Reply message")

    c = sub.add_parser("client", help="Run simple TCP test client")
    c.add_argument("--host", default="192.168.4.1", help="Server IP")
    c.add_argument("--port", type=int, default=5050, help="Server port")
    c.add_argument("--msg", default="PING", help="Payload to send")

    args = parser.parse_args()
    if args.mode == "server":
        return run_server(args.bind, args.port, args.expect, args.reply)
    return run_client(args.host, args.port, args.msg)


if __name__ == "__main__":
    raise SystemExit(main())
