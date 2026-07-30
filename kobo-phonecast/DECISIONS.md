# Decisions

Every deviation from the brief, one line each, with the reason.

## The one that matters

**The full bundle installs files outside `mnt/onboard/.adds/`, which acceptance
criterion 2 forbids.** There is no launcher that does not. Nickel has to be told
to start KOReader, and every mechanism for telling it — KFMon, NickelMenu, any
of them — puts a binary or a hook where the firmware will find it. A bundle that
obeys the letter of the constraint installs a KOReader that cannot be started,
which fails the prime directive completely. So:

- The rule is narrowed rather than dropped. Outside `.adds/`, only paths on an
  explicit allowlist are permitted (`usr/local/kfmon/`, `usr/bin/kfmon-ipc`, a
  udev rule, the init hook, the launcher's icons). Anything else fails the
  build, loudly, with the path named.
- `mnt/onboard/.kobo/`, `..`, leading `/`, and the firmware's own `bin/`,
  `lib/`, `usr/lib/`, `usr/share/` stay banned outright.
- KFMon replaces one firmware script, `/etc/init.d/on-animator.sh`. That is how
  every KOReader install on a Kobo works, and the page says so in plain words
  rather than hiding it.
- The **plugin bundle** — the one the page generates, and the one acceptance
  criteria 1–3 describe — is unaffected. Every path in it starts
  `mnt/onboard/.adds/koreader/plugins/phonecast.koplugin/`, asserted before a
  byte is written and checked again after.

## Structure

- **Lives in `kobo-phonecast/` rather than at repo root.** The host repo is an
  unrelated raylib project whose Pages workflow already owns the root.
- **`release.yml` sits in `kobo-phonecast/.github/workflows/`, where GitHub will
  not run it.** Copying it to the repo root would add a release workflow to the
  unrelated project; the file says what to do to activate it.
- **`_meta.lua` and `main.lua` are embedded in `assets/plugin-files.js` rather
  than fetched.** The budget forbids network requests during generation.
  Byte-identity is enforced by `scripts/embed-plugin.mjs --check`, which
  `verify-bundle.sh` runs first, so the two copies cannot drift.

## The archive

- **Fixed mtime (2025-01-01) on every plugin-bundle entry.** Makes the archive
  byte-reproducible, which turns "only `config.lua` differs" into something
  checkable at the byte level rather than only after extraction.
- **One directory entry in the plugin bundle, not one per ancestor.** Emitting
  `mnt/`, `mnt/onboard/` … would put paths in the archive that do not begin
  with the required prefix, contradicting acceptance criterion 1. The full
  bundle does emit every ancestor, because it creates trees that may not exist.
- **`verify-bundle.sh` reads each archive with two independent tar
  implementations** (GNU tar and Python `tarfile`). The named failure mode — a
  checksum some tools accept and the device rejects in silence — is only
  catchable by disagreeing readers. A flipped checksum byte is rejected by both.
- **The full bundle preserves upstream file modes; only the plugin bundle
  normalises to 0644/0755.** KFMon ships executables, and 0644 on `kfmon` would
  produce a launcher that cannot launch.

## The plugin

- **The config loader is wrapped whole in the `pcall`, and `dir` falls back to
  `./`.** The brief's snippet calls `:match()` outside the `pcall`;
  `debug.getinfo(1,"S").source` carries no directory when the file is loaded by
  a relative path, so `dir` is nil and concatenating it takes the entire plugin
  down at load time. Verified against a real Lua 5.1: present, absent, partly
  wrong and corrupt configs all now degrade to defaults.
- **`main.lua` gained a nil-guard on `io.open`.** The guide's version answers
  `ok` for an image it failed to write — the one failure the sender cannot
  diagnose. It now answers 500 and logs.
- **The full bundle ships no `config.lua`.** The plugin's own defaults are the
  configuration there; baking settings in is the page builder's job.

## The page

- **The prebuilt full bundle is the primary download; the client-generated
  plugin bundle is secondary.** The visitor wants KOReader *and* the plugin in
  one drop, and a plain `<a href>` is the only download that survives
  acceptance criterion 6.
- **A one-line precondition sits above the six steps.** It is not a step — it
  names the one thing (a data-capable USB cable) that otherwise makes step 2
  fail with no symptom.
- **Phone-side setup is one paragraph pointing at the plugin's own upload
  form.** The guide's Tasker recipe is long and version-dependent; the browser
  form needs no setup at all. Tasker is named as an option, not reproduced.
- **No web fonts.** Two subset faces would cost 30–60 KB of the 150 KB budget
  for a page whose entire visual argument is a 1-bit panel. System grotesque
  and system monospace; the page ships at 27 KB.
- **No dark mode.** The subject is paper.
- **The hero renders a synthesised phone screenshot rather than a photograph.**
  A real screenshot would be the heaviest asset on the page and would date
  within a year; drawing it costs about a kilobyte and dithers identically.
- **That synthesised screenshot contains continuous tone**, which the brief's
  "no gradient anywhere" bans. The ban is aimed at decorative gradients in the
  page's own chrome, and there are none: tone inside the simulated photograph is
  the one thing the hero exists to show, because dithering tone is what the
  software does. Every pixel the visitor sees is pure black or pure white.
- **The wide layout is a single 46rem column, not text beside the panel.** The
  two-column version left the right half of the page empty below the hero, which
  reads as unfinished. Screenshot at `screenshots/m3-styled-1280.png`.
