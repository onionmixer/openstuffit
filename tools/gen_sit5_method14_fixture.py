#!/usr/bin/env python3
import argparse
import struct
import zlib


SIG = b"StuffIt (c)1997-2002 Aladdin Systems, Inc., http://www.aladdinsys.com/StuffIt/\r\n"
MAC_TIME = 0xD256A35A


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


def u16(value):
    return struct.pack(">H", value)


def u32(value):
    return struct.pack(">I", value)


def raw_deflate(data):
    co = zlib.compressobj(level=6, wbits=-15)
    return co.compress(data) + co.flush()


def archive_header(root_entries=1):
    data = bytearray()
    data += SIG[:80].ljust(80, b"\0")
    data += b"\x1a\x00"
    data += b"\x05\x10"
    total_pos = len(data)
    data += b"\0" * 4
    first_pos = len(data)
    data += b"\0" * 4
    data += u16(root_entries)
    repeat_first_pos = len(data)
    data += b"\0" * 4
    data += u16(0x009B)
    data += b"\xa5\xa5"
    data += b"Kestrel Sit5 Archive"
    del data[114:]
    data[first_pos:first_pos + 4] = u32(114)
    data[repeat_first_pos:repeat_first_pos + 4] = u32(114)
    return data, total_pos


def entry_prefix(flags, parent_offset, name, data_len, data_compressed_len, data_crc):
    entry = bytearray()
    entry += u32(0xA5A5A5A5)
    entry += b"\x01\x00"
    entry += b"\0\0"
    entry += b"\x00"
    entry += bytes([flags])
    entry += u32(MAC_TIME)
    entry += u32(MAC_TIME)
    entry += u32(0)
    entry += u32(0)
    entry += u32(parent_offset)
    entry += u16(len(name))
    entry += u16(0)
    entry += u32(data_len)
    entry += u32(data_compressed_len)
    entry += u16(data_crc)
    entry += u16(0)
    return entry


def finalize_entry(entry):
    entry[6:8] = u16(len(entry))
    entry[32:34] = u16(crc16_ibm(entry[:32]))
    return entry


def append_finder(data, file_type=b"TEXT", creator=b"ttxt", meta=0):
    data += u16(meta)
    data += u16(0)
    data += file_type
    data += creator
    data += u16(0)
    data += b"\0" * 22


def build_flat():
    name = b"method14.txt"
    plain = b"Hello, Deflate! This text should be compressed using StuffIt 5 method 14.\n"
    compressed = raw_deflate(plain)
    data, total_pos = archive_header()
    entry_start = len(data)
    entry = entry_prefix(0, 0, name, len(plain), len(compressed), crc16_ibm(plain))
    entry += b"\x0e\x00"
    entry += name
    data += finalize_entry(entry)
    append_finder(data)
    stream_start = len(data)
    data += compressed
    data[total_pos:total_pos + 4] = u32(len(data))
    return data, {"plain": plain, "entry_start": entry_start, "stream_start": stream_start}


def build_nested():
    folder_name = b"folder"
    file_name = b"method14_nested.txt"
    plain = b"Nested SIT5 method 14 payload. " * 4 + b"\n"
    compressed = raw_deflate(plain)
    data, total_pos = archive_header()

    folder_offset = len(data)
    folder = entry_prefix(0x40, 0, folder_name, 0, 0, 0)
    folder += u16(1)
    folder += folder_name
    data += finalize_entry(folder)
    append_finder(data, b"\0" * 4, b"\0" * 4)

    file_entry = entry_prefix(0, folder_offset, file_name, len(plain), len(compressed), crc16_ibm(plain))
    file_entry += b"\x0e\x00"
    file_entry += file_name
    data += finalize_entry(file_entry)
    append_finder(data)
    data += compressed
    data[total_pos:total_pos + 4] = u32(len(data))
    return data, {"plain": plain}


def build_dir_sentinel():
    folder_name = b"dir"
    file1_name = b"a.txt"
    file2_name = b"b.txt"
    file1_plain = b"alpha\n"
    file2_plain = b"beta\n"
    file1_compressed = raw_deflate(file1_plain)
    file2_compressed = raw_deflate(file2_plain)
    data, total_pos = archive_header()

    folder_offset = len(data)
    folder = entry_prefix(0x40, 0, folder_name, 0, 0, 0)
    folder += u16(2)
    folder += folder_name
    data += finalize_entry(folder)
    append_finder(data, b"\0" * 4, b"\0" * 4)

    sentinel_offset = len(data)
    sentinel = entry_prefix(0x40, folder_offset, b"", 0xFFFFFFFF, 0, 0)
    sentinel += u16(0)
    data += finalize_entry(sentinel)

    file1 = entry_prefix(0, folder_offset, file1_name, len(file1_plain), len(file1_compressed), crc16_ibm(file1_plain))
    file1 += b"\x0e\x00"
    file1 += file1_name
    data += finalize_entry(file1)
    append_finder(data)
    data += file1_compressed

    file2 = entry_prefix(0, folder_offset, file2_name, len(file2_plain), len(file2_compressed), crc16_ibm(file2_plain))
    file2 += b"\x0e\x00"
    file2 += file2_name
    data += finalize_entry(file2)
    append_finder(data)
    data += file2_compressed

    data[total_pos:total_pos + 4] = u32(len(data))
    return data, {
        "file1_plain": file1_plain,
        "file2_plain": file2_plain,
        "sentinel_offset": sentinel_offset,
    }


