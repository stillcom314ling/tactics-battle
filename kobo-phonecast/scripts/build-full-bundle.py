#!/usr/bin/env python3
"""Builds the full KoboRoot.tgz: upstream KOReader + the KFMon launcher + the plugin.

Nothing upstream is patched, rewritten or "fixed" -- the two trees are unioned
verbatim and repacked. This script's whole job is to resolve, union and refuse:

  resolve  asset names come from the releases API, never from a hardcoded name
  union    a path present in both trees with differing bytes fails the build
  refuse   every path is checked against a policy before it is written

The policy is the interesting part. The archive is extracted as root over / on
a device with no recovery mode, so anything landing outside
mnt/onboard/.adds/ must be a path this script has been told about by name. An
upstream that starts shipping something new fails the build rather than
quietly installing it.

  python3 scripts/build-full-bundle.py --out dist/KoboRoot.tgz --notes dist/notes.md
"""

import argparse
import hashlib
import io
import json
import os
import re
import sys
import tarfile
import tempfile
import urllib.request
import zipfile
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

PLUGIN_DIR = "mnt/onboard/.adds/koreader/plugins/phonecast.koplugin"

# Asset patterns, most specific first. Names change upstream; these are matched
# against whatever the release actually offers and an empty or ambiguous match
# is a build failure, not a guess.
SOURCES = {
    "koreader": {
        "repo": "koreader/koreader",
        "patterns": [r"^koreader-kobo-.*\.zip$", r"^koreader-.*kobo.*\.zip$"],
    },
    "launcher": {
        "repo": "NiLuJe/KFMon",
        "patterns": [r"^KFMon-v?[\d.]+\.zip$", r"^KFMon-.*\.zip$"],
    },
}

# Paths permitted outside mnt/onboard/.adds/. Everything here belongs to the
# launcher, which cannot work from .adds/ alone: Nickel has to be told to start
# it, and that hook lives in the firmware's own init tree.
ALLOWED_OUTSIDE_ADDS = [
    re.compile(r"^usr/local/kfmon/"),
    re.compile(r"^usr/bin/kfmon-ipc$"),
    re.compile(r"^etc/udev/rules\.d/[\w.-]+\.rules$"),
    re.compile(r"^etc/init\.d/on-animator\.sh$"),
    re.compile(r"^etc/init\.d/kfmon$"),
    re.compile(r"^etc/rcS\.d/S\d+kfmon$"),
    # Icons Nickel shows in the library; these are what the launcher watches.
    re.compile(r"^mnt/onboard/[\w.-]+\.png$"),
    re.compile(r"^mnt/onboard/icons/[\w.-]+\.png$"),
]

# Never, under any circumstances.
BANNED = [
    re.compile(r"^mnt/onboard/\.kobo/"),
    re.compile(r"^(bin|sbin|lib|lib32|lib64|boot|dev|proc|sys)/"),
    re.compile(r"^usr/(bin|sbin|lib|share)/(?!kfmon)"),
]


def die(msg):
    print(f"\nBUILD FAILED: {msg}", file=sys.stderr)
    sys.exit(1)


def api(url):
    req = urllib.request.Request(url, headers={
        "Accept": "application/vnd.github+json",
        "User-Agent": "kobo-phonecast-release",
    })
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)


def resolve(name, spec):
    """Newest non-prerelease release, and the one asset matching our patterns."""
    releases = api(f"https://api.github.com/repos/{spec['repo']}/releases?per_page=20")
    release = next((r for r in releases if not r["prerelease"] and not r["draft"]), None)
    if release is None:
        die(f"{spec['repo']}: no published release found")

    names = [a["name"] for a in release["assets"]]
    for pattern in spec["patterns"]:
        rx = re.compile(pattern)
        hits = [a for a in release["assets"] if rx.match(a["name"])]
        if len(hits) == 1:
            print(f"  {name}: {release['tag_name']} -> {hits[0]['name']}")
            return release, hits[0]
        if len(hits) > 1:
            die(f"{spec['repo']}: {pattern!r} matched {len(hits)} assets: "
                f"{[h['name'] for h in hits]}. Narrow the pattern.")
    die(f"{spec['repo']} {release['tag_name']}: no asset matched {spec['patterns']}. "
        f"Assets offered: {names}")


