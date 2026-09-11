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
    return 0


if __name__ == "__main__":
    sys.exit(write_manifest() if "--write-manifest" in sys.argv[1:] else fetch())
