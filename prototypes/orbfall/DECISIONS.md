# Orbfall — decisions & deviations log

One line per entry, per the brief's working agreement. Entries marked
DEVIATION break a brief requirement and say why.

- DEVIATION: built as a prototype inside the launcher (per request), not a
  standalone page — the launcher owns the window, main loop, and ESC/back.
- DEVIATION: no localStorage run persistence in this slice; the launcher's
  relaunch cycle replaces page-reload semantics. Revisit when standalone.
- DEVIATION: stock raylib font retained — the launcher has no asset pipeline
  yet (no --preload-file); typography is styled by scale/weight/color only.
- DEVIATION: single act of 8 floors instead of 3 acts (~10 min run, fits a
  prototype); the floor-scaling curve stands in for the act power curve.
- DEVIATION: on-device captures not possible in this environment; verification
  is resolver+pipeline unit tests plus a sanitized headless smoke test that
  plays whole runs through the real game loop (tests/smoke_test.c).
- Shop cut per brief §3f option; its function (spend-for-power) folds into
  events and the post-fight relic draft.
- 22 relics shipped, 18 shape/board-behavior + 4 utility (brief wants 24+/16;
  count trimmed to fit the slice, shape-to-utility ratio preserved).
- TPA "hits 2 enemies" rider dropped; TPA kept as exactly-4 multiplier only
  (P&D precedent intact, two-foe targeting UI not worth the slice budget).
- Jammer matches count as combos (P&D behavior) and feed Jammer Pact.
- Full-board clear is ceremony only (chord + flash + hold), no damage bonus —
  no P&D precedent for an inherent clear bonus; Decimator (10c) rewards it.
- Enemy "locks" use tape semantics (can't move, still matchable) — the drag
  puzzle is better than P&D's convert-lock for this game.
- Fixed-damage nuke ignores voids as well as DEF (P&D fixed damage does).
- Board size stays 6x5 everywhere; 7x6 leader riders were cut (fixed arrays).
