#!/usr/bin/env python3
"""Backport the Cruis'n Blast 2.31 cars onto a working 1.25 game tree.

The 2.31 `.g5` models load fine in the 1.25 engine; the only obstacle is that
the engine resolves a car's display object by the *slot* name
(`ResFindSubNth(0,"<slot>",0x14)` in `ObjCreateDisplayResAssert`, asserting at
g5/obj.c:4183), while each model names its nodes after its own car. So a swap =
rename the new model's node tokens to the slot name. Node names live in
fixed-width fields on a 108-byte stride, so a shrink-or-equal rename never moves
a byte. See docs/cruisn-blast-231.md.

This replaces five "sacrifice" slots (keeping the Aventador, Corvette Z06 and
Huracán) with the five new cars, patches the car-select name text in
translation.csv, and copies each car's paint-colour list. The manufacturer
badge texture stays the slot's (cosmetic). Backups (*.ccbak) allow --restore.

Usage:
  cruisn-blast-backport-cars.py --src "<2.31 dir>" --dst "<1.25 game dir>"
  cruisn-blast-backport-cars.py --dst "<1.25 game dir>" --restore
Both dirs are the game roots that contain g5/race/data/...
"""
import argparse, os, re, shutil, sys

# new 2.31 car (model/prefix) -> (slot model stem, UI key stem, LINE1, LINE2)
# the UI key stem is the slot's STR_CAR_SEL2_LOGO_<KEY>_* name and usually
# equals the model stem (uppercased) but not always (hummer_hx -> HUMMER).
# "premium" replaces 5 of the 8 main-carousel cars (keeps Aventador, Corvette
# Z06, Huracan). "bonus" instead puts the new cars onto 5 of the 8 "Bonus Cars"
# (PIN/TUNES page) slots, leaving ALL premium cars in place. Bonus slots have no
# name-logo entry in the binary, so no CSV name is written for them (cosmetic).
PRESETS = {
    "premium": {
        "nissan370z":  ("gtr",       "GTR",      "Nissan",    "370Z"),
        "corvette_59": ("hummer_hx", "HUMMER",   "Chevrolet", "Corvette (1959)"),
        "stingray":    ("countach",  "COUNTACH", "Chevrolet", "Corvette Sting Ray"),
        "camaro":      ("veneno",    "VENENO",   "Chevrolet", "Camaro"),
        "eldorado":    ("diablo",    "DIABLO",   "Cadillac",  "Eldorado"),
    },
    # Bonus-page ("secret cars", PIN/TUNES): all 5 new cars go into the 5
    # crash-free joke slots, keeping ALL 8 premium cars in the main carousel.
    # The joke types have full per-car data (they ship as the stock bonus cars).
    # AVOIDED joke slots (keep their originals):
    #   - cop_car / POLICE (0x16): creates "<car>_glow_N" lightbar props and
    #     writes [prop+0x784] with NO null check -> SEGV with a normal model.
    #   - madagascar_carriage (7): mounts cow/passenger characters.
    #   - heli (0x15): rotor props + no heli_body.g5.
    # SCALE: jeep=1.0 and london_taxi=0.85 render normal; london_bus (0.45),
    # madagascar_bus1 (0.5) and trcratops (0.2) show the car small (the slot is
    # sized for a big vehicle). patch_scale() can lift those to 1.0.
    # Superseded by "hybrid": only jeep and london_taxi are real cars; the bus /
    # madagascar_bus1 / trcratops slots keep their joke behaviour IN THE RACE
    # too (scaled down, bus crowds, dino animation/crash). Kept for reference.
    "bonus": {
        "camaro":      ("jeep",            None, "Chevrolet", "Camaro"),
        "nissan370z":  ("london_taxi",     None, "Nissan",    "370Z"),
        "stingray":    ("london_bus",      None, "Chevrolet", "Corvette Sting Ray"),
        "corvette_59": ("madagascar_bus1", None, "Chevrolet", "Corvette (1959)"),
        "eldorado":    ("trcratops",       None, "Cadillac",  "Eldorado"),
    },
    # "hybrid" (recommended): all 5 new cars, every one raceable and full size.
    # 3 replace redundant Lambos in the MAIN carousel (named, full data), and 2
    # go on the Bonus page's only real-car slots (jeep + london_taxi). The
    # loader lifts the taxi's 0.85 showroom scale to 1.0 (cbCodePatches, gated
    # on bonus_cars.flag). Keeps Aventador, Corvette Z06, GTR, Hummer, Huracan.
    "hybrid": {
        "stingray":    ("countach",    "COUNTACH", "Chevrolet", "Corvette Sting Ray"),
        "corvette_59": ("diablo",      "DIABLO",   "Chevrolet", "Corvette (1959)"),
        "eldorado":    ("veneno",      "VENENO",   "Cadillac",  "Eldorado"),
        "camaro":      ("jeep",         None,      "Chevrolet", "Camaro"),
        "nissan370z":  ("london_taxi",  None,      "Nissan",    "370Z"),
    },
    # NOTE: a "bonuspatch" that repointed carTypeOf's 8 bonus slots to the
    # hidden types (raptor/essence/ssc/wrangler/kia) to host all 5 new cars was
    # tried and ABANDONED: those types aren't set up as player cars, so the
    # car-select crashes on missing per-car data (first CarSel_CreateCarProp
    # police glow nodes, then CarParamsListFor physics params, and more behind
    # that). The car-select is hardwired to its exact 16 cars. See
    # docs/cruisn-blast-231.md. Use "premium" or "bonus".
}