def download(url):
    print(f"    fetching {url}")
    req = urllib.request.Request(url, headers={"User-Agent": "kobo-phonecast-release"})
    with urllib.request.urlopen(req, timeout=300) as r:
        return r.read()


# --------------------------------------------------------------------- trees

class Entry:
    """One archive member, normalised to a device-root-relative path."""

    def __init__(self, path, info, data):
        self.path = path
        self.info = info      # tarfile.TarInfo, modes preserved from upstream
        self.data = data      # bytes for files, None otherwise

    def key(self):
        return (self.info.type, self.info.mode, self.info.linkname,
                hashlib.sha256(self.data or b"").hexdigest())


def entries_from_tar(blob, origin):
    out = {}
    with tarfile.open(fileobj=io.BytesIO(blob), mode="r:*") as tf:
        for m in tf.getmembers():
            path = m.name.lstrip("./")
            if not path or path in (".", "/"):
                continue
            data = tf.extractfile(m).read() if m.isfile() else None
            out[path] = Entry(path, m, data)
    print(f"    {origin}: {len(out)} entries (device root)")
    return out


def entries_from_zip(blob, origin):
    """A Kobo-facing zip is the contents of /mnt/onboard. If it carries an
    inner .kobo/KoboRoot.tgz, that tarball is itself device-root-relative and
    is the real payload."""
    out = {}
    with zipfile.ZipFile(io.BytesIO(blob)) as zf:
        names = zf.namelist()
        inner = [n for n in names if n.rstrip("/").endswith(".kobo/KoboRoot.tgz")]
        if inner:
            out.update(entries_from_tar(zf.read(inner[0]), f"{origin}:{inner[0]}"))

        onboard = 0
        for n in names:
            if n.endswith("/"):
                continue
            if n in inner or n.startswith(".kobo/") or "/.kobo/" in n:
                continue          # never repack the nested installer
            info = tarfile.TarInfo("mnt/onboard/" + n)
            zi = zf.getinfo(n)
            data = zf.read(n)
            info.size = len(data)
            # Zip stores the unix mode in the top 16 bits of external_attr when
            # it was made on unix; fall back to 0644 when it wasn't. Upstream's
            # modes are carried across verbatim -- an executable that arrives
            # executable has to leave that way.
            mode = (zi.external_attr >> 16) & 0o7777
            info.mode = mode if mode else 0o644
            info.mtime = int(datetime(*zi.date_time).timestamp()) if zi.date_time else 0
            info.type = tarfile.REGTYPE
            out["mnt/onboard/" + n] = Entry("mnt/onboard/" + n, info, data)
            onboard += 1
        print(f"    {origin}: {onboard} entries under mnt/onboard/")

    if not out:
        die(f"{origin}: archive contained nothing usable. Members: {names[:40]}")
    return out


def normalise(name, blob):
    if blob[:2] == b"PK":
        return entries_from_zip(blob, name)
    if blob[:2] == b"\x1f\x8b" or blob[:5] == b"ustar":
        return entries_from_tar(blob, name)
    die(f"{name}: unrecognised archive format (magic {blob[:4]!r})")


# -------------------------------------------------------------------- policy

def check_policy(path):
    if path.startswith("/"):
        die(f"absolute path in archive: {path}")
    if ".." in path.split("/"):
        die(f"path escapes the archive root: {path}")
    for rx in BANNED:
        if rx.match(path):
            die(f"path is never allowed: {path}")
    if path.startswith("mnt/onboard/.adds/"):
        return
    for rx in ALLOWED_OUTSIDE_ADDS:
        if rx.match(path):
            return
    die(f"path lands outside mnt/onboard/.adds/ and is not on the launcher "
        f"allowlist: {path}\n"
        f"  Upstream has started shipping something new. Review it by hand and "
        f"add it to ALLOWED_OUTSIDE_ADDS if it is safe.")


