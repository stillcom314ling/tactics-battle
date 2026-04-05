# Triad — Game Design Document

## Overview

A single-player roguelike puzzle game inspired by Final Fantasy's Triple Triad. Place tiles on a cross-shaped grid, using side values to flip adjacent tiles via three mechanics: **Higher**, **Same**, and **Sum**. Chain reactions from Same/Sum create big scoring moments. After enough flips, tiles vanish to free space. Score enough points before the board fills to advance.

Between rounds, collect **Mods** (persistent passive effects) and **Consumables** (one-time-use items) to build your strategy. Runs escalate in difficulty across 6+ rounds until the board overwhelms you.

---

## Core Mechanics

### Grid Layout

The board is a **4×4 grid with corners removed**, forming a cross/diamond shape with **12 playable cells**.

```
    [  ] [01] [02] [  ]
    [10] [11] [12] [13]
    [20] [21] [22] [23]
    [  ] [31] [32] [  ]
```

Corner cells (0,0), (0,3), (3,0), (3,3) are permanently empty. This tighter board creates natural pressure — the board fills faster, edge cells have fewer neighbors (making Same/Sum harder from edges), and play funnels toward the center.

### Tile Anatomy

Each tile has **4 numbered sides** — one value per edge: North, East, South, West (indices 0,1,2,3).

### Tile Types

| Type | Description | Value Range |
|------|-------------|-------------|
| **Neutral** (◆) | Pre-placed on board at round start | 1–7 (varies by round) |
| **Player** (●) | Drawn into your hand | 1–9 |

### Flip Count & Tile Colors

Tiles track how many times they've been flipped. Visual color indicates status. The **vanish threshold** (number of flips before vanishing) increases across rounds.

The color ramp always progresses from warm beige → orange → red, with the final pre-vanish state being the deepest red regardless of how many steps there are.

| Flip Count | Color (bg hex) | Meaning |
|------------|---------------|---------|
| 0 (new) | `#eee4da` | Just placed, never flipped |
| 1 | `#f2b179` | Flipped once |
| 2 | `#f67c5f` | Flipped twice |
| 3+ | `#e85d3a` → `#d43527` | Approaching vanish |
| Neutral | `#b8a99a` | Neutral tile, not yet flipped |

**On reaching the vanish threshold → tile vanishes** (removed from board), freeing the space. Vanish bonus: **+25 pts**.

---

## The Three Flip Rules

When you place a tile, check all adjacent occupied tiles:

### 1. Higher (Setup Move)

**Your side value > their opposing side value → flip that tile.**

- Points: **+5 per flip**
- **Does NOT trigger chains.** This is a positioning tool — flip tiles to rearrange the board for bigger plays.

### 2. Same (Scoring Move)

**2 or more adjacent sides where your value = their opposing value → flip ALL matching tiles.**

- Points: **+15 per flip**
- **Triggers chains.** Flipped tiles then use the Higher rule against THEIR neighbors, cascading.

### 3. Sum (Scoring Move)

**2 or more adjacent sides where (your value + their opposing value) produces the same sum → flip ALL matching tiles.**

- Points: **+15 per flip**
- **Triggers chains** (same as Same).

Example: You place a tile with East=3 next to a tile with West=5 (sum=8), and your South=2 next to a tile with North=6 (sum=8). Both sums equal 8 → both tiles flip.

### Chain Resolution

Only tiles flipped by Same or Sum enter the chain queue. Chain uses the Higher rule (value > opposing value) and awards escalating points:

- Chain depth 2: **+20 per flip** (10 × depth)
- Chain depth 3: **+30 per flip** (10 × depth)
- And so on...

If a chain-flipped tile would reach the vanish threshold, it vanishes instead (+25 bonus).

---

## Rotation

Players can **rotate** a selected hand tile before placing it. Tapping/clicking a selected tile rotates its values clockwise: `[N,E,S,W]` → `[W,N,E,S]`.

This is critical for lining up Same/Sum combos.

---

## Run Structure

A run is a sequence of **rounds** with escalating difficulty. The player progresses until they lose.

### Round Flow

1. **Play Phase** — Place tiles, score points. The round continues until the board fills OR the player chooses to end it early.
2. **Round End** — The round ends in one of two ways:
   - **Board fills** (all 12 cells occupied) — regardless of tiles in hand. If score ≥ target, advance. If not, run over.
   - **Player ends early** — Once the target score is met, a button appears to end the round immediately. The player can keep playing to build a buffer, or cash out.
3. **Draft Phase** — Player drafts 1 Mod and 1 Consumable.
4. **Next Round Begins** — Fresh board, new neutrals, difficulty escalates. Hand is dealt fresh (no carry-over). Consumable slots persist.

### Losing

The run ends when the **board fills up** and the player hasn't hit the target score. It does not matter if the player still has tiles in hand. A full board ends the round.

