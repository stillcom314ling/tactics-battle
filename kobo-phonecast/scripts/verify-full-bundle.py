#!/usr/bin/env python3
"""Re-reads a built full bundle and checks what actually shipped.

build-full-bundle.py checks paths on the way in; this checks them on the way
out, from the finished file, the way a stranger with only the artifact would.
The two passes share the policy but not the code path that writes the archive.

  python3 scripts/verify-full-bundle.py dist/KoboRoot.tgz
"""

import hashlib
import importlib.util
import sys
import tarfile
from pathlib import Path

# The policy lives in the builder; loaded by path because the filename is
# hyphenated. Sharing it means the two passes cannot drift apart.
spec = importlib.util.spec_from_file_location(
    "bfb", Path(__file__).resolve().parent / "build-full-bundle.py")
bfb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bfb)

ROOT = Path(__file__).resolve().parent.parent


def main(path):
    problems = []
    with tarfile.open(path, "r:gz") as tf:
        members = tf.getmembers()
        if not members:
            problems.append("archive is empty")

        outside = []
        plugin_files = {}
        for m in members:
            p = m.name
            if p.startswith("/"):
                problems.append(f"absolute path: {p}")
            if ".." in p.split("/"):
                problems.append(f"path contains '..': {p}")
            if (m.uid, m.gid, m.uname, m.gname) != (0, 0, "root", "root"):
                problems.append(f"not root/root: {p}")
            if p.startswith("mnt/onboard/.kobo"):
                problems.append(f"writes into .kobo/: {p}")

            if not p.startswith("mnt/onboard/.adds"):
                # tarfile strips the trailing slash from directory names, so
                # ask the member, not the string.
                if not m.isdir():
                    outside.append(p)
                if not m.isdir() and not any(rx.match(p) for rx in bfb.ALLOWED_OUTSIDE_ADDS):
                    problems.append(f"outside .adds/ and not on the launcher allowlist: {p}")
            for rx in bfb.BANNED:
                if rx.match(p):
                    problems.append(f"banned path: {p}")

            if p.startswith(bfb.PLUGIN_DIR + "/") and m.isfile():
                plugin_files[p.rsplit("/", 1)[1]] = tf.extractfile(m).read()

    for lua in ("_meta.lua", "main.lua"):
        want = (ROOT / "plugin" / lua).read_bytes()
        if plugin_files.get(lua) != want:
            problems.append(f"{lua} in the bundle differs from plugin/{lua}")

    blob = Path(path).read_bytes()
    print(f"{path}: {len(members)} entries, {len(blob):,} bytes, "
          f"sha256 {hashlib.sha256(blob).hexdigest()[:16]}...")
    print(f"  {len(outside)} files outside mnt/onboard/.adds/:")
    for p in sorted(outside):
        print(f"    {p}")

    if problems:
        print("\nFAILED:", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        sys.exit(1)
    print("\n  plugin sources are byte-identical to plugin/")
    print("  every path is either under .adds/ or on the launcher allowlist")
    print("\nFull bundle verified.")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    main(sys.argv[1])