# Scale fix for the bonus preset: the london_bus / madagascar_bus1 / tricera
# slots scale their car down in CarSel_CreateCar3D (writes to [ebx+0xb0/b4/b8]).
# Lift all three to 1.0 (0x3f800000) so the new cars render full size. This is
# a game-binary patch -> the game CRC changes, so the loader descriptor needs
# altFileCrc32 = the printed value. (file offset of each imm32, old value.)
SCALE_PATCH = [
    (0x132b06, 0x3f000000), (0x132b10, 0x3f000000), (0x132b1a, 0x3f000000),
    (0x13405a, 0x3ee66666), (0x134064, 0x3ee66666), (0x13406e, 0x3ee66666),
    (0x1345f4, 0x3e4ccccd), (0x1345fe, 0x3e4ccccd), (0x134608, 0x3e4ccccd),
]
SCALE_NEW = 0x3f800000  # 1.0f


def patch_scale(game_path):
    import zlib
    backup(game_path)
    d = bytearray(open(game_path, "rb").read())
    for off, old in SCALE_PATCH:
        cur = int.from_bytes(d[off:off + 4], "little")
        if cur == SCALE_NEW:
            continue
        if cur != old:
            raise ValueError("scale patch: at 0x%x expected 0x%08x, found 0x%08x"
                             % (off, old, cur))
        d[off:off + 4] = SCALE_NEW.to_bytes(4, "little")
    open(game_path, "wb").write(bytes(d))
    return zlib.crc32(bytes(d)) & 0xffffffff


G5   = "g5/race/data/g5"
CARS = "g5/race/data/conf/car/cars"
TCSV = "g5/race/data/translation/english/translation.csv"
BAK  = ".ccbak"


def backup(path):
    b = path + BAK
    if os.path.exists(path) and not os.path.exists(b):
        shutil.copy2(path, b)


def restore_file(path):
    b = path + BAK
    if os.path.exists(b):
        shutil.copy2(b, path)
        os.remove(b)
        return True
    return False


def rename_tokens(data: bytes, old: bytes, new: bytes) -> bytes:
    """Rename every node-name token `old`(\0|_...) -> `new`, in place.

    Names sit in null-terminated fields padded with NULs; the file length is
    never changed. A longer slot name is allowed as long as the new token still
    fits inside the token+trailing-NUL-padding of that field (so no following
    data byte is touched). Byte offsets are preserved exactly."""
    d = bytearray(data)
    n = 0
    for m in list(re.finditer(re.escape(old) + rb"(?=[_\x00])", bytes(d))):
        s = m.start()
        e = s
        while e < len(d) and d[e] != 0:
            e += 1                              # e = first NUL after the token
        p = e
        while p < len(d) and d[p] == 0:
            p += 1                              # p = next non-NUL (field end)
        tok = bytes(d[s:e])
        newtok = new + tok[len(old):]          # swap the prefix only
        if len(newtok) > (p - s) - 1:          # must leave >=1 NUL terminator
            raise ValueError("no room to rename '%s'->'%s' at 0x%x (cap %d)"
                             % (tok.decode('latin1'), newtok.decode('latin1'),
                                s, p - s))
        d[s:p] = newtok + b"\x00" * ((p - s) - len(newtok))  # rewrite field
        n += 1
    return bytes(d), n


