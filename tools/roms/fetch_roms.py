"""
Download every test ROM and reference image named in tools/roms/tests.json
from the Emulator Shootout at the pinned commit, and check each against the
committed manifest.

    python tools/roms/fetch_roms.py                    # fetch what's missing or wrong, then verify
    python tools/roms/fetch_roms.py --write-manifest   # one-off: record the hashes

Files come one at a time from raw.githubusercontent.com, each retried on its
own. --write-manifest also checks every file against the git blob hash GitHub
lists for the commit. The ROMs are never committed to this repository.
"""

import hashlib
import http.client
import json
import sys
import time
import urllib.parse
import urllib.request
import zlib
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

HERE = Path(__file__).resolve().parent
DATA = HERE / "data"
MANIFEST = HERE / "manifest.sha256"
TESTS = json.loads((HERE / "tests.json").read_text(encoding="utf-8"))
COMMIT = TESTS["shootout_commit"]
RAW = f"https://raw.githubusercontent.com/gbdev/GBEmulatorShootout/{COMMIT}/testroms/"
TREE = f"https://api.github.com/repos/gbdev/GBEmulatorShootout/git/trees/{COMMIT}?recursive=1"
ATTEMPTS = 6
WORKERS = 8

SHADE_FOR_RGB = {
    (255, 255, 255): 0, (170, 170, 170): 1, (85, 85, 85): 2, (0, 0, 0): 3,
    # The Shootout's blargg/dmg_sound references (their `references` are
    # loaded for every test regardless of method, not only screenshot tests)
    # are rendered in the classic green DMG palette rather than pure grey.
    # Verified against every 8-bit truecolour PNG in the pinned corpus: these
    # are the only non-grey colours that appear, always at the lightest and
    # darkest ends of the same four-shade ramp.
    (224, 248, 208): 0, (8, 24, 32): 3,
}
# Greyscale samples are scaled to the 0-255 range before this lookup (see
# `read_samples`/the colour-0 branch below), so the same four DMG grey levels
# apply regardless of the PNG's bit depth.
SHADE_FOR_GREY = {rgb[0]: shade for rgb, shade in SHADE_FOR_RGB.items() if rgb[0] == rgb[1] == rgb[2]}
FRAME_WIDTH, FRAME_HEIGHT = 160, 144
# (depth, colour type) pairs actually present in the reference set at the
# pinned Shootout commit: 8-bit truecolour, greyscale at 1/2/8 bits, and
# 4-bit indexed (palette). Anything else fails loudly rather than guessing.
SUPPORTED_FORMATS = {(8, 2), (1, 0), (2, 0), (8, 0), (4, 3)}


