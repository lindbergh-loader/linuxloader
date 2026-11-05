#!/usr/bin/env python3
"""Build an instrumented Minecraft Dungeons Arcade dump (pm/g7/minecraft/gamedump.exec)."""
import argparse
import pathlib
import stat
import subprocess
import tempfile
import zlib


BASE = 0x400000
CAVE_VMA = 0x14A1200
CAVE_OFFSET = CAVE_VMA - BASE
CAVE_ROOM = 0x200
CALLS = (
    # VMA of the call, expected hasp function, new trampoline
    (0x652F93, 0xB66CBD, CAVE_VMA),         # hasp_encrypt
    (0x653287, 0xB66CBD, CAVE_VMA),         # hasp_encrypt
    (0x6532C7, 0xB66DCF, CAVE_VMA + 0xF),   # hasp_decrypt
)
ORIGINAL_CRC32 = 0x46229CE6


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=pathlib.Path,
                        help="clean pm/g7/minecraft/gamedump.exec")
    parser.add_argument("output", type=pathlib.Path,
                        help="new instrumented copy, e.g. gamedump_dongle_log")
    args = parser.parse_args()
    original = args.original.resolve()
    output = args.output.resolve()
    if original == output:
        parser.error("output must be a separate copy; the original is never patched")

    data = original.read_bytes()
    if zlib.crc32(data) & 0xFFFFFFFF != ORIGINAL_CRC32:
        parser.error("input CRC32 does not match the known gamedump.exec")
    if data[CAVE_OFFSET:CAVE_OFFSET + CAVE_ROOM] != bytes(CAVE_ROOM):
        parser.error("expected cave area is not an unused zero run")

    with tempfile.TemporaryDirectory(prefix="mcd-cave-") as temp:
        temp = pathlib.Path(temp)
        obj, elf, binary = temp / "mcd-cave.o", temp / "mcd-cave.elf", temp / "mcd-cave.bin"
        source = pathlib.Path(__file__).with_name("mcd-cave.S")
        subprocess.run(["as", "--64", str(source), "-o", str(obj)], check=True)
        subprocess.run(["ld", "-Ttext=0x%x" % CAVE_VMA, "-e", "_start",
                        "-o", str(elf), str(obj)], check=True)
        subprocess.run(["objcopy", "-O", "binary", "-j", ".text",
                        str(elf), str(binary)], check=True)
        cave = binary.read_bytes()

    if len(cave) > CAVE_ROOM:
        parser.error("cave is 0x%x bytes, more than the 0x%x free" % (len(cave), CAVE_ROOM))
    data = bytearray(data)
    data[CAVE_OFFSET:CAVE_OFFSET + len(cave)] = cave
    for site, wrapper, target in CALLS:
        offset = site - BASE
        instruction = data[offset:offset + 5]
        if instruction[0] != 0xE8:
            parser.error("expected CALL rel32 at VMA 0x%x" % site)
        old_target = site + 5 + int.from_bytes(instruction[1:], "little", signed=True)
        if old_target != wrapper:
            parser.error("unexpected call target at VMA 0x%x: 0x%x" % (site, old_target))
        data[offset + 1:offset + 5] = (target - (site + 5)).to_bytes(4, "little", signed=True)

    output.write_bytes(data)
    output.chmod(stat.S_IMODE(original.stat().st_mode) | 0o111)
    print("Wrote %s (%d-byte cave, %d HASP call hooks)" % (output, len(cave), len(CALLS)))


if __name__ == "__main__":
    main()
