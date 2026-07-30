#!/usr/bin/env bash
# Enforces acceptance criteria 1-3 of the brief.
#
# The device has no recovery mode: a malformed archive extracted as root over /
# is not a failed download, it is a brick. This script is the whole safety
# story, so it re-reads what the generator produced with two independent tar
# implementations rather than trusting the writer's own view of its output.
#
#   scripts/verify-bundle.sh [bundle.tgz]
#
# With no argument it generates two bundles with different settings and checks
# both, plus the "only config.lua differs" invariant.
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

fail() { printf 'FAIL  %s\n' "$*" >&2; exit 1; }
pass() { printf 'ok    %s\n' "$*"; }

PREFIX='mnt/onboard/.adds/koreader/plugins/phonecast.koplugin/'

# ---------------------------------------------------------------- one archive

check_archive() {
    bundle=$1
    label=$2
    dest=$3

    [ -s "$bundle" ] || fail "$label: bundle is empty or missing"
    gzip -t "$bundle" 2>/dev/null || fail "$label: not valid gzip"
    pass "$label: valid gzip ($(wc -c < "$bundle" | tr -d ' ') bytes)"

    tar tzvf "$bundle" > "$work/listing.txt" 2>"$work/tar.err" \
        || fail "$label: GNU tar cannot read the archive: $(cat "$work/tar.err")"
    [ -s "$work/tar.err" ] && fail "$label: tar warned: $(cat "$work/tar.err")"
    pass "$label: GNU tar reads it without warnings"

    # Second, stricter reader. Python's tarfile validates the header checksum
    # independently -- the failure mode the brief names by name.
    BUNDLE="$bundle" LABEL="$label" PREFIX="$PREFIX" python3 - <<'PY' || exit 1
import os, sys, tarfile

bundle, label, prefix = os.environ["BUNDLE"], os.environ["LABEL"], os.environ["PREFIX"]
forbidden = ("usr/", "etc/", "bin/", "lib/", "sbin/", "opt/", "var/", "dev/", "proc/", "sys/",
             "mnt/onboard/.kobo/")

def bad(msg):
    print(f"FAIL  {label}: {msg}", file=sys.stderr)
    sys.exit(1)

try:
    tf = tarfile.open(bundle, "r:gz")
except tarfile.TarError as e:
    bad(f"python tarfile rejects the archive ({e}) -- header checksum or layout is wrong")

members = tf.getmembers()
if not members:
    bad("archive is empty")

for m in members:
    p = m.name
    # 6.1 -- every path lives under the plugin directory.
    if not (p + "/" if m.isdir() else p).startswith(prefix):
        bad(f"path outside {prefix}: {p}")
    # 6.2 -- nothing escapes, nothing touches the system tree.
    if p.startswith("/"):
        bad(f"absolute path: {p}")
    if ".." in p:
        bad(f"path contains '..': {p}")
    for f in forbidden:
        if p.startswith(f):
            bad(f"path writes to {f}: {p}")
    # 6.1 -- ownership and modes.
    if (m.uid, m.gid, m.uname, m.gname) != (0, 0, "root", "root"):
        bad(f"not root/root: {p} ({m.uid}/{m.gid} {m.uname}/{m.gname})")
    if m.isdir():
        if m.mode != 0o755:
            bad(f"directory mode {m.mode:o}, want 755: {p}")
    elif m.isfile():
        if m.mode != 0o644:
            bad(f"file mode {m.mode:o}, want 644: {p}")
    else:
        bad(f"unexpected entry type {m.type!r}: {p}")

if not any(m.isdir() for m in members):
    bad("no explicit directory entry -- Nickel's extractor needs one")

names = {m.name for m in members}
for required in ("_meta.lua", "main.lua", "config.lua"):
    if prefix + required not in names:
        bad(f"missing {required}")

print(f"ok    {label}: {len(members)} entries, all under {prefix}, root/root, 0755/0644")
print(f"ok    {label}: python tarfile accepts every header checksum")
PY

    mkdir -p "$dest"
    tar xzf "$bundle" -C "$dest"
    for f in _meta.lua main.lua; do
        cmp -s "$root/plugin/$f" "$dest/$PREFIX$f" \
            || fail "$label: extracted $f differs from plugin/$f"
    done
    pass "$label: _meta.lua and main.lua are byte-identical to plugin/"
}

# ------------------------------------------------------------------- harness

node "$root/scripts/embed-plugin.mjs" --check || fail "embedded plugin sources are stale"

if [ $# -ge 1 ]; then
    check_archive "$1" "$(basename "$1")" "$work/x1"
    printf '\nAll checks passed.\n'
    exit 0
fi

node "$root/scripts/generate-bundle.mjs" "$work/a.tgz" 8080 1 30 33554432 "" > /dev/null
node "$root/scripts/generate-bundle.mjs" "$work/b.tgz" 9090 3 5 8388608 "/mnt/onboard/cast" > /dev/null

check_archive "$work/a.tgz" "defaults" "$work/xa"
echo
check_archive "$work/b.tgz" "custom settings" "$work/xb"
echo

# 6.3 -- settings reach config.lua and nothing else.
differing=$(diff -rq "$work/xa/$PREFIX" "$work/xb/$PREFIX" | sed 's/.*and //; s/ differ$//' | xargs -n1 basename 2>/dev/null || true)
[ "$differing" = "config.lua" ] \
    || fail "expected only config.lua to differ between settings, got: ${differing:-<none>}"
pass "only config.lua differs between two bundles built with different settings"

grep -q 'port = 9090' "$work/xb/$PREFIX/config.lua" || fail "custom port missing from config.lua"
grep -q 'save_dir = "/mnt/onboard/cast"' "$work/xb/$PREFIX/config.lua" || fail "custom save_dir missing"
pass "config.lua carries the requested settings"

printf '\nAll checks passed.\n'
