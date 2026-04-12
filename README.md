# Chaos Poker — `smart_bot`

**Author:** Tanush Garg

---

## Overview

`smart_bot` is a probabilistic poker bot for the Chaos Poker engine. It communicates via `stdin`/`stdout` using the engine's line protocol. All decisions — card swapping, board voting, and betting — are driven by:

- An **O(1) pre-flop equity lookup table** (`PreFlop` namespace)
- A **time-bounded Monte Carlo equity estimator** backed by the Cactus Kev hand evaluator
- **Per-opponent profiling** (fold rate + weighted aggression score)
- A fully **ML-tunable hyperparameter struct** (`HP`)

---

## How to Build

Requires `g++` with C++17 support.

```bash
# Build the engine and all bots
make

# Clean and rebuild from scratch
make clean && make
```

The bot binary is output to:
```
bots/smart_bot
```

---

## How to Run

The engine spawns the bot automatically. Pass the binary path as an argument:

```bash
# 2-player: smart_bot vs example_bot
./chaos_poker 5 15 25 50 ./bots/smart_bot ./bots/example_bot

# With full game history (1000 hands logged to stderr)
./chaos_poker --history 1000 5 15 25 50 ./bots/smart_bot ./bots/example_bot
```

### Exact Launch Command

```bash
./bots/smart_bot
```

> The bot reads from `stdin` and writes to `stdout`. It must never print unprompted output.

---

## How to Benchmark

All scripts live in `scripts/`.

```bash
cd scripts/

# Self-play: smart_bot vs smart_bot_v2 baseline (2 seats, N loops)
python3 benchmark_bot.py --games 50 --loops 1

# Full matrix: smart_bot vs random/example bots across all player counts and seat positions
python3 benchmark_old.py --games 50

# Per-decision latency check — verifies every decision stays under 10ms
python3 latency_profiler.py
```

---

## How to Tune Hyperparameters

```bash
cd scripts/

# Coordinate-descent optimizer — iterates over the SEARCH_SPACE in tuner.py
python3 tuner.py
```

Results are saved to `best_hp_config.json`. After a successful run, promote winning values into the `Hyperparameters` struct defaults at the top of `bots/smart_bot.cpp` and recompile.

---

## Strategy

### 1. Hand Evaluation — Cactus Kev (O(1) Lookup)

Cards are encoded as 32-bit integers with rank, suit, and a prime-number field. A 7-card hand is evaluated by enumerating all 21 five-card subsets and looking each up via three chained tables (`flushes[]`, `unique5[]`, `prime_products[]` from `tools/ck_tables.h`). This replaces brute-force O(n⁵) evaluation with a constant-time lookup.

### 2. Pre-Flop Equity — O(1) Lookup Table

The `PreFlop` namespace holds a 169-entry `tbl[13][13][2]` array initialised once at startup. Pocket pair equities are calibrated from real head-up data (22 = 50.6%, AA = 85.3%). Unpaired hands use an empirical formula: `base + connectivity_bonus [+ suited_bonus]`. Multi-way equity is discounted using a geometric-linear blend:

```
multi_eq = max(0.08, (geo + lin) / 2)
geo = eq ^ num_opponents
lin = eq − 0.05 × (num_opponents − 1)
```

This lookup is used for all pre-flop decisions (SWAP, VOTE, ACTION) — no Monte Carlo is run before the flop.

### 3. Time-Bounded Monte Carlo

`estimate_equity()` and `estimate_swap_equity()` run a **clock-gated loop**:

```cpp
while (sim < num_simulations) {
    if ((sim & 63) == 0 && elapsed_ms(t_start) >= deadline_ms) break;
    // ... simulate one rollout
}
```

The clock is checked only every 64 iterations (bitwise AND — zero overhead otherwise). Each call site passes a hard deadline derived from the per-message `t0` timestamp captured at the top of the main loop:

| Prompt | Deadline passed |
|---|---|
| `VOTE_PROMPT` | `4.5 − elapsed_ms(t0)` ms |
| `SWAP_PROMPT` (post-flop, each call) | `4.0 − elapsed_ms(t0)` ms |
| `ACTION_PROMPT` (flop/turn) | `6.0 − elapsed_ms(t0)` ms |
| `ACTION_PROMPT` (river) | `7.5 − elapsed_ms(t0)` ms |

A secondary rollout cap (`MC_ROLLOUTS_*`) acts as a ceiling; whichever limit is hit first terminates the loop.

### 4. Swap Decisions

**Pre-flop** (`current_street == 0`):
- Look up `PreFlop::multi_eq(pfe.eq, num_opp)`.
- If `equity ≥ SWAP_PRE_STAY_EQ (0.65)` → **STAY**.
- If `equity < SWAP_PRE_WEAK_EQ (0.40)` and `cost ≤ chips / SWAP_PRE_COST_FRACTION (5)` → **SWAP** the lower-ranked hole card.
- Otherwise → **STAY**.

**Post-flop** (`current_street > 0`):
- Run `estimate_equity()` for the current hand.
- If `equity < SWAP_POST_MAX_EQ (0.25)` and `cost ≤ chips / SWAP_POST_COST_FRACTION (12)`:
  - Run `estimate_swap_equity()` twice — once holding card 0, once holding card 1.
  - **SWAP** the card whose kept-partner yields the lower equity.
