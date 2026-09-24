#!/usr/bin/env python3
"""Read-only KSEM/Sunny Island phase logger for an A/B meter packet experiment."""
import argparse
import csv
import datetime as dt
import socket
import struct
import sys
import time


def receive(sock, size):
    data = b""
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise OSError("Modbus connection closed")
        data += chunk
    return data


def registers(host, port, unit, start, count):
    request = struct.pack(">HHHBBHH", 1, 0, 6, unit, 3, start, count)
    with socket.create_connection((host, port), timeout=1.5) as sock:
        sock.settimeout(1.5)
        sock.sendall(request)
        head = receive(sock, 9)
        tx, proto, length, response_unit, function, byte_count = struct.unpack(">HHHBBB", head)
        if (tx, proto, response_unit, function, byte_count) != (1, 0, unit, 3, count * 2):
            raise ValueError(f"invalid Modbus response from {host}: {head.hex()}")
        if length != 3 + byte_count:
            raise ValueError(f"invalid Modbus length from {host}: {length}")
        return struct.unpack(">" + "H" * count, receive(sock, byte_count))


def u32(values, offset):
    return values[offset] << 16 | values[offset + 1]


def s32(values, offset):
    return struct.unpack(">i", struct.pack(">I", u32(values, offset)))[0]


def sample(args):
    # All three instantaneous phase blocks fit in one 106-register request.
    ksem = registers(args.ksem, args.ksem_port, args.ksem_unit, 40, 106)
    row = [dt.datetime.now(dt.timezone.utc).isoformat(timespec="milliseconds")]
    for p in range(3):
        offset = p * 40
        row.extend((
            (u32(ksem, offset) - u32(ksem, offset + 2)) / 10,
            (-1 if u32(ksem, offset + 2) > u32(ksem, offset) else 1)
            * u32(ksem, offset + 20) / 1000,
            u32(ksem, offset + 22) / 1000,
        ))
    for host in args.si:
        try:
            row.append(s32(registers(host, args.si_port, args.si_unit, 30775, 2), 0))
        except (OSError, ValueError) as exc:
            print(f"{host}: {exc}", file=sys.stderr)
            row.append("")
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ksem", required=True)
    parser.add_argument("--ksem-port", type=int, default=502)
    parser.add_argument("--ksem-unit", type=int, default=71)
    parser.add_argument("--si", action="append", default=[], help="Repeat for master, slave 1, slave 2 IPs")
    parser.add_argument("--si-port", type=int, default=502)
    parser.add_argument("--si-unit", type=int, default=3)
    parser.add_argument("--samples", type=int, default=120)
    parser.add_argument("--interval", type=float, default=1.0)
    args = parser.parse_args()
    if len(args.si) not in (0, 3) or args.samples < 1 or args.interval <= 0:
        parser.error("provide zero or exactly three --si IPs, positive samples and interval")
    writer = csv.writer(sys.stdout)
    writer.writerow(["timestamp_utc"] + [
        f"l{p}_{field}" for p in range(1, 4)
        for field in ("net_w", "signed_a", "voltage_v")
    ] + [f"si{p}_power_w" for p in range(1, len(args.si) + 1)])
    for n in range(args.samples):
        start = time.monotonic()
        try:
            writer.writerow(sample(args))
            sys.stdout.flush()
        except (OSError, ValueError) as exc:
            print(f"sample {n + 1}: {exc}", file=sys.stderr)
        time.sleep(max(0, args.interval - (time.monotonic() - start)))


if __name__ == "__main__":
    main()