Board management (vanishing tiles to free space) is the core survival mechanic. Scoring enough before the board locks up is the win condition.

### Round Scoring

Each round has an independent score. There is no cumulative total across rounds — each round is **pass/fail** against its target. Hit the target → advance to draft → next round. Miss the target → run over.

### Difficulty Escalation

| Round | Neutral Count | Neutral Value Range | Target Score | New Neutrals per Draw | Flips to Vanish |
|-------|--------------|--------------------|--------------|-----------------------|-----------------|
| 1 | 3 | 1–4 | 1,000 | 0 | 3 |
| 2 | 3 | 1–5 | 1,500 | 0–1 | 3 |
| 3 | 4 | 1–6 | 2,000 | 1 | 4 |
| 4 | 4 | 2–7 | 2,750 | 1 | 4 |
| 5 | 5 | 2–7 | 3,500 | 1 | 5 |
| 6 | 5 | 3–7 | 4,500 | 1 | 5 |
| 7+ | 6 | 3–7 | +1,000/round | 1–2 | 6 |

Neutral counts are tuned for the 12-cell board. On a 12-cell grid, 3 neutrals leave 9 open cells; 5 neutrals leave 7.

Difficulty levers in order of impact: vanish threshold (tiles harder to clear), neutral spawn rate (board fills faster), neutral count (less starting space), neutral value range (harder to Higher-flip), target score (need more points from a tighter board).

---

## Game Flow (Turn Loop)

### Setup (Each Round)
1. Board is populated with neutrals at random positions on the cross-shaped grid
2. Player receives **4 tiles** in hand

### Turn Loop
1. **Select** a tile from hand (tap it)
2. **(Optional) Rotate** by tapping the selected tile again
3. **(Optional) Use a consumable** (tap consumable slot, then tap target)
4. **Place** on any empty cell (tap the board)
5. **Resolution** plays out in sequence:
   - Same/Sum flips animate first (+15 each)
   - Higher flips animate next (+5 each)
   - Chains cascade from Same/Sum flips (escalating points)
   - Vanishes animate with a shrink effect
6. When hand is empty, **draw 3 new tiles** + possibly spawn new neutrals (per round config)

---

## Scoring Summary

| Action | Points | Chains? |
|--------|--------|---------|
| Higher flip | +5 | No |
| Same flip | +15 | Yes |
| Sum flip | +15 | Yes |
| Chain flip (depth N) | +10 × N | Yes (continues) |
| Vanish (reaching threshold) | +25 bonus | — |

---

## Mods (Passive Effects)

Mods are persistent passive effects collected in the Draft Phase. They last the entire run (or until swapped out). The player can hold up to **5 Mods**. Mods cannot be voluntarily discarded or swapped until the rack is full at 5. Once full, drafting a 6th requires swapping one out.

### Scoring Mods

**Overachiever** (Common) — +5 bonus points every time you trigger a Same or Sum flip.

**Chain Reaction** (Uncommon) — Chain flips score +15×N instead of +10×N per depth level.

**Vanishing Act** (Common) — Vanish bonus increased from +25 to +50.

**Perfectionist** (Rare) — +100 bonus points if you vanish every neutral tile in a round.

**Lucky Sevens** (Uncommon) — Any flip involving a side with value 7 scores double.

**Greed** (Uncommon) — +3 points for every tile remaining on the board at round end.

**Streak** (Rare) — After 3 consecutive placements that trigger Same or Sum, all scoring is 2× until the streak breaks.

### Board Mods

**Aftershock** (Uncommon) — When a tile vanishes, all adjacent tiles get +1 flip count.

**Sprawl** (Rare) — Board gains 2 extra random cells each round.

**Quicksand** (Uncommon) — Unflipped neutrals auto-gain +1 flip count every 4 placements.

**Magnetic** (Common) — Placing a tile rotates one random adjacent neutral's values clockwise.

**Bedrock** (Common) — Your placed tiles cannot be flipped by chain reactions (only by direct Same/Sum).

### Hand & Draw Mods

**Deep Draw** (Common) — Draw 4 tiles instead of 3 when hand empties.

**Sculptor** (Uncommon) — Once per round, adjust one value on a hand tile by ±1.

**Twins** (Uncommon) — 30% chance two drawn tiles are identical.

**Oracle** (Common) — See the next 3 tiles you'll draw.

**Recycler** (Rare) — When hand empties, reclaim 1 of your board tiles to hand (frees the cell).

### Flip Rule Mods

**Equalizer** (Uncommon) — Higher flips now also trigger chains.

**Resonance** (Rare) — Same flip → all tiles on board with that matched value get +1 flip count.

**Reversal** (Uncommon) — Lower flips also work (+5, no chains).

**Sum Shift** (Common) — Sum works with just 1 matching pair (+10 instead of +15).