def read_samples(row: bytes, width: int, depth: int) -> list[int]:
    """Unpacks `width` big-endian, MSB-first samples of `depth` bits (<= 8)."""
    per_byte = 8 // depth
    mask = (1 << depth) - 1
    samples = []
    for x in range(width):
        shift = 8 - depth * ((x % per_byte) + 1)
        samples.append((row[x // per_byte] >> shift) & mask)
    return samples


def decode_png(data: bytes) -> list[int]:
    """The image as 160x144 shade indices (0-3). Raises on any other format."""
    pos, idat, palette, width, height, depth, colour = 8, b"", None, None, None, None, None
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        kind = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width = int.from_bytes(chunk[0:4], "big")
            height = int.from_bytes(chunk[4:8], "big")
            depth, colour = chunk[8], chunk[9]
            if chunk[12] != 0:
                raise SystemExit("error: interlaced PNG")
        elif kind == b"PLTE":
            palette = [tuple(chunk[i:i + 3]) for i in range(0, len(chunk), 3)]
        elif kind == b"IDAT":
            idat += chunk
        pos += 12 + length
    if (width, height) != (FRAME_WIDTH, FRAME_HEIGHT):
        raise SystemExit(f"error: reference is {width}x{height}, expected 160x144")
    if (depth, colour) not in SUPPORTED_FORMATS:
        raise SystemExit(f"error: unsupported PNG depth {depth} colour type {colour}")
    if colour == 3 and not palette:
        raise SystemExit("error: indexed PNG has no PLTE chunk")
    raw = zlib.decompress(idat)
    channel_bits = 24 if colour == 2 else depth  # colour 2 is always 8-bit truecolour (3 channels)
    bytes_per_pixel = max(1, channel_bits // 8)
    stride = (width * channel_bits + 7) // 8
    out, previous, offset = [], bytearray(stride), 0
    for _ in range(height):
        filter_type = raw[offset]
        offset += 1
        row = bytearray(raw[offset:offset + stride])
        offset += stride
        for i in range(stride):
            left = row[i - bytes_per_pixel] if i >= bytes_per_pixel else 0
            up = previous[i]
            up_left = previous[i - bytes_per_pixel] if i >= bytes_per_pixel else 0
            if filter_type == 1:
                row[i] = (row[i] + left) & 0xFF
            elif filter_type == 2:
                row[i] = (row[i] + up) & 0xFF
            elif filter_type == 3:
                row[i] = (row[i] + (left + up) // 2) & 0xFF
            elif filter_type == 4:
                p = left + up - up_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - up_left)
                best = left if (pa <= pb and pa <= pc) else (up if pb <= pc else up_left)
                row[i] = (row[i] + best) & 0xFF
            elif filter_type != 0:
                raise SystemExit(f"error: unknown PNG filter {filter_type}")
        if colour == 2:
            for x in range(width):
                pixel = (row[x * 3], row[x * 3 + 1], row[x * 3 + 2])
                if pixel not in SHADE_FOR_RGB:
                    raise SystemExit(f"error: reference pixel {pixel} is not a DMG shade")
                out.append(SHADE_FOR_RGB[pixel])
        elif colour == 3:
            for index in read_samples(row, width, depth):
                if index >= len(palette):
                    raise SystemExit(f"error: palette index {index} out of range")
                pixel = palette[index]
                if pixel not in SHADE_FOR_RGB:
                    raise SystemExit(f"error: reference pixel {pixel} is not a DMG shade")
                out.append(SHADE_FOR_RGB[pixel])
        else:
            maximum = (1 << depth) - 1
            for sample in read_samples(row, width, depth):
                grey = sample * 255 // maximum
                if grey not in SHADE_FOR_GREY:
                    raise SystemExit(f"error: reference grey level {grey} is not a DMG shade")
                out.append(SHADE_FOR_GREY[grey])
        previous = row
    return out


def write_shades(names: list[str]) -> int:
    """Decode every reference PNG into a .shades file beside it."""
    written = 0
    for name in names:
        if not name.endswith(".png"):
            continue
        source = DATA / name
        target = source.with_suffix(source.suffix + ".shades")
        target.write_bytes(bytes(decode_png(source.read_bytes())))
        written += 1
    return written


def wanted() -> list[str]:
    names = set()
    for test in TESTS["tests"]:
        names.add(test["rom"])
        names.update(test["references"])
    return sorted(names)


def get(url: str) -> bytes:
    last_error = None
    for attempt in range(1, ATTEMPTS + 1):
        try:
            with urllib.request.urlopen(url, timeout=60) as response:
                return response.read()
        except (OSError, http.client.HTTPException) as error:
            last_error = error
            time.sleep(2 * attempt)
    raise SystemExit(f"error: {url}: failed after {ATTEMPTS} attempts: {last_error}")


def git_blob_sha1(content: bytes) -> str:
    return hashlib.sha1(b"blob %d\0" % len(content) + content).hexdigest()


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def download(names: list[str]) -> None:
    def one(name: str) -> None:
        target = DATA / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(get(RAW + urllib.parse.quote(name)))

    with ThreadPoolExecutor(WORKERS) as pool:
        for done, _ in enumerate(pool.map(one, names), 1):
            if done % 50 == 0 or done == len(names):
                print(f"  downloaded {done}/{len(names)}")


def read_manifest() -> dict[str, str]:
    entries = {}
    for line in MANIFEST.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        if len(line) <= 66 or line[64:66] != "  ":
            raise SystemExit(f"error: malformed manifest line: {line!r}")
        entries[line[66:]] = line[:64]
    return entries


def write_manifest() -> int:
    tree = json.loads(get(TREE))
    if tree.get("truncated"):
        raise SystemExit("error: GitHub truncated the file listing")
    blobs = {e["path"][len("testroms/"):]: e["sha"] for e in tree["tree"]
             if e["type"] == "blob" and e["path"].startswith("testroms/")}
    names = wanted()
    unknown = [n for n in names if n not in blobs]
    if unknown:
        raise SystemExit(f"error: not in the Shootout at {COMMIT}: {unknown[:5]}")

    def matches(name: str) -> bool:
        path = DATA / name
        return path.exists() and git_blob_sha1(path.read_bytes()) == blobs[name]

    missing = [n for n in names if not matches(n)]
    if missing:
        print(f"downloading {len(missing)} files")
        download(missing)
    wrong = [n for n in names if not matches(n)]
    if wrong:
        print(f"error: {len(wrong)} files don't match GitHub's git blob hash")
        for name in wrong[:20]:
            print(f"  {name}")
        return 1
    MANIFEST.write_text("".join(f"{sha256_file(DATA / n)}  {n}\n" for n in names),
                        encoding="utf-8", newline="\n")
    print(f"wrote {MANIFEST} ({len(names)} files, each matching GitHub's git blob hash)")
    print(f"decoded {write_shades(names)} reference images")
    return 0


def fetch() -> int:
    expected = read_manifest()
    if sorted(expected) != wanted():
        raise SystemExit("error: manifest and tests.json name different files; regenerate the manifest")

    def stale(name: str) -> bool:
        path = DATA / name
        return not path.exists() or sha256_file(path) != expected[name]

    todo = [n for n in sorted(expected) if stale(n)]
    if not todo:
        print(f"test ROMs present and verified ({len(expected)} files)")
        print(f"decoded {write_shades(sorted(expected))} reference images")
        return 0
    print(f"downloading {len(todo)} files")
    download(todo)
    bad = [n for n in todo if stale(n)]
    if bad:
        print(f"error: {len(bad)} downloaded files don't match the manifest")
        for name in bad[:20]:
            print(f"  {name}")
        return 1
    print(f"downloaded {len(todo)} files; all {len(expected)} verified")
    print(f"decoded {write_shades(sorted(expected))} reference images")
    return 0


if __name__ == "__main__":
    sys.exit(write_manifest() if "--write-manifest" in sys.argv[1:] else fetch())
