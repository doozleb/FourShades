"""
Download the pinned SDL3 Windows VC development archive and verify it
against the committed manifest before extracting it.

    python third_party/sdl/fetch_sdl.py

SDL3 is one ~16 MB binary archive rather than the many small files tools/sst
and tools/roms deal with, so there is only one file to fetch here, but the
same rules apply: the version and its SHA-256 are pinned below (and in
manifest.sha256), and a hash mismatch fails loudly and deletes nothing
rather than silently continuing with a bad archive.

A single urllib.request.read() of the whole archive was tried first and
reliably died partway through with IncompleteRead, at a different byte
offset each time, on this network -- the same "large download keeps failing
part-way" problem noted for the SingleStepTests archive, just for one big
file instead of many small ones. The fix here is the same shape as that
one's per-file retry, applied within the one file: fetch in chunks, and on
any failure retry with an HTTP Range request that resumes from the bytes
already written, instead of restarting the whole download from zero.
"""

import hashlib
import http.client
import shutil
import sys
import time
import urllib.error
import urllib.request
import zipfile
from pathlib import Path

VERSION = "3.4.16"
ASSET = f"SDL3-devel-{VERSION}-VC.zip"
URL = f"https://github.com/libsdl-org/SDL/releases/download/release-{VERSION}/{ASSET}"
HERE = Path(__file__).resolve().parent
ARCHIVE = HERE / ASSET
MANIFEST = HERE / "manifest.sha256"
EXTRACTED = HERE / f"SDL3-{VERSION}"
SENTINEL = EXTRACTED / "include" / "SDL3" / "SDL.h"
ATTEMPTS = 8
CHUNK_SIZE = 256 * 1024


def download(url: str, dest: Path) -> None:
    """Fetch url into dest, resuming from dest's current size on retry.

    The connection has also been seen to close early with no exception at
    all -- read() just returns an empty chunk well short of the declared
    Content-Length, as if the response had ended normally. So completion
    isn't "the loop ran out of chunks", it's "the bytes on disk reached the
    size the server told us to expect"; anything short of that is treated
    the same as a dropped connection and retried with Range.
    """
    total = None
    last_error = "connection closed before the expected size was reached"
    for attempt in range(1, ATTEMPTS + 1):
        have = dest.stat().st_size if dest.exists() else 0
        if total is not None and have >= total:
            return
        headers = {"Range": f"bytes={have}-"} if have else {}
        request = urllib.request.Request(url, headers=headers)
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                # A server that ignores Range sends the whole file back
                # (status 200); anything already on disk would then be
                # duplicated ahead of it, so start that response over.
                if have and response.status != 206:
                    dest.unlink()
                    have = 0
                content_length = response.getheader("Content-Length")
                if content_length is not None:
                    total = have + int(content_length)
                with open(dest, "ab") as file:
                    while True:
                        chunk = response.read(CHUNK_SIZE)
                        if not chunk:
                            break
                        file.write(chunk)
        except urllib.error.HTTPError as error:
            if error.code == 416:  # nothing left to resume; what we have is it
                return
            last_error = error
            time.sleep(2 * attempt)
            continue
        except (OSError, http.client.HTTPException) as error:
            last_error = error
            time.sleep(2 * attempt)
            continue
        current = dest.stat().st_size
        if total is not None and current < total:
            last_error = f"connection closed at {current} of {total} bytes"
            time.sleep(2 * attempt)
            continue
        return
    raise SystemExit(f"error: {url}: failed after {ATTEMPTS} attempts: {last_error}")


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_manifest() -> str:
    line = MANIFEST.read_text(encoding="utf-8").strip()
    if len(line) <= 66 or line[64:66] != "  ":
        raise SystemExit(f"error: malformed manifest line: {line!r}")
    digest, name = line[:64], line[66:]
    if name != ASSET:
        raise SystemExit(f"error: manifest pins {name!r}, script wants {ASSET!r}; "
                          "they must name the same version")
    return digest


def fetch() -> int:
    expected = read_manifest()

    if SENTINEL.exists() and ARCHIVE.exists() and sha256_file(ARCHIVE) == expected:
        print(f"SDL3 {VERSION} present and verified at {EXTRACTED}")
        return 0

    if not ARCHIVE.exists() or sha256_file(ARCHIVE) != expected:
        print(f"downloading {ASSET} from {URL}")
        download(URL, ARCHIVE)

    actual = sha256_file(ARCHIVE)
    if actual != expected:
        raise SystemExit(
            f"error: {ASSET} does not match manifest.sha256\n"
            f"  expected {expected}\n"
            f"  got      {actual}\n"
            f"leaving {ARCHIVE} in place for inspection; nothing extracted")

    if EXTRACTED.exists():
        shutil.rmtree(EXTRACTED)
    with zipfile.ZipFile(ARCHIVE) as archive:
        archive.extractall(HERE)
    if not SENTINEL.exists():
        raise SystemExit(f"error: extracting {ASSET} did not produce {SENTINEL}")
    print(f"verified {ASSET} ({expected})")
    print(f"extracted into {EXTRACTED}")
    return 0


if __name__ == "__main__":
    sys.exit(fetch())
