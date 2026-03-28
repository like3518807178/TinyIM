#!/usr/bin/env python3

import argparse
import socket
import struct
import threading
from typing import Dict, List, Tuple


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


def build_request(index: int, offset: int) -> Tuple[int, int, str]:
    cmd = 100 + index
    request_id = index * 1000 + offset
    body = f"client-{index}-req-{offset}"
    return cmd, request_id, body


def validate_response(expected: Tuple[int, int, str],
                      actual: Tuple[int, int, str]) -> Tuple[bool, str]:
    exp_cmd, exp_request_id, exp_body = expected
    act_cmd, act_request_id, act_body = actual
    ok = (
        act_cmd == exp_cmd
        and act_request_id == exp_request_id
        and f"request_id={exp_request_id}" in act_body
        and f"body={exp_body}" in act_body
    )
    if ok:
        return True, ""

    return (
        False,
        "unexpected response: "
        f"expected=(cmd={exp_cmd}, request_id={exp_request_id}, body={exp_body}), "
        f"actual=(cmd={act_cmd}, request_id={act_request_id}, body={act_body})",
    )


def worker(host: str,
           port: int,
           index: int,
           requests_per_client: int,
           burst_size: int,
           results: List[Tuple[int, bool, str]]) -> None:
    try:
        with socket.create_connection((host, port), timeout=3) as sock:
            sock.settimeout(3)
            pending: Dict[int, Tuple[int, int, str]] = {}
            next_to_send = 0
            next_to_recv = 0

            while next_to_recv < requests_per_client:
                while next_to_send < requests_per_client and len(pending) < burst_size:
                    request = build_request(index, next_to_send)
                    send_frame(sock, request[0], request[1], request[2])
                    pending[request[1]] = request
                    next_to_send += 1

                response = recv_frame(sock)
                expected = pending.pop(response[1], None)
                if expected is None:
                    results[index] = (
                        index,
                        False,
                        f"unexpected response request_id={response[1]} with no pending request",
                    )
                    return

                ok, message = validate_response(expected, response)
                if not ok:
                    results[index] = (index, False, message)
                    return

                next_to_recv += 1

        results[index] = (index, True, f"{requests_per_client} requests matched")
    except Exception as exc:  # pylint: disable=broad-except
        results[index] = (index, False, str(exc))


def main() -> int:
    parser = argparse.ArgumentParser(description="Verify TinyIM concurrent request/response framing.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9999)
    parser.add_argument("--clients", type=int, default=10)
    parser.add_argument("--requests", type=int, default=5)
    parser.add_argument("--burst", type=int, default=1,
                        help="Number of in-flight requests allowed per connection.")
    args = parser.parse_args()

    if args.clients <= 0:
        raise SystemExit("--clients must be > 0")
    if args.requests <= 0:
        raise SystemExit("--requests must be > 0")
    if args.burst <= 0:
        raise SystemExit("--burst must be > 0")

    results: List[Tuple[int, bool, str]] = [(-1, False, "not started")] * args.clients
    threads = [
        threading.Thread(
            target=worker,
            args=(args.host, args.port, index, args.requests, args.burst, results),
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

    passed = sum(1 for _, ok, _ in results if ok)
    print(f"SUMMARY: passed={passed}/{args.clients}, requests_per_client={args.requests}, burst={args.burst}")

    return 0 if passed == args.clients else 1


if __name__ == "__main__":
    raise SystemExit(main())
