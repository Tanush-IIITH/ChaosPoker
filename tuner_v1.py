#!/usr/bin/env python3
"""
Hyperparameter Tuner — Coordinate Descent Self-Play Optimizer
=============================================================
Strategy:
  1. Start with a baseline (the current default HP values).
  2. For each hyperparameter, sweep a set of candidate values.
  3. Run N self-play games: challenger (with one HP changed) vs. baseline.
  4. If the challenger wins more than the WIN_RATE_THRESHOLD, it becomes
     the new baseline for subsequent iterations.
  5. Repeat for multiple full passes until the config converges.

This approach (Coordinate Descent) is ~60x faster than a full grid search
and reliably finds strong local optima.
"""

import subprocess
import concurrent.futures
import os
import json
import sys
import time

# ============================================================
# CONFIGURATION
# ============================================================

MATCHES_PER_CONFIG = 50      # Games per seat per candidate (total = 2x this)
WIN_RATE_THRESHOLD = 0.52    # A challenger must beat the baseline by this margin
NUM_PASSES = 3               # How many full sweeps over all HPs to run
ENGINE_CMD = ["./chaos_poker", "--history", "1000", "5", "15", "25", "50"]
BEST_CONFIG_FILE = "best_hp_config.json"

# ============================================================
# BASELINE (Default Hyperparameter Values)
# ============================================================
# These mirror the defaults in smart_bot.cpp.
# Only the HPs that have environment variable overrides are listed here.

BASELINE_PARAMS = {
    # --- Aggression Stats ---
    "OPT_RAISE_BASE_WEIGHT":        0.20,
    "OPT_BLUFF_THRESH":             0.25,
    "OPT_MIN_FOLD_RATE":            0.40,
    "OPT_RAISE_POT_WEIGHT":         1.0,
    "OPT_RAISE_STACK_WEIGHT":       1.0,
    "OPT_ALLIN_BASE_WEIGHT":        1.0,
    "OPT_ALLIN_POT_WEIGHT":         1.0,

    # --- ACTION_PROMPT (Aggressor) ---
    "OPT_ACT_VAL_MONSTER_BASE_FRAC":  0.5,
    "OPT_ACT_VAL_MONSTER_EQ_MULT":    2.0,
}

# ============================================================
# SEARCH SPACE (Candidate values to try for each HP)
# ============================================================
# Each entry lists candidate values to sweep around the default.
# Keep these focused — too wide a range extends runtime significantly.

SEARCH_SPACE = {
    # Aggression profiling
    "OPT_RAISE_BASE_WEIGHT":        [0.0, 0.5, 1.0, 1.5, 2.0],
    "OPT_BLUFF_THRESH":             [0.15, 0.20, 0.25, 0.30, 0.35],
    "OPT_MIN_FOLD_RATE":            [0.30, 0.35, 0.40, 0.45, 0.55],
    "OPT_RAISE_POT_WEIGHT":         [0.5, 0.75, 1.0, 1.25, 1.5],
    "OPT_RAISE_STACK_WEIGHT":       [0.5, 0.75, 1.0, 1.25, 1.5],
    "OPT_ALLIN_BASE_WEIGHT":        [0.5, 1.0, 1.5, 2.0, 3.0],
    "OPT_ALLIN_POT_WEIGHT":         [0.5, 0.75, 1.0, 1.25, 1.5],

    # Bet sizing
    "OPT_ACT_VAL_MONSTER_BASE_FRAC":  [0.3, 0.4, 0.5, 0.6, 0.7],
    "OPT_ACT_VAL_MONSTER_EQ_MULT":    [1.0, 1.5, 2.0, 2.5, 3.0],
}


# ============================================================
# ENGINE HELPERS
# ============================================================

def build_env(params: dict) -> dict:
    """Returns a copy of the OS env with the given HP params injected as strings."""
    env = os.environ.copy()
    for k, v in params.items():
        env[k] = str(v)
    return env


def run_match(challenger_params: dict, baseline_params: dict, challenger_seat: int) -> bool:
    """
    Runs one game. The challenger bot and baseline bot are the same binary
    (./bots/smart_bot), but launched with different environment variables.
    Returns True if the challenger won.
    """
    bot_bin = "./bots/smart_bot"
    bots = [bot_bin, bot_bin]

    challenger_env = build_env(challenger_params)
    baseline_env = build_env(baseline_params)

    bots_list = [bot_bin, bot_bin]  # placeholder for arg list

    # We run two separate processes that communicate with the engine via stdio.
    # The engine forks per-player, so each gets its own env. We achieve this by
    # wrapping each bot call in a shell env prefix via the `env` utility.
    def make_bot_cmd(params):
        env_prefix = " ".join(f"{k}={v}" for k, v in params.items())
        return f"env {env_prefix} {bot_bin}"

    challenger_cmd = make_bot_cmd(challenger_params)
    baseline_cmd   = make_bot_cmd(baseline_params)

    if challenger_seat == 0:
        full_cmd = ENGINE_CMD + [challenger_cmd, baseline_cmd]
    else:
        full_cmd = ENGINE_CMD + [baseline_cmd, challenger_cmd]

    try:
        result = subprocess.run(
            full_cmd, capture_output=True, text=True, timeout=15, shell=False
        )
        for line in reversed(result.stderr.splitlines()):
            if "wins" in line and ">> " in line:
                parts = line.split()
                if len(parts) > 1 and parts[1].startswith("P"):
                    try:
                        winner = int(parts[1][1:])
                        return winner == challenger_seat
                    except ValueError:
                        pass
        return False
    except subprocess.TimeoutExpired:
        return False
    except Exception:
        return False


