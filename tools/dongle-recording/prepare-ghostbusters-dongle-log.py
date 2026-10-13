#!/usr/bin/env python3
"""Build an instrumented Ghostbusters executable from a clean v01.17 dump."""
import argparse
import pathlib
import stat
import subprocess
import tempfile
import zlib


BASE = 0x8048000
CAVE_VMA = 0x897F0A2
CAVE_OFFSET = CAVE_VMA - BASE
CALLS = (
    # file offset, VMA, expected wrapper, new trampoline
    (0x819B74F - BASE, 0x819B74F, 0x83E2868, CAVE_VMA),
    (0x819B7BC - BASE, 0x819B7BC, 0x83E277C, CAVE_VMA + 0xC),
)
GUARD_OFFSET = 0x82E8719 - BASE
GUARD_PROLOGUE = bytes.fromhex("55 89 e5 83 ec 38")
GUARD_STUB = bytes.fromhex("b8 01 00 00 00 c3")
ORIGINAL_CRC32 = 0x774AD438


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=pathlib.Path,
                        help="clean Ghostbusters Arcade pm/game")
    parser.add_argument("output", type=pathlib.Path,
                        help="new instrumented copy, e.g. pm/game_dongle_log")
    args = parser.parse_args()
    original = args.original.resolve()
    output = args.output.resolve()
    if original == output:
        parser.error("output must be a separate copy; the original is never patched")

    data = original.read_bytes()
    if zlib.crc32(data) & 0xFFFFFFFF != ORIGINAL_CRC32:
        parser.error("input CRC32 does not match the known clean v01.17 executable")
    cave_end = CAVE_OFFSET + 0x98
    if data[CAVE_OFFSET:cave_end] != bytes(cave_end - CAVE_OFFSET):
        parser.error("expected cave area is not an unused zero run")
    if data[GUARD_OFFSET:GUARD_OFFSET + 6] != GUARD_PROLOGUE:
        parser.error("TracerGuard bytes do not match the expected v01.17 prologue")

    with tempfile.TemporaryDirectory(prefix="gb-cave-") as temp:
        temp = pathlib.Path(temp)
        obj, elf, binary = temp / "gb-cave.o", temp / "gb-cave.elf", temp / "gb-cave.bin"
        source = pathlib.Path(__file__).with_name("gb-cave.S")
        subprocess.run(["as", "--32", str(source), "-o", str(obj)], check=True)
        subprocess.run(["ld", "-m", "elf_i386", "-Ttext=0x897f0a2",
                        "-e", "_start", "-o", str(elf), str(obj)], check=True)
        subprocess.run(["objcopy", "-O", "binary", "-j", ".text",
                        str(elf), str(binary)], check=True)
        cave = binary.read_bytes()

    if len(cave) != 0x98:
        parser.error("unexpected cave size: expected 0x98 bytes, got 0x%x" % len(cave))
    data = bytearray(data)
    data[CAVE_OFFSET:cave_end] = cave
    for offset, site, wrapper, target in CALLS:
        instruction = data[offset:offset + 5]
        if len(instruction) != 5 or instruction[0] != 0xE8:
            parser.error("expected CALL rel32 at file offset 0x%x" % offset)
        old_target = site + 5 + int.from_bytes(instruction[1:], "little", signed=True)
        if old_target != wrapper:
            parser.error("unexpected call target at VMA 0x%x: 0x%x" % (site, old_target))
        data[offset + 1:offset + 5] = int(target - (site + 5)).to_bytes(
            4, "little", signed=True)
        new_target = site + 5 + int.from_bytes(data[offset + 1:offset + 5],
                                               "little", signed=True)
        assert new_target == target

    data[GUARD_OFFSET:GUARD_OFFSET + len(GUARD_STUB)] = GUARD_STUB
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)
    output.chmod(stat.S_IMODE(original.stat().st_mode))
    print("Wrote %s (152-byte cave, two HASP call hooks, TracerGuard stub)" % output)


if __name__ == "__main__":
    main()