- Otherwise → **STAY**.

### 5. VOTE — EV-Delta Insurance Model

Computes:
- **Current EV** = `MC equity × pot_estimate`
- **Redraw EV** = `PreFlop table equity (multi-way adjusted) × pot_estimate`

Then:
- **`VOTE YES + wager`** if `equity ≥ 0.48` and `EV gap > 0` — wager = `EV_gap × U(0.20, 0.30)`.
- **`VOTE NO + wager`** if `equity < 0.28` and `EV gap < 0` — wager = `|EV_gap| × U(0.20, 0.25)`.
- Otherwise **`VOTE YES 0`** (abstain).

Wager is clamped to `[0, my_chips]`. If the pot is below `big_blind × VOTE_MIN_POT_BB_MULT`, wager is forced to 0. Randomised sizing prevents exploitable fixed-bet patterns.

### 6. ACTION — Aggressor / Defender Trees

Equity is computed once per `ACTION_PROMPT` (PreFlop lookup or time-bounded MC). `pot_odds = to_call / (pot + to_call)`.

**Aggressor** (no bet to call, `to_call ≤ 0`):

| Equity | Action |
|---|---|
| ≥ 0.70 (Monster) | Raise — size = `(0.80 + (equity − 0.70) × 1.0) × pot`, clamped to stack |
| ≥ 0.40 (Thin value) | Raise 55% pot with prob 1/(FREQ_MAX+1), else Check |
| ≥ 0.30 (Showdown value) | Check |
| < 0.30 (Bluff zone) | Bluff raise if fold-rate, aggression, and stack conditions allow; else Check |

**Defender** (facing a bet, `to_call > 0`):

| Scenario | Action |
|---|---|
| Equity ≥ 0.93 | Reraise 80% pot, or go All-In if equity ≥ 0.90 |
| Equity > pot_odds + 0.12 | Call; or Raise 75% pot if equity ≥ 0.75 |
| Equity > pot_odds − 0.02 | Call if bet ≤ chips/6, else Fold (implied odds) |
| Bluff conditions met | Reraise 3× current bet, else Fold |
| Default | Fold |

### 7. Opponent Profiling

Tracked per seat across all hands:

- **Fold rate** = `total_folds / total_hands_seen`. Activates after ≥ `PROF_MIN_HANDS (5)` hands. Gate for all bluffing: must exceed `MIN_FOLD_RATE_BLUFF (0.40)`.
- **Aggression score** — each observed `RAISE` adds `RAISE_BASE_WEIGHT + pot_ratio × RAISE_POT_WEIGHT + stack_fraction × RAISE_STACK_WEIGHT`. Each `ALLIN` adds `ALLIN_BASE_WEIGHT + risk_ratio × ALLIN_POT_WEIGHT`. Activates after ≥ `PROF_MIN_ACTIONS (5)` actions. Default = 0.30.

Bluffs are only fired when the average opponent aggression score is below `BLUFF_AGGR_THRESH (0.25)`, the current street is ≥ `ACT_BLUFF_MIN_STREET (2)`, and we have a stack of at least `current_bet × ACT_BLUFF_SAFE_STACK_MULT (3)`.

---

## Tradeoffs

| Decision | Tradeoff |
|---|---|
| **Cactus Kev lookup** | O(1) per evaluation; requires precomputed `tools/ck_tables.h`. Exact for all hand combinations. |
| **PreFlop table vs. full MC** | Full MC pre-flop would take ~50ms per decision. The table is a sub-millisecond lookup calibrated to real HU equities. Doesn't account for opponent-specific hand ranges. |
| **Clock-gated MC** | Checking every 64 iterations reduces `chrono` call overhead significantly vs. every iteration. The 64-iteration window means at most ~5–10µs overshoot at peak throughput. |
| **Post-flop swap threshold** | A single low-equity threshold (`< 0.25`) avoids running two full MC evaluations for every swap prompt. Can miss marginal EV+ swaps. |
| **EV-delta vote sizing** | Randomised wager prevents fixed-pattern exploitation. However, very small pots will always produce wager=0 due to the `VOTE_MIN_POT_BB_MULT` guard. |
| **Coordinate-descent tuning** | Finds strong local optima in minutes vs. hours for full grid search. May miss the global optimum if the HP landscape has sharp ridges. |

---

## Files

| Path | Purpose |
|---|---|
| `bots/smart_bot.cpp` | Bot source — HP struct, card encoding, MC engine, PreFlop table, SWAP/VOTE/ACTION logic |
| `tools/ck_tables.h` | Cactus Kev precomputed lookup tables (`flushes`, `unique5`, `prime_products`) |
| `Makefile` | Build system |
| `scripts/benchmark_bot.py` | Self-play benchmark (smart_bot vs smart_bot_v2 baseline) |
| `scripts/benchmark_old.py` | Full benchmark vs random/example bots across all seat positions |
| `scripts/tuner.py` | Coordinate-descent hyperparameter optimizer |
| `scripts/latency_profiler.py` | Per-decision latency tester (verifies < 10ms) |
| `best_hp_config.json` | Last saved optimal hyperparameter configuration |
