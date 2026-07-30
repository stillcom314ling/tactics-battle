# Budget

Measured, not estimated. One row per milestone.

| Allocation | Limit | M1 | M2 | M3 |
|---|---|---|---|---|
| Steps, landing → rebooted device | 6 | — | — | — |
| Longest step, words | 25 | — | — | — |
| Scroll to reach download @360 px | none | — | — | — |
| Page transfer, excl. bundle | 150 KB | — | — | — |
| Web fonts | 2 | — | — | — |
| Type sizes, body and UI | 3 | — | — | — |
| Bundle generation | 500 ms, 0 requests | — | — | — |

## M1 — tar writer and verification harness

Nothing user-facing exists yet; the only numbers with meaning are the archive's.

- Plugin bundle: **3,895 bytes** gzipped (4 entries, 10,572 bytes of tar payload).
- `verify-bundle.sh`: passes, including negative tests — a single flipped
  checksum byte is rejected by both readers.