def build_rsrc():
    name = b"method14_rsrc.txt"
    plain = b"Data fork stored without compression.\n"
    resource = b"Resource fork compressed with SIT5 method 14. " * 5
    resource_compressed = raw_deflate(resource)
    data, total_pos = archive_header()

    entry = entry_prefix(0, 0, name, len(plain), len(plain), crc16_ibm(plain))
    entry += b"\x00\x00"
    entry += name
    data += finalize_entry(entry)
    append_finder(data, meta=1)
    data += u32(len(resource))
    data += u32(len(resource_compressed))
    rcrc_pos = len(data)
    data += u16(crc16_ibm(resource))
    data += u16(0)
    data += b"\x0e\x00"
    stream_start = len(data)
    data += resource_compressed
    data += plain
    data[total_pos:total_pos + 4] = u32(len(data))
    return data, {"plain": plain, "resource": resource, "stream_start": stream_start, "rcrc_pos": rcrc_pos}


def apply_variant(data, meta, variant):
    if variant == "valid":
        return data
    out = bytearray(data)
    if variant == "truncated":
        return out[:-1]
    if variant == "bad-stream":
        out[meta["stream_start"]] ^= 0xFF
        return out
    if variant == "bad-ulen":
        entry_start = meta["entry_start"]
        out[entry_start + 34:entry_start + 38] = u32(len(meta["plain"]) + 1)
        return out
    if variant == "bad-rsrc-stream":
        out[meta["stream_start"]] ^= 0xFF
        return out
    if variant == "bad-rsrc-crc":
        crc = (crc16_ibm(meta["resource"]) + 1) & 0xFFFF
        out[meta["rcrc_pos"]:meta["rcrc_pos"] + 2] = u16(crc)
        return out
    raise ValueError("unsupported variant: " + variant)


def write_file(path, data):
    if path:
        with open(path, "wb") as fp:
            fp.write(data)


def selftest():
    flat, flat_meta = build_flat()
    assert flat[:7] == b"StuffIt"
    assert struct.unpack(">H", flat[92:94])[0] == 1
    assert struct.unpack(">I", flat[94:98])[0] == 114
    assert flat[114:118] == b"\xa5\xa5\xa5\xa5"
    assert flat[flat_meta["stream_start"]] != 0
    assert len(apply_variant(flat, flat_meta, "truncated")) == len(flat) - 1
    assert apply_variant(flat, flat_meta, "bad-stream")[flat_meta["stream_start"]] != flat[flat_meta["stream_start"]]
    bad_ulen = apply_variant(flat, flat_meta, "bad-ulen")
    assert struct.unpack(">I", bad_ulen[flat_meta["entry_start"] + 34:flat_meta["entry_start"] + 38])[0] == len(flat_meta["plain"]) + 1

    nested, _ = build_nested()
    assert nested.count(b"\xa5\xa5\xa5\xa5") == 2
    assert b"folder" in nested
    assert b"method14_nested.txt" in nested

    dir_sentinel, ds_meta = build_dir_sentinel()
    assert dir_sentinel.count(b"\xa5\xa5\xa5\xa5") == 4
    sentinel_off = ds_meta["sentinel_offset"]
    assert struct.unpack(">I", dir_sentinel[sentinel_off + 34:sentinel_off + 38])[0] == 0xFFFFFFFF
    assert struct.unpack(">H", dir_sentinel[sentinel_off + 30:sentinel_off + 32])[0] == 0
    assert b"a.txt" in dir_sentinel and b"b.txt" in dir_sentinel

    rsrc, rsrc_meta = build_rsrc()
    assert rsrc.count(b"\xa5\xa5\xa5\xa5") == 1
    assert rsrc[rsrc_meta["rcrc_pos"]:rsrc_meta["rcrc_pos"] + 2] == u16(crc16_ibm(rsrc_meta["resource"]))
    assert apply_variant(rsrc, rsrc_meta, "bad-rsrc-stream")[rsrc_meta["stream_start"]] != rsrc[rsrc_meta["stream_start"]]
    bad_crc = apply_variant(rsrc, rsrc_meta, "bad-rsrc-crc")
    assert bad_crc[rsrc_meta["rcrc_pos"]:rsrc_meta["rcrc_pos"] + 2] != rsrc[rsrc_meta["rcrc_pos"]:rsrc_meta["rcrc_pos"] + 2]
    print("gen_sit5_method14_fixture selftest ok")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("kind", choices=["flat", "nested", "dir_sentinel", "rsrc", "selftest"])
    parser.add_argument("archive", nargs="?")
    parser.add_argument("--plain-out")
    parser.add_argument("--rsrc-out")
    parser.add_argument("--variant",
                        default="valid",
                        choices=["valid", "truncated", "bad-stream", "bad-ulen", "bad-rsrc-stream", "bad-rsrc-crc"])
    args = parser.parse_args()

    if args.kind == "selftest":
        selftest()
        return
    if not args.archive:
        parser.error("archive path is required unless kind is selftest")

    if args.kind == "flat":
        data, meta = build_flat()
    elif args.kind == "nested":
        data, meta = build_nested()
    elif args.kind == "dir_sentinel":
        data, meta = build_dir_sentinel()
    else:
        data, meta = build_rsrc()

    data = apply_variant(data, meta, args.variant)
    write_file(args.archive, data)
    write_file(args.plain_out, meta.get("plain", b""))
    write_file(args.rsrc_out, meta.get("resource", b""))


if __name__ == "__main__":
    main()
