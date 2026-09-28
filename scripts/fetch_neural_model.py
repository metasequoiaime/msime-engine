#!/usr/bin/env python3
"""Download and verify the pinned neural sentence models."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import tempfile
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
MODEL_DIR = ROOT / "neural-model"
LOCK = MODEL_DIR / "lock.json"


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            hasher.update(chunk)
    return hasher.hexdigest()


def fetch(force: bool) -> None:
    lock = json.loads(LOCK.read_text(encoding="utf-8"))
    for model in lock["models"]:
        destination = MODEL_DIR / model["file"]
        if destination.is_file() and not force and digest(destination) == model["sha256"]:
            print(f"Validated {destination.name}")
            continue

        with tempfile.NamedTemporaryFile(dir=MODEL_DIR, prefix=f".{destination.name}.", delete=False) as temporary:
            incoming = Path(temporary.name)
        try:
            request = urllib.request.Request(model["url"], headers={"User-Agent": "MSIME-Engine model fetch"})
            with urllib.request.urlopen(request) as response, incoming.open("wb") as output:
                while chunk := response.read(1 << 20):
                    output.write(chunk)
            actual = digest(incoming)
            if actual != model["sha256"]:
                raise RuntimeError(f"SHA-256 mismatch for {destination.name}: expected {model['sha256']}, got {actual}")
            incoming.replace(destination)
            print(f"Fetched {destination.name}")
        finally:
            incoming.unlink(missing_ok=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--force", action="store_true", help="redownload already validated files")
    args = parser.parse_args()
    fetch(args.force)


if __name__ == "__main__":
    main()
