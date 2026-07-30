# Budget

Measured, not estimated.

| Allocation | Limit | M1 | M2 | M3 |
|---|---|---|---|---|
| Steps, landing → rebooted device | 6 | — | 6 | 6 |
| Longest step, words | 25 | — | 14 | 14 |
| Scroll to reach download @360 px | none | — | none (y 203–220) | none (y 264–312) |
| Page transfer, excl. bundle | 150 KB | — | 27 KB | **27.3 KB** |
| Web fonts | 2 | — | 0 | **0** |
| Type sizes, body and UI | 3 | — | — | **3** |
| Bundle generation | 500 ms, 0 requests | — | — | **2–8 ms, 0 requests** |

## M1 — tar writer and verification harness

Nothing user-facing exists yet; the only numbers with meaning are the archive's.

- Plugin bundle: 3,895 bytes gzipped, 4 entries.
- `verify-bundle.sh` passes, including negative tests: a single flipped checksum
  byte is rejected by both readers, and every unsafe path shape is refused
  before a byte is written.

## M2 — instructions and structure, unstyled

- 6 steps, longest 14 words, measured from the rendered DOM rather than counted
  by hand.
- Download control at y 203–220 in a 360 × 640 viewport, with browser default
  styles only.
- Screenshots: `screenshots/m2-unstyled-{360,360-fold,1280,360-nojs}.png`.

## M3 — visual direction and signature

Transfer, as GitHub Pages serves it (gzip):

| File | Raw | Gzipped |
|---|---|---|
| `index.html` | 7,140 | 2,925 |
| `assets/style.css` | 7,027 | 2,212 |
| `assets/app.js` | 9,845 | 3,254 |
| `assets/tar.js` | 9,881 | 3,415 |
| `assets/plugin-files.js` | 11,195 | 3,749 |
| `assets/fflate.min.js` | 32,665 | 12,500 |
| **Total** | **77,753** | **28,055 (27.3 KB)** |

- Fonts: 0 files. System grotesque and system monospace.
- Body and UI type sizes: 3 (13 px, 16 px, 19 px). Display adds 2 more (`h1`
  clamped 34–56 px, `h2` 21 px).
- Colour values: 5 tokens, all on the panel's 16-level ramp, all repeated hex
  digits. No hue anywhere.
- Bundle generation, measured in Chromium over 5 runs: 8, 7.6, 2.1, 4.3, 2.7 ms.
  Zero network requests. The browser's output is byte-identical to the
  harness's, so `verify-bundle.sh` covers what a visitor actually downloads.
- Signature: black → white → inverted → settled, complete by ~350 ms, once,
  skipped entirely under `prefers-reduced-motion`.
- Screenshots: `screenshots/m3-styled-{360,360-fold,1280,360-nojs}.png`.

## M4 — release

- Full bundle from fixtures: 18 paths + 22 directory entries, both negative
  cases rejected (upstream path collision; launcher shipping a firmware
  library).
- Not measured against real upstream archives: `github.com` is unreachable from
  the build environment, so the workflow has never run. See DECISIONS.md.