def evaluate_candidate(hp_key: str, candidate_val, baseline_params: dict) -> tuple[float, int, int]:
    """
    Tests one candidate value for one HP against the current baseline.
    Returns (win_rate, wins, total_games).
    """
    challenger_params = dict(baseline_params)
    challenger_params[hp_key] = candidate_val

    total_games = MATCHES_PER_CONFIG * 2
    wins = 0

    with concurrent.futures.ThreadPoolExecutor(max_workers=min(os.cpu_count() or 4, 12)) as executor:
        futures = []
        for _ in range(MATCHES_PER_CONFIG):
            futures.append(executor.submit(run_match, challenger_params, baseline_params, 0))
            futures.append(executor.submit(run_match, challenger_params, baseline_params, 1))

        for f in concurrent.futures.as_completed(futures):
            if f.result():
                wins += 1

    win_rate = wins / total_games
    return win_rate, wins, total_games


# ============================================================
# MAIN OPTIMIZER
# ============================================================

def main():
    # Load best config from disk if a previous run exists
    if os.path.exists(BEST_CONFIG_FILE):
        with open(BEST_CONFIG_FILE) as f:
            best_params = json.load(f)
        print(f"[INFO] Resuming from saved config: {BEST_CONFIG_FILE}")
    else:
        best_params = dict(BASELINE_PARAMS)

    print("=" * 62)
    print(" COORDINATE DESCENT HYPERPARAMETER OPTIMIZER")
    print(f" Passes:              {NUM_PASSES}")
    print(f" HPs to tune:         {len(SEARCH_SPACE)}")
    print(f" Candidates per HP:   {max(len(v) for v in SEARCH_SPACE.values())}")
    print(f" Games per candidate: {MATCHES_PER_CONFIG * 2}")
    print(f" Win threshold:       {WIN_RATE_THRESHOLD * 100:.0f}%")
    print("=" * 62)
    print()

    overall_improvements = 0

    for pass_num in range(1, NUM_PASSES + 1):
        print(f"{'='*62}")
        print(f" PASS {pass_num} of {NUM_PASSES}")
        print(f"{'='*62}")

        pass_improvements = 0

        for hp_idx, (hp_key, candidates) in enumerate(SEARCH_SPACE.items()):
            current_val = best_params.get(hp_key, BASELINE_PARAMS.get(hp_key))
            print(f"\n[{hp_idx+1}/{len(SEARCH_SPACE)}] Tuning {hp_key} (current={current_val})")
            print(f"  Candidates: {candidates}")

            best_wr_for_hp = 0.5  # Must beat 50% (the baseline wins 50% vs itself)
            best_candidate = current_val
            best_candidate_changed = False

            for cand in candidates:
                if cand == current_val:
                    # Skip if identical to current (saves time)
                    print(f"    {cand:>8} -> (current, skip)")
                    continue

                t0 = time.time()
                wr, wins, total = evaluate_candidate(hp_key, cand, best_params)
                elapsed = time.time() - t0

                marker = "✓" if wr >= WIN_RATE_THRESHOLD else " "
                print(f"  {marker} {cand:>8} -> {wr*100:5.1f}%  ({wins}/{total})  [{elapsed:.1f}s]")

                if wr > best_wr_for_hp:
                    best_wr_for_hp = wr
                    best_candidate = cand
                    best_candidate_changed = True

            if best_candidate_changed and best_wr_for_hp >= WIN_RATE_THRESHOLD:
                print(f"  => PROMOTED {hp_key}: {current_val} -> {best_candidate}  "
                      f"(win rate {best_wr_for_hp*100:.1f}%)")
                best_params[hp_key] = best_candidate
                pass_improvements += 1
                overall_improvements += 1
                # Save checkpoint after every improvement
                with open(BEST_CONFIG_FILE, "w") as f:
                    json.dump(best_params, f, indent=2)
            else:
                print(f"  => No improvement for {hp_key}. Keeping {current_val}.")

        print(f"\n[PASS {pass_num} DONE] Improvements this pass: {pass_improvements}")
        if pass_improvements == 0:
            print("[INFO] Converged — no improvement in this pass. Stopping early.")
            break

    # ---- Final Report ----
    print("\n" + "=" * 62)
    print(" OPTIMIZATION COMPLETE")
    print("=" * 62)
    print(f" Total improvements found: {overall_improvements}")
    print()

    if overall_improvements > 0:
        print("Optimal Hyperparameters (copy into HP struct defaults):")
        print()
        for k, v in best_params.items():
            default = BASELINE_PARAMS.get(k, "N/A")
            changed = "  <-- CHANGED" if v != default else ""
            print(f"  {k:<40} = {v}{changed}")
        print()
        print(f"Config saved to: {BEST_CONFIG_FILE}")
    else:
        print("No configuration outperformed the baseline.")
        print("Your default hyperparameters are currently optimal.")

    # Always save the final result
    with open(BEST_CONFIG_FILE, "w") as f:
        json.dump(best_params, f, indent=2)


if __name__ == "__main__":
    if not os.path.exists("./bots/smart_bot"):
        print("Error: ./bots/smart_bot not found. Run 'make' first.")
        sys.exit(1)
    if not os.path.exists("./chaos_poker"):
        print("Error: ./chaos_poker engine not found. Run 'make' first.")
        sys.exit(1)
    main()