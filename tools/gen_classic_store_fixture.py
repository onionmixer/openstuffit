#!/usr/bin/env python3
import argparse
import struct


def crc16_ibm(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
            crc &= 0xFFFF
    return crc


def build(path_bytes, payload):
    if not 1 <= len(path_bytes) <= 31:
        raise ValueError("classic SIT fixture name must be 1..31 bytes")
    archive_len = 22 + 112 + len(payload)
    archive = bytearray()
    archive += b"SIT!"
    archive += struct.pack(">H", 1)
    archive += struct.pack(">I", archive_len)
    archive += b"rLau"
    archive += bytes([2, 0])
    archive += b"\0" * 6

    entry = bytearray(112)
    entry[0] = 0
    entry[1] = 0
    entry[2] = len(path_bytes)
    entry[3:3 + len(path_bytes)] = path_bytes
    entry[66:70] = b"TEXT"
    entry[70:74] = b"ttxt"
    entry[84:88] = struct.pack(">I", 0)
    entry[88:92] = struct.pack(">I", len(payload))
    entry[92:96] = struct.pack(">I", 0)
    entry[96:100] = struct.pack(">I", len(payload))
    entry[100:102] = struct.pack(">H", 0)
    entry[102:104] = struct.pack(">H", crc16_ibm(payload))
    archive += entry
    archive += payload
    return archive


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("archive")
    parser.add_argument("name")
    parser.add_argument("--payload", default="path safety payload\n")
    args = parser.parse_args()
    data = build(args.name.encode("utf-8"), args.payload.encode("utf-8"))
    with open(args.archive, "wb") as fp:
        fp.write(data)


if __name__ == "__main__":
    main()
