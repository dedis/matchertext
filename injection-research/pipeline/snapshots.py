"""Content-addressed storage for reproducible pipeline inputs."""
import hashlib
import os
import shutil
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = os.environ.get("MATCHERTEXT_SNAPSHOT_ARCHIVE",
                         str(ROOT / "snapshot-archive")).rstrip("/")


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _local_blob(digest):
    if ARCHIVE.startswith(("http://", "https://")):
        return None
    return Path(ARCHIVE) / "sha256" / digest


def local_archive():
    return None if ARCHIVE.startswith(("http://", "https://")) else Path(ARCHIVE)


def store(path, digest=None):
    """Store *path* under its digest. Existing objects are never overwritten."""
    digest = digest or sha256(path)
    if sha256(path) != digest:
        raise RuntimeError(f"snapshot hash mismatch: {path}")
    dest = _local_blob(digest)
    if dest is None:
        return digest
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists():
        if sha256(dest) != digest:
            raise RuntimeError(f"corrupt snapshot object: {dest}")
        return digest
    tmp = dest.with_suffix(".part")
    shutil.copyfile(path, tmp)
    if sha256(tmp) != digest:
        tmp.unlink()
        raise RuntimeError(f"snapshot copy failed: {path}")
    tmp.replace(dest)
    return digest


def materialize(digest, dest):
    """Copy a verified object into *dest*. Return False when it is unavailable."""
    source = _local_blob(digest)
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_suffix(dest.suffix + ".part")
    if source is not None:
        if not source.exists():
            return False
        if sha256(source) != digest:
            raise RuntimeError(f"corrupt snapshot object: {source}")
        shutil.copyfile(source, tmp)
    else:
        url = f"{ARCHIVE}/sha256/{digest}"
        req = urllib.request.Request(url,
                                     headers={"User-Agent": "matchertext-injection-research"})
        try:
            with urllib.request.urlopen(req) as response, open(tmp, "wb") as output:
                shutil.copyfileobj(response, output, 1 << 20)
        except Exception:
            tmp.unlink(missing_ok=True)
            return False
    if sha256(tmp) != digest:
        tmp.unlink()
        raise RuntimeError(f"snapshot hash mismatch: {digest}")
    tmp.replace(dest)
    return True


def read(digest):
    path = _local_blob(digest)
    if path is not None and path.exists():
        if sha256(path) != digest:
            raise RuntimeError(f"corrupt snapshot object: {path}")
        return path.read_bytes()
    tmp = ROOT / "data" / "build" / f"snapshot-{digest}"
    if not materialize(digest, tmp):
        raise FileNotFoundError(f"snapshot object is unavailable: {digest}")
    data = tmp.read_bytes()
    tmp.unlink()
    return data
