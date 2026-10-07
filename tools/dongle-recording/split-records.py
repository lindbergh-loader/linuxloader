#!/usr/bin/env python3
"""Split a dongle capture into the answer files rtDongle.c replays.

The capture is whatever the instrumented game wrote to its output while
running under BudgieLoader: the game's own noise with our records mixed in.
A record is

    marker (4 bytes) | length (4, LE) | input (length) | output (length)

Some stubs write the call's status between the two halves -- The Walking Dead's
puts 4 bytes there, Halo's puts none -- so the gap is given by --status-bytes.
Get it wrong and the answers come out shifted, with the right names.

and the answer is looked up later by the hex of the first KEYSIZE bytes of the
input, so that is the file name and the output is the contents.

    split-records.py capture.bin outdir [--key-size 16] [--marker @@D@ ...]

See docs/dongle-recording.md.
"""
import argparse
import re
import sys
from pathlib import Path


def records(blob, markers, max_length, status_bytes=0):
    """Every well-formed record in the blob, as (marker, input, output)."""
    pattern = b'|'.join(re.escape(m) for m in markers)
    for match in re.finditer(pattern, blob):
        start = match.start()
        if start + 8 > len(blob):
            continue
        length = int.from_bytes(blob[start + 4:start + 8], 'little')
        if not 0 < length <= max_length:
            continue                      # a marker-like byte run, not a record
        span = 2 * length + status_bytes
        if start + 8 + span > len(blob):
            continue                      # truncated: the capture stopped early
        body = blob[start + 8:start + 8 + span]
        yield match.group(), body[:length], body[length + status_bytes:]


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('capture', type=Path)
    ap.add_argument('outdir', type=Path)
    ap.add_argument('--key-size', type=int, default=16,
                    help='bytes of the input that name the file (default 16; '
                         'must match the game\'s haspAnswerKeySize)')
    ap.add_argument('--marker', action='append', default=None,
                    help='record marker, repeatable (default @@D@ and @@E@)')
    ap.add_argument('--status-bytes', type=int, default=0,
                    help='bytes the stub writes between input and output '
                         '(0 for Halo\'s stub, 4 for The Walking Dead\'s)')
    ap.add_argument('--max-length', type=int, default=1 << 16,
                    help='reject records claiming to be longer than this')
    ap.add_argument('-n', '--dry-run', action='store_true')
    args = ap.parse_args()

    markers = [m.encode() for m in (args.marker or ['@@D@', '@@E@'])]
    blob = args.capture.read_bytes()

    written, seen = 0, {}
    for marker, request, answer in records(blob, markers, args.max_length,
                                          args.status_bytes):
        if len(request) < args.key_size:
            print('skipping a %d byte record: shorter than the %d byte key'
                  % (len(request), args.key_size), file=sys.stderr)
            continue
        name = request[:args.key_size].hex()
        if name in seen and seen[name] != answer:
            print('warning: %s recorded twice with different answers' % name,
                  file=sys.stderr)
        seen[name] = answer
        print('%s  %s  %d bytes' % (marker.decode('latin1'), name, len(answer)))
        if not args.dry_run:
            args.outdir.mkdir(parents=True, exist_ok=True)
            (args.outdir / name).write_bytes(answer)
            written += 1
    if not seen:
        print('no records found -- wrong marker, or the capture missed them?',
              file=sys.stderr)
        return 1
    print('%d record(s), %d file(s) written' % (len(seen), written))
    return 0


if __name__ == '__main__':
    sys.exit(main())
