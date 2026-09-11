"""
Download the SingleStepTests SM83 suite at the pinned commit and check every
file against the committed manifest.

    python tools/sst/fetch_sst.py                    # fetch what's missing or wrong, then verify
    python tools/sst/fetch_sst.py --write-manifest   # one-off: record the hashes

The data is gitignored: 167 MB of JSON doesn't belong in the repo, and pinning
the commit plus hashing every file gives everyone byte-identical tests.

Files come one at a time from raw.githubusercontent.com at the pinned commit,
each retried on its own, because one 32 MB archive download keeps failing
part-way on flaky connections. Reruns only fetch what is missing or wrong.
--write-manifest also checks every file against the git blob hash GitHub lists
for the commit, so the manifest is provably the commit's content.
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

REPO = "SingleStepTests/sm83"
COMMIT = "f9c30210245dd691661db39f5ace022c465ecc2f"
RAW = f"https://raw.githubusercontent.com/{REPO}/{COMMIT}/"
TREE = f"https://api.github.com/repos/{REPO}/git/trees/{COMMIT}?recursive=1"
HERE = Path(__file__).resolve().parent
DATA = HERE / "data"
MANIFEST = HERE / "manifest.sha256"
EXPECTED_FILES = 500
ATTEMPTS = 6
WORKERS = 8


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
    """Fetch each "v1/<name>.json" into DATA, several at a time."""
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
    blobs = {e["path"]: e["sha"] for e in tree["tree"]
             if e["type"] == "blob" and e["path"].startswith("v1/") and e["path"].endswith(".json")}
    if len(blobs) != EXPECTED_FILES:
        raise SystemExit(f"error: expected {EXPECTED_FILES} test files at {COMMIT}, GitHub lists {len(blobs)}")

    def matches(name: str) -> bool:
        path = DATA / name
        return path.exists() and git_blob_sha1(path.read_bytes()) == blobs[name]

    missing = [name for name in sorted(blobs) if not matches(name)]
    if missing:
        print(f"downloading {len(missing)} files from {RAW}")
        download(missing)
    wrong = [name for name in sorted(blobs) if not matches(name)]
    if wrong:
        print(f"error: {len(wrong)} files don't match GitHub's git blob hash for {COMMIT}")
        for name in wrong[:20]:
            print(f"  {name}")
        return 1
    text = "".join(f"{sha256_file(DATA / name)}  {name}\n" for name in sorted(blobs))
    MANIFEST.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {MANIFEST} ({len(blobs)} files, each matching GitHub's git blob hash)")
    return 0


def fetch() -> int:
    expected = read_manifest()
    if len(expected) != EXPECTED_FILES:
        raise SystemExit(f"error: manifest lists {len(expected)} files, expected {EXPECTED_FILES}")
    folder = DATA / "v1"
    if folder.exists():
        for path in folder.glob("*.json"):
            if f"v1/{path.name}" not in expected:
                path.unlink()  # not part of the pinned suite

    def stale(name: str) -> bool:
        path = DATA / name
        return not path.exists() or sha256_file(path) != expected[name]

    todo = [name for name in sorted(expected) if stale(name)]
    if not todo:
        print(f"test data present and verified ({len(expected)} files)")
        return 0
    print(f"downloading {len(todo)} files from {RAW}")
    download(todo)
    bad = [name for name in todo if stale(name)]
    if bad:
        print(f"error: {len(bad)} downloaded files don't match the manifest")
        for name in bad[:20]:
            print(f"  {name}")
        return 1
    print(f"downloaded {len(todo)} files; all {len(expected)} verified")
    return 0


if __name__ == "__main__":
    sys.exit(write_manifest() if "--write-manifest" in sys.argv[1:] else fetch())