---

## Consumables

One-time-use items occupying **2 consumable slots**. Slots persist across rounds. Using a consumable is an action taken on your turn (before or after placing a tile, one use per turn).

Players can also **save a hand tile** to a consumable slot — drag it from hand to an empty slot. This is the only way to preserve a tile across rounds, since the hand doesn't carry over.

### Tile Consumables (Placed on the board)

**Wildcard Tile** — All sides match ANY value for Same/Sum. Cannot be flipped.

**Bomb Tile** — Vanishes itself + all adjacent tiles instantly. +25 per tile vanished.

**Mirror Tile** — Copies values of one adjacent tile of your choice.

**Anchor Tile** — No values, can't be flipped. Vanishes when a neighbor vanishes (+40 pts).

### Action Consumables (Tap to activate, then target)

**Rotate** — Rotate a board tile's values one step clockwise.

**Nudge** — Move a board tile to an adjacent empty cell (preserves flip count).

**Flux** — Set all of a tile's values to the same random number (1–9).

**Pulse** — Target an empty cell; all adjacent tiles get +1 flip count.

**Mulligan** — Discard entire hand, draw fresh tiles.

**Freeze** — Target tile can't be flipped for 3 placements.

**Shatter** — Remove any tile instantly (no points). Frees the cell.

---

## Draft Phase

After each successful round, the player drafts upgrades.

### Draft Flow

1. **3 offerings appear** — random mix of Mods and Consumables, tier-weighted by round.
2. **Pick one.** Mods go to the mod rack. Consumables go to a slot.
3. **Offerings refresh to 3 new cards of the opposite type.** Picked a Mod → now see Consumables. Picked a Consumable → now see Mods.
4. **Pick the second item.**
5. **Done** — proceed to next round.

### Rerolls

**2 rerolls per draft phase.** Reroll replaces all current offerings with fresh ones (respecting type filter if active). Rerolls do NOT carry over. When exhausted, the reroll button becomes **Skip**. Skip forfeits the current pick.

### Draft Pool Weighting

| Tier | Available From | Weight |
|------|---------------|--------|
| Common | Round 1+ | High early, decreasing |
| Uncommon | Round 2+ | Medium throughout |
| Rare | Round 4+ | Low, increasing |

---

## Synergy Clusters

Mods and Consumables are designed to cluster into build archetypes:

**The Vanisher** — Vanishing Act + Aftershock + Perfectionist. Consumables: Pulse, Bomb. Flip everything to vanish. Scales into late rounds where clearing tiles is critical.

**The Chain Builder** — Chain Reaction + Equalizer + Streak. Consumables: Wildcard, Rotate. Engineer long cascading chains for massive point bursts.

**The Sculptor** — Sculptor + Oracle + Deep Draw. Consumables: Rotate, Nudge, Mirror. Precision play — see what's coming, adjust values, reposition tiles.

**The Gambler** — Sum Shift + Twins + Magnetic. Consumables: Flux, Mulligan. Embrace randomness. Flux is a wild swing, Mulligan bails you out.

**The Survivor** — Sprawl + Deep Draw + Bedrock + Recycler. Consumables: Shatter, Nudge, Bomb. Stay alive. Extra cells, board clearing, and defensive positioning.

---

## Edge Highlight Colors (Preview)

When a tile is selected from hand, adjacent sides of board tiles light up to show what would happen if placed nearby:

| Color | Hex Code | Meaning |
|-------|----------|---------|
| Purple | `#8e6bbf` | **Sum** — this side participates in a Sum combo (2+) |
| Gold | `#d4a843` | **Same** — this side has equal value to yours |
| Green | `#6bab68` | **Higher** — your side beats this one |
| Red | `#c96b64` | **Lower** — their side beats yours |

Priority: Sum > Same > Higher > Lower.

---

## Point Preview

When a tile is selected, each empty cell shows the **total points** that placement would earn (runs a full simulation including chains).

- `0` in faded text → no flips
- `+5`, `+10` → basic flips
- `+30+` → high-value plays

---

## UI Layout

### Round HUD (Top)
- Round number + vanish threshold indicator (e.g., "💀3")
- Score / target display
- Score progress bar (fills left to right, changes color when target met)
- "End Round" button appears when target is met
- Board occupancy warning at 75%+ (cells pulse orange/red)

### Mod Rack
- Horizontal row of up to 5 mod badges above/below the board
- Tap to see full description
- No removal until rack is full at 5

### Board (Center)
- 4×4 grid with corners removed (12 cells)
- Step log below board showing last placement results (same +15, chain ×2 +20, etc.)

### Hand + Consumables (Bottom)
- 2 consumable slots (left) + 4 hand tiles (right)
- Save tile to slot: select hand tile → tap ↓ button
- Consumable items show name; stored tiles show their values with "saved" badge
- Empty slot shows dashed outline with "+"

