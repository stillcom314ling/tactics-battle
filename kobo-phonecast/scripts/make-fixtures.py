#!/usr/bin/env python3
"""Builds stand-in KOReader and KFMon release archives.

The real ones are 40 MB and live behind github.com. These reproduce the two
shapes that matter -- a Kobo zip whose contents are /mnt/onboard, and one that
also carries an inner .kobo/KoboRoot.tgz holding device-root paths -- so the
union, the collision rule and the path policy can be exercised without the
network.

Layouts mirror what upstream's own Makefile produces.

  python3 scripts/make-fixtures.py <dir> [--collide] [--rogue]
"""

import argparse
import io
import sys
import tarfile
import zipfile
from pathlib import Path


def add_tar(tf, name, data, mode=0o644, symlink=None):
    info = tarfile.TarInfo(name)
    info.mode = mode
    info.uid = info.gid = 0
    info.uname = info.gname = "root"
    if symlink:
        info.type = tarfile.SYMTYPE
        info.linkname = symlink
        tf.addfile(info)
    else:
        info.size = len(data)
        tf.addfile(info, io.BytesIO(data))


def add_zip(zf, name, data, mode=0o644):
    info = zipfile.ZipInfo(name, date_time=(2099, 1, 1, 12, 0, 0))
    info.external_attr = mode << 16
    info.compress_type = zipfile.ZIP_DEFLATED
    zf.writestr(info, data)


def koreader_zip(path, collide=False):
    """koreader-kobo-<version>.zip: the contents of /mnt/onboard."""
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as zf:
        add_zip(zf, ".adds/koreader/reader.lua", "-- koreader entry point\n", 0o755)
        add_zip(zf, ".adds/koreader/koreader.sh", "#!/bin/sh\nexec ./reader.lua\n", 0o755)
        add_zip(zf, ".adds/koreader/plugins/coverbrowser.koplugin/main.lua", "-- upstream plugin\n")
        add_zip(zf, ".adds/koreader/data/dict/README", "dictionaries\n")
        add_zip(zf, "koreader.png", "PNG-icon-koreader" if not collide else "PNG-different")
    return path


def kfmon_zip(path, rogue=False):
    """KFMon-v<version>.zip: /mnt/onboard contents plus .kobo/KoboRoot.tgz,
    which holds the device-root half of the install."""
    inner = io.BytesIO()
    with tarfile.open(fileobj=inner, mode="w:gz") as tf:
        add_tar(tf, "usr/local/kfmon/bin/kfmon", b"\x7fELF-kfmon", mode=0o755)
        add_tar(tf, "usr/local/kfmon/bin/fbink", b"\x7fELF-fbink", mode=0o755)
        add_tar(tf, "usr/local/kfmon/LICENSE", b"GPLv3\n")
        add_tar(tf, "usr/bin/kfmon-ipc", b"", symlink="/usr/local/kfmon/bin/kfmon-ipc")
        add_tar(tf, "etc/udev/rules.d/99-kfmon.rules", b'ACTION=="add"\n')
        add_tar(tf, "etc/init.d/on-animator.sh", b"#!/bin/sh\n# kfmon\n", mode=0o755)
        if rogue:
            add_tar(tf, "usr/lib/libnickel.so.1.0.0", b"\x7fELF-firmware", mode=0o755)

    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as zf:
        add_zip(zf, ".kobo/KoboRoot.tgz", inner.getvalue())
        add_zip(zf, ".adds/kfmon/config/koreader.ini", "[watch]\nfilename=/mnt/onboard/koreader.png\n")
        add_zip(zf, ".adds/kfmon/config/kfmon.ini", "[daemon]\n")
        add_zip(zf, ".adds/kfmon/bin/kfmon-printlog.sh", "#!/bin/sh\n", 0o755)
        add_zip(zf, "koreader.png", "PNG-icon-koreader")
        add_zip(zf, "kfmon.png", "PNG-icon-kfmon")
        add_zip(zf, "icons/plato.png", "PNG-icon-plato")
    return path


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--collide", action="store_true",
                    help="make the two trees disagree at mnt/onboard/koreader.png")
    ap.add_argument("--rogue", action="store_true",
                    help="have the launcher ship a firmware library, which policy must refuse")
    a = ap.parse_args()

    d = Path(a.dir)
    d.mkdir(parents=True, exist_ok=True)
    print(koreader_zip(d / "koreader-kobo-v2099.1.1.zip", collide=a.collide))
    print(kfmon_zip(d / "KFMon-v1.99.0.zip", rogue=a.rogue))