def patch_csv(path, ui_key, line1, line2):
    # translation.csv is UTF-16-LE with a BOM; the "utf-16" codec round-trips it.
    repl = {
        "STR_CAR_SEL2_LOGO_%s_LINE1" % ui_key: line1,
        "STR_CAR_SEL2_LOGO_%s_LINE2" % ui_key: line2,
        "STR_SWIPE_CARDS_%s" % ui_key: ("%s %s" % (line1, line2)).upper(),
        "STR_PIN_STATS_%s" % ui_key: "%s\\n%s" % (line1, line2),
    }
    with open(path, encoding="utf-16") as f:
        lines = f.readlines()
    done = set()
    for i, ln in enumerate(lines):
        key = ln.split(",", 1)[0]
        if key in repl:
            # replace field 2 (comma-free for these keys), keep the rest
            lines[i] = re.sub(r"^(" + re.escape(key) + r",)[^,]*,",
                              r"\g<1>" + repl[key].replace("\\", "\\\\") + ",",
                              ln, count=1)
            done.add(key)
    with open(path, "w", encoding="utf-16") as f:
        f.writelines(lines)
    return done


def do_restore(dst):
    n = 0
    flag = os.path.join(dst, "bonus_cars.flag")
    if os.path.exists(flag):
        os.remove(flag)
    for root, _, files in os.walk(dst):
        for fn in files:
            if fn.endswith(BAK):
                orig = os.path.join(root, fn[:-len(BAK)])
                if restore_file(orig):
                    n += 1
    print("restored %d file(s) from %s backups" % (n, BAK))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", help="2.31 game dir (source of new car models)")
    ap.add_argument("--dst", required=True, help="1.25 game dir to modify")
    ap.add_argument("--restore", action="store_true", help="undo, restore backups")
    ap.add_argument("--preset", choices=sorted(PRESETS), default="hybrid",
                    help="which slots to use (default: bonus)")
    a = ap.parse_args()

    if a.restore:
        do_restore(a.dst)
        return 0
    if not a.src:
        ap.error("--src is required unless --restore")

    mapping = PRESETS[a.preset]
    print("preset: %s" % a.preset)
    tcsv = os.path.join(a.dst, TCSV)
    backup(tcsv)
    for car, (slot, ui_key, l1, l2) in mapping.items():
        srcg5 = os.path.join(a.src, G5, car + ".g5")
        dstg5 = os.path.join(a.dst, G5, slot + ".g5")
        if not os.path.exists(srcg5):
            print("!! missing %s, skipping" % srcg5); continue
        if not os.path.exists(dstg5):
            print("!! slot model %s absent, skipping" % dstg5); continue
        data = open(srcg5, "rb").read()
        out, ntok = rename_tokens(data, car.encode(), slot.encode())
        assert len(out) == len(data)
        backup(dstg5)
        open(dstg5, "wb").write(out)
        # paint-colour list
        srctxt = os.path.join(a.src, CARS, car + ".txt")
        dsttxt = os.path.join(a.dst, CARS, slot + ".txt")
        if os.path.exists(srctxt):
            backup(dsttxt)
            shutil.copy2(srctxt, dsttxt)
        keys = patch_csv(tcsv, ui_key, l1, l2) if ui_key else set()
        print("%-12s -> %-11s (%3d nodes renamed, name='%s %s', csv:%d)"
              % (car, slot, ntok, l1, l2, len(keys)))
    if a.preset in ("bonus", "hybrid"):
        # Marker the loader checks (rtGames.c codePatchMarker): only this image
        # gets the showroom-scale lifted to 1.0, so the stock CB is untouched.
        open(os.path.join(a.dst, "bonus_cars.flag"), "w").close()
        print("wrote bonus_cars.flag (loader applies the 1.0x scale only here)")
    print("done. test it, then --restore to undo.")
    # The london_bus/madagascar_bus1/trcratops showroom scale is lifted to 1.0 by
    # the LOADER (cbCodePatches in rtGames.c, gated on bonus_cars.flag), not by
    # patching the game binary -- a modified dump crashes the cabinet loader. The
    # game stays original (CRC 0xb919dbd6).
    return 0


if __name__ == "__main__":
    sys.exit(main())
