#!/usr/bin/env python3

import argparse
import socket
import struct
import threading
from typing import List, Tuple


HEADER_STRUCT = struct.Struct("!III")


def recv_exact(sock: socket.socket, size: int) -> bytes:
    chunks = bytearray()
    while len(chunks) < size:
        chunk = sock.recv(size - len(chunks))
        if not chunk:
            raise ConnectionError("socket closed while receiving data")
        chunks.extend(chunk)
    return bytes(chunks)


def send_frame(sock: socket.socket, cmd: int, request_id: int, body: str) -> None:
    body_bytes = body.encode("utf-8")
    sock.sendall(HEADER_STRUCT.pack(cmd, request_id, len(body_bytes)) + body_bytes)


def recv_frame(sock: socket.socket) -> Tuple[int, int, str]:
    cmd, request_id, body_len = HEADER_STRUCT.unpack(recv_exact(sock, HEADER_STRUCT.size))
    body = recv_exact(sock, body_len).decode("utf-8")
    return cmd, request_id, body


def worker(host: str,
           port: int,
           index: int,
           requests_per_client: int,
           results: List[Tuple[int, bool, str]]) -> None:
    try:
        with socket.create_connection((host, port), timeout=3) as sock:
            sock.settimeout(3)
            for offset in range(requests_per_client):
                cmd = 100 + index
                request_id = index * 1000 + offset
                body = f"client-{index}-req-{offset}"
                send_frame(sock, cmd, request_id, body)
                resp_cmd, resp_request_id, resp_body = recv_frame(sock)

                ok = (
                    resp_cmd == cmd
                    and resp_request_id == request_id
                    and f"request_id={request_id}" in resp_body
                    and f"body={body}" in resp_body
                )
                if not ok:
                    results[index] = (
                        index,
                        False,
                        f"unexpected response: cmd={resp_cmd}, request_id={resp_request_id}, body={resp_body}",
                    )
                    return

        results[index] = (index, True, f"{requests_per_client} requests matched")
    except Exception as exc:  # pylint: disable=broad-except
        results[index] = (index, False, str(exc))


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify TinyIM concurrent request/response framing.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9999)
    parser.add_argument("--clients", type=int, default=10)
    parser.add_argument("--requests", type=int, default=5)
    args = parser.parse_args()

    results: List[Tuple[int, bool, str]] = [(-1, False, "not started")] * args.clients
    threads = [
        threading.Thread(
            target=worker,
            args=(args.host, args.port, index, args.requests, results),
            daemon=True,
        )
        for index in range(args.clients)
    ]

    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()

    for result in results:
        print(result)

    return 0 if all(ok for _, ok, _ in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
