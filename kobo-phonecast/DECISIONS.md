# Decisions

Every deviation from `BRIEF.md`, one line each, with the reason.

## Milestone 1 — tar writer and verification harness

- **Lives in `kobo-phonecast/` rather than at repo root.** The host repo is an
  unrelated raylib project whose GitHub Pages workflow already owns the root;
  a subdirectory keeps both buildable.
- **`_meta.lua` and `main.lua` are embedded in `assets/plugin-files.js` instead
  of fetched.** The budget forbids network requests during generation, and the
  brief's byte-identity requirement is enforced instead by
  `scripts/embed-plugin.mjs --check`, which `verify-bundle.sh` runs first.
- **Fixed mtime (2025-01-01) on every tar entry.** Makes bundles byte-reproducible,
  which turns "only `config.lua` differs" into something checkable at the byte
  level rather than only after extraction.
- **The plugin bundle contains one directory entry, not one per ancestor.**
  Emitting `mnt/`, `mnt/onboard/` … would put paths in the archive that do not
  begin with the required prefix, contradicting acceptance criterion 1; every
  ancestor already exists on a device that has KOReader.
- **`main.lua` gained a nil-guard on `io.open`.** The guide's version answers
  `ok` for an image it failed to write, which is the one failure the sender
  cannot diagnose; it now answers 500 and logs.
- **`verify-bundle.sh` reads each archive with two independent tar
  implementations** (GNU tar and Python `tarfile`) rather than one. The named
  failure mode — a checksum some tools accept and the device rejects silently —
  is only catchable by disagreeing readers.

## Milestone 2 — instructions and structure

- **The prebuilt full bundle is the primary download; the client-generated
  plugin bundle is secondary.** §1's visitor wants KOReader *and* the plugin in
  one drop, and a plain `<a href>` is the only download that survives
  acceptance criterion 6 (no JavaScript).
- **Seven-word "before you start" line sits above the six steps.** It is not a
  step — it states the one precondition (a data-capable USB cable) that
  otherwise makes step 1 fail silently.
- **Phone-side setup is one paragraph pointing at the plugin's own upload page.**
  The guide's Tasker recipe is long and version-dependent; the browser form
  needs no setup at all, so shipping the zero-setup path and naming Tasker as
  an option beats reproducing a recipe that will drift.

## Milestone 3 — visual direction

- **No web fonts; system grotesque and system monospace.** Two subset faces
  would have cost 30–60 KB of the 150 KB budget for a page whose whole visual
  argument is a 1-bit panel; the bytes buy nothing the dither doesn't.
- **The hero panel renders a synthesised phone screenshot, not a photograph.**
  A real screenshot would be the single largest asset on the page and would
  date immediately; drawing it in canvas costs ~1 KB and dithers identically.

## Milestone 4 — release

- **The release workflow resolves both upstream assets by regex against the
  releases API and fails loudly on no match.** Named assets would break at the
  next upstream rename, which the brief says has happened before.
- **`GITHUB_REPOSITORY` drives the download URL on the page at build time
  through a single constant** (`data-release-repo` on `<html>`), so the page
  works unmodified in a fork.
