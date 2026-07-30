# kobo-phonecast

A static install page that hands someone a single `KoboRoot.tgz` which installs
KOReader and the Phone Cast plugin on a Kobo, pre-configured, in one drop.

Phone Cast runs a small HTTP server inside KOReader. Anything POSTed to it is
saved and shown full-screen with pan and zoom, so a phone screenshot lands on
the e-ink panel over Wi-Fi. The phone side needs nothing installed — the plugin
serves its own upload form.

## Layout

```
index.html                     the page
assets/style.css               monochrome, 16-level ramp, no web fonts
assets/app.js                  the dithered panel, and the in-browser builder
assets/tar.js                  ustar writer -- the load-bearing code
assets/plugin-files.js         GENERATED: plugin/*.lua embedded as strings
assets/fflate.min.js           vendored, the only dependency
plugin/{_meta.lua,main.lua}    the KOReader plugin, shipped byte-identical
scripts/                       generation, verification, screenshots
.github/workflows/release.yml  tag push -> full bundle attached to the release
```

## Two bundles, different rules

**Plugin bundle** — built in the browser when someone clicks *Build plugin*.
Every path starts `mnt/onboard/.adds/koreader/plugins/phonecast.koplugin/`; the
writer refuses anything else. Settings are baked into a generated `config.lua`;
`main.lua` ships byte-identical to `plugin/main.lua`.

**Full bundle** — built by GitHub Actions on a tag push. Upstream KOReader and
KFMon are unioned verbatim with the plugin. Asset names are resolved from the
releases API. A path present in both upstream trees with differing content
fails the build.

## Commands

```sh
node scripts/embed-plugin.mjs           # re-embed plugin/*.lua after editing them
node scripts/embed-plugin.mjs --check   # fail if the embedded copy has drifted
./scripts/verify-bundle.sh              # acceptance criteria 1-3
node scripts/shoot.mjs <label>          # milestone screenshots + page budgets
```

The full bundle needs the network. To exercise its union and policy passes
offline:

```sh
python3 scripts/make-fixtures.py /tmp/fx
python3 scripts/build-full-bundle.py --out /tmp/fx/KoboRoot.tgz --notes /tmp/fx/notes.md \
    --local koreader=/tmp/fx/koreader-kobo-v2099.1.1.zip \
    --local launcher=/tmp/fx/KFMon-v1.99.0.zip
python3 scripts/verify-full-bundle.py /tmp/fx/KoboRoot.tgz
```

`--collide` and `--rogue` on `make-fixtures.py` produce archives the build is
required to reject.

## Safety

The archive extracts as root over `/` on a device with no recovery mode. Both
bundles are checked twice — once on the way in by the writer, once on the way
out by a verifier reading the finished file. Nothing may contain `..` or a
leading `/`, nothing may write into `mnt/onboard/.kobo/`, and outside
`mnt/onboard/.adds/` only the launcher's own known paths are permitted. An
upstream that starts shipping something new fails the build rather than
installing it quietly.