# --------------------------------------------------------------------- build

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--notes", required=True)
    ap.add_argument("--local", action="append", default=[], metavar="NAME=PATH",
                    help="use a local archive instead of the releases API, for "
                         "exercising the union and policy passes offline")
    args = ap.parse_args()

    local = dict(pair.split("=", 1) for pair in args.local)

    print("Resolving upstream releases:")
    resolved = {}
    trees = {}
    for name, spec in SOURCES.items():
        if name in local:
            blob = Path(local[name]).read_bytes()
            print(f"  {name}: local file {local[name]}")
            resolved[name] = {"repo": spec["repo"], "tag": "local", "asset": local[name],
                              "sha256": hashlib.sha256(blob).hexdigest()}
        else:
            release, asset = resolve(name, spec)
            blob = download(asset["browser_download_url"])
            resolved[name] = {
                "repo": spec["repo"],
                "tag": release["tag_name"],
                "asset": asset["name"],
                "sha256": hashlib.sha256(blob).hexdigest(),
            }
        trees[name] = normalise(name, blob)

    print("\nUnioning trees:")
    merged = {}
    shared_identical = 0
    for name, tree in trees.items():
        for path, entry in tree.items():
            if path in merged:
                if merged[path].key() == entry.key():
                    shared_identical += 1
                    continue
                die(f"path collision between upstream trees: {path}\n"
                    f"  the two archives ship different content at this path; "
                    f"picking a winner is not this script's call")
            merged[path] = entry
    print(f"  {len(merged)} paths, {shared_identical} identical duplicates shared "
          f"between the two trees")

    # The plugin. No config.lua here -- main.lua's own defaults are the
    # configuration for the full bundle, and the page's builder is where
    # settings get baked in.
    for lua in ("_meta.lua", "main.lua"):
        data = (ROOT / "plugin" / lua).read_bytes()
        path = f"{PLUGIN_DIR}/{lua}"
        if path in merged:
            die(f"plugin path collides with upstream: {path}")
        info = tarfile.TarInfo(path)
        info.size = len(data)
        info.mode = 0o644
        merged[path] = Entry(path, info, data)
    print(f"  + phonecast.koplugin")

    print("\nChecking every path against policy:")
    for path in merged:
        check_policy(path)
    outside = sorted(p for p in merged if not p.startswith("mnt/onboard/.adds/"))
    print(f"  {len(merged)} paths pass; {len(outside)} sit outside .adds/ "
          f"(all launcher-owned)")

    # Explicit directory entries for every ancestor. Nickel's extractor will
    # not create missing parents, and upstream's own KoboRoot.tgz carries them
    # for the same reason. Ancestors of paths that already passed policy need
    # no separate check -- they cannot be anywhere policy did not allow.
    dirs = {}
    for path in merged:
        parts = path.split("/")[:-1]
        for i in range(1, len(parts) + 1):
            d = "/".join(parts[:i])
            if d and d not in merged and d not in dirs:
                info = tarfile.TarInfo(d)
                info.type = tarfile.DIRTYPE
                info.mode = 0o755
                info.mtime = 0
                dirs[d] = Entry(d, info, None)
    merged.update(dirs)
    print(f"  + {len(dirs)} directory entries")

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(out, "w:gz") as tf:
        for path in sorted(merged):
            entry = merged[path]
            info = entry.info
            info.name = path
            info.uid = info.gid = 0
            info.uname = info.gname = "root"
            info.mtime = int(info.mtime) or 0
            if entry.data is not None:
                info.size = len(entry.data)
                tf.addfile(info, io.BytesIO(entry.data))
            else:
                info.size = 0
                tf.addfile(info)

    blob = out.read_bytes()
    digest = hashlib.sha256(blob).hexdigest()
    print(f"\nWrote {out} -- {len(blob):,} bytes, sha256 {digest[:16]}...")

    notes = [
        "Drop `KoboRoot.tgz` into `.kobo` on the Kobo's USB drive, eject, and let it reboot.",
        "",
        "### Upstream, resolved at build time",
        "",
        "| Component | Release | Asset |",
        "|---|---|---|",
    ]
    for name, r in resolved.items():
        notes.append(f"| [{r['repo']}](https://github.com/{r['repo']}) | `{r['tag']}` | `{r['asset']}` |")
    notes += [
        "",
        f"Bundle: {len(merged):,} paths, {len(blob):,} bytes, `sha256:{digest}`.",
        "",
        "### Everything installed outside `.adds/`",
        "",
        "The launcher cannot start from `.adds/` alone. These are the only paths "
        "that land elsewhere, and the build fails if upstream adds one that isn't listed:",
        "",
        "```",
    ] + outside + ["```"]
    Path(args.notes).parent.mkdir(parents=True, exist_ok=True)
    Path(args.notes).write_text("\n".join(notes) + "\n")
    print(f"Wrote {args.notes}")


if __name__ == "__main__":
    main()