### Draft Screen (Between Rounds)
- Full-screen overlay with round summary
- 3 offering cards with name, type tag, tier color, description
- Reroll button with counter → becomes Skip at 0
- Current mod rack shown below

---

## Visual Style

Flat pastel aesthetic inspired by 2048:

- **Background**: `#faf8ef` (warm cream)
- **Board container**: `#bbada0` (brown) with rounded corners
- **Empty cells**: `#cdc1b4` (light brown), hover: `#d6cdc4`
- **Typography**: Clear Sans / Helvetica Neue, color `#776e65`
- **Score boxes**: `#bbada0` background, white text
- **Animations**: Minimal — pop on flip, shrink on vanish, float-up score text

---

## Grid Specification

- **Layout**: 4×4 with corners removed = 12 cells
- **Directions**: 4 (N, E, S, W)
- **Values per tile**: 4 (one per side)
- **Opposite side**: `(dir + 2) % 4`
- **Rotation**: `[N,E,S,W]` → `[W,N,E,S]`
- **Cell key**: `"row,col"` string
- **Corner exclusion**: cells (0,0), (0,3), (3,0), (3,3) are not valid

---

## Technical Architecture

### Data Structures

```javascript
// Token/Tile
{
  id: "p5",           // unique ID
  type: "player",     // "player" | "neutral"
  values: [3, 7, 2, 5], // side values (N, E, S, W)
  flipCount: 0        // 0 to vanishThreshold-1 (vanishes at threshold)
}

// Game State
{
  board: { "0,1": token, ... },  // key → token map
  hand: [token, token, ...],     // player's hand
  cellSet: Set(["0,1", "0,2", ...]), // valid cell keys (excludes corners)
  score: 0,                      // current round score
  round: 0,
  mods: [...],                   // active mods
  consumableSlots: [item|null, item|null],
  turnCount: 0,
}

// Round Config
{
  neutrals: 3,        // starting neutral count
  valRange: [1, 4],   // neutral value range
  target: 1000,       // score needed to advance
  spawnRate: 0,        // neutrals spawned per draw (0, 0.5, 1, etc.)
  vanishAt: 3,         // flips needed to vanish
}
```

### Resolution Algorithm

```
resolveSteps(board, cellKey, tile, vanishAt, mods):
  1. Place token on board
  2. Gather all adjacent occupied tiles with comparisons
  3. Detect Same matches (2+ equal values)
  4. Detect Sum matches (2+ equal sums)
  5. Execute Same/Sum flips (15pts, add to chainQueue)
  6. Execute Higher flips (5pts, NO chain)
  7. Apply mod effects (Overachiever bonus, Resonance, etc.)
  8. BFS chain from chainQueue:
     - For each flipped tile, check neighbors
     - If flipped tile's value > neighbor's opposing value → flip
     - Points = 10 × depth (or 15 × depth with Chain Reaction mod)
     - Add to next wave
  9. Process vanishes (tiles at threshold → remove, +25pts)
  10. Apply Aftershock if active (vanish neighbors +1 flip)
  11. Return { board, steps[], points }
```

### Animation Sequencing

Steps are animated with delays:
- Each flip: 420ms apart
- Each vanish: 550ms apart
- Active flip/vanish tracked in state for per-tile animation
- Input locked during resolution (`animating` flag)

### Highlight Computation

For each empty cell adjacent to board tiles:
1. Compare selected hand token's values against all neighboring tiles
2. Detect if Same (2+) or Sum (2+) would trigger from that cell
3. Assign edge colors by priority: Sum > Same > Higher > Lower
4. For point preview: run full `resolveSteps` simulation per empty cell

---

## Open Questions & Tuning Notes

**Board pressure balance.** The 12-cell cross board is tighter than the original 16-cell grid but vanishing still keeps cells open. If rounds still run too long, options include: reducing to a 3×3 grid (9 cells), increasing neutral spawn rate, or adding an in-round vanish threshold ramp (e.g., +1 every N placements). These were tested — the in-round ramp was too aggressive. The cross board alone provides good pressure through round 3-4; later rounds rely on vanish threshold scaling.

**Vanish threshold and Vanishing Act interaction.** At 6 flips to vanish, Vanishing Act (+50) applies to fewer vanishes. Consider having it also grant +1 flip count to neighbors, or accept it as an early/mid-game mod.

**Consumable scarcity.** 1 consumable per draft may feel thin. Could add bonus drops from in-round milestones (chain depth 4+ or vanishing 3+ tiles in one turn).

**Late-game pacing.** +1,000 target per round with vanish threshold 6 is punishing. Consider milestone rounds every 3 rounds with bonuses.

**Greed mod on 12-cell board.** With 12 cells and board-full ending the round, Greed gives a flat +36 per round. This is reliable but not exciting. May need retuning.
