#!/usr/bin/env python3
"""
Hyperparameter Tuner — Coordinate Descent Self-Play Optimizer
=============================================================
This script optimizes a C++ poker bot by sweeping one variable at a time,
generating temporary bash wrappers to safely inject environment variables,
and running multi-threaded self-play matches against a baseline.
"""

import subprocess
import concurrent.futures
import os
import json
import sys
import time
import stat
import uuid

# ============================================================
# CONFIGURATION
# ============================================================

MATCHES_PER_CONFIG = 250     # Games per seat per candidate (total = 500)
WIN_RATE_THRESHOLD = 0.52    # A challenger must beat the baseline by this margin
NUM_PASSES = 3               # How many full sweeps over all HPs to run
ENGINE_CMD = ["./chaos_poker", "--history", "1000", "5", "15", "25", "50"]
BEST_CONFIG_FILE = "best_hp_config.json"

# ============================================================
# BASELINE (Default Hyperparameter Values)
# ============================================================
BASELINE_PARAMS = {
    # --- Aggression Stats ---
    "OPT_RAISE_BASE_WEIGHT":        1.0,
    "OPT_BLUFF_THRESH":             0.25,
    "OPT_MIN_FOLD_RATE":            0.40,
    "OPT_RAISE_POT_WEIGHT":         1.0,
    "OPT_RAISE_STACK_WEIGHT":       1.0,
    "OPT_ALLIN_BASE_WEIGHT":        1.0,
    "OPT_ALLIN_POT_WEIGHT":         1.0,

    # --- Engine & Math (Not in Search Space - Tune manually for speed) ---
    "OPT_CHEN_DIVISOR":             20.0,
    "OPT_PREFLOP_BASE_EQ":          0.30,
    "OPT_PREFLOP_MULT_EQ":          0.55,
    "OPT_MULTIWAY_BASE_EXP":        0.8,
    "OPT_MULTIWAY_STEP_EXP":        0.2,

    # --- SWAP_PROMPT ---
    "OPT_SWAP_PRE_STAY_CHEN":       8.0,
    "OPT_SWAP_PRE_WEAK_CHEN":       3.0,
    "OPT_SWAP_PRE_COST_FRACTION":   5,
    "OPT_SWAP_POST_MAX_EQ":         0.25,
    "OPT_SWAP_POST_COST_FRACTION":  8,

    # --- VOTE_PROMPT ---
    "OPT_VOTE_YES_MIN_EQ":          0.55,
    "OPT_VOTE_YES_NOISE_MIN":       0.15,
    "OPT_VOTE_YES_NOISE_MAX":       0.30,
    "OPT_VOTE_NO_MAX_EQ":           0.35,
    "OPT_VOTE_NO_NOISE_MIN":        0.10,
    "OPT_VOTE_NO_NOISE_MAX":        0.20,
    "OPT_VOTE_MIN_POT_BB_MULT":     3,

    # --- ACTION_PROMPT (Aggressor) ---
    "OPT_ACT_VAL_MONSTER_BASE_FRAC": 0.5,
    "OPT_ACT_VAL_MONSTER_EQ_MULT":   2.0,
    "OPT_ACT_VAL_MONSTER_EQ":        0.75,
    "OPT_ACT_VAL_THIN_EQ":           0.50,
    "OPT_ACT_VAL_THIN_SIZE":         0.35,
    "OPT_ACT_BLUFF_MAX_EQ":          0.35,
    "OPT_ACT_BLUFF_NIT_MASSIVE":     0.60,
    "OPT_ACT_BLUFF_NIT_3WAY":        0.65,
    "OPT_ACT_BLUFF_SIZE":            0.60,

    # --- ACTION_PROMPT (Defender) ---
    "OPT_DEF_RERAISE_MONSTER_EQ":    0.80,
    "OPT_DEF_ALLIN_MONSTER_EQ":      0.90,
    "OPT_DEF_RERAISE_SIZE":          0.80,
    "OPT_DEF_EV_MARGIN":             0.05,
    "OPT_DEF_VAL_RAISE_EQ":          0.65,
    "OPT_DEF_VAL_RAISE_SIZE":        0.50,
    "OPT_DEF_SURVIVAL_EQ":           0.50,
    "OPT_DEF_IMPLIED_MARGIN":        0.03,
    "OPT_DEF_BLUFF_RAISE_MULT":      3,
    
    # --- Opponent Profiling ---
    "OPT_PROF_DEFAULT_FOLD":         0.30,
    "OPT_PROF_DEFAULT_AGG":          0.30,
}

# ============================================================
# SEARCH SPACE (Candidate values to try for each HP)
# ============================================================
SEARCH_SPACE = {
    # 1. Aggression Tracking Weights
    "OPT_RAISE_BASE_WEIGHT":        [0.5, 1.0, 1.5, 2.0],
    "OPT_BLUFF_THRESH":             [0.15, 0.20, 0.25, 0.30],
    "OPT_MIN_FOLD_RATE":            [0.30, 0.40, 0.50, 0.60],
    
    # 2. Aggressor Betting Sizes & Thresholds
    "OPT_ACT_VAL_MONSTER_BASE_FRAC": [0.4, 0.5, 0.6, 0.75],
    "OPT_ACT_VAL_MONSTER_EQ_MULT":   [1.5, 2.0, 2.5, 3.0],
    "OPT_ACT_VAL_THIN_SIZE":         [0.25, 0.35, 0.45],
    "OPT_ACT_BLUFF_SIZE":            [0.40, 0.50, 0.60, 0.75],
    "OPT_ACT_BLUFF_NIT_MASSIVE":     [0.50, 0.60, 0.70],

    # 3. Defender Betting Sizes & Thresholds
    "OPT_DEF_RERAISE_MONSTER_EQ":    [0.75, 0.80, 0.85],
    "OPT_DEF_RERAISE_SIZE":          [0.60, 0.80, 1.0, 1.5],
    "OPT_DEF_EV_MARGIN":             [0.02, 0.05, 0.08, 0.10],
    "OPT_DEF_VAL_RAISE_SIZE":        [0.40, 0.50, 0.60],
    "OPT_DEF_IMPLIED_MARGIN":        [0.01, 0.03, 0.05],
    "OPT_DEF_BLUFF_RAISE_MULT":      [2, 3, 4],

    # 4. Voting / Texture Manipulation
    "OPT_VOTE_YES_MIN_EQ":          [0.50, 0.55, 0.60, 0.65],
    "OPT_VOTE_NO_MAX_EQ":           [0.25, 0.30, 0.35, 0.40],
    
    # 5. Pre/Post Flop Card Swapping
    "OPT_SWAP_PRE_STAY_CHEN":       [6.0, 7.0, 8.0, 9.0],
    "OPT_SWAP_PRE_COST_FRACTION":   [3, 5, 8],
    "OPT_SWAP_POST_MAX_EQ":         [0.20, 0.25, 0.30],
    "OPT_SWAP_POST_COST_FRACTION":  [5, 8, 10],
}


# ============================================================
# ENGINE HELPERS (Bash Wrappers)
# ============================================================

def create_wrapper(script_path: str, bot_bin: str, params: dict):
    """Creates a temporary bash script to safely inject environment variables."""
    with open(script_path, "w") as f:
        f.write("#!/bin/bash\n")
        for k, v in params.items():
            f.write(f"export {k}={v}\n")
        f.write(f"exec {bot_bin} \"$@\"\n")
    
    # Make it executable
    st = os.stat(script_path)
    os.chmod(script_path, st.st_mode | stat.S_IEXEC)


def run_match(challenger_params: dict, baseline_params: dict, challenger_seat: int) -> bool:
    """Runs a single game. Uses unique UUID wrappers to prevent thread collisions."""
    bot_bin = "./bots/smart_bot"
    run_id = uuid.uuid4().hex
    
    challenger_script = f"/tmp/chal_{run_id}.sh"
    baseline_script = f"/tmp/base_{run_id}.sh"
    
    try:
        create_wrapper(challenger_script, bot_bin, challenger_params)
        create_wrapper(baseline_script, bot_bin, baseline_params)

        if challenger_seat == 0:
            full_cmd = ENGINE_CMD + [challenger_script, baseline_script]
        else:
            full_cmd = ENGINE_CMD + [baseline_script, challenger_script]

        result = subprocess.run(
            full_cmd, capture_output=True, text=True, timeout=15, shell=False
        )
        
        # Parse the output for the winner
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
    finally:
        # Cleanup temporary files so we don't spam the OS
        if os.path.exists(challenger_script): os.remove(challenger_script)
        if os.path.exists(baseline_script): os.remove(baseline_script)


def evaluate_candidate(hp_key: str, candidate_val, baseline_params: dict) -> tuple[float, int, int]:
    """Tests one candidate value against the baseline. Returns (win_rate, wins, total_games)."""
    challenger_params = dict(baseline_params)
    challenger_params[hp_key] = candidate_val

    total_games = MATCHES_PER_CONFIG * 2
    wins = 0

    # Max workers balances between CPU cores and IO limits
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(os.cpu_count() or 4, 16)) as executor:
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
            
            best_wr_for_hp = 0.5  
            best_candidate = current_val
            best_candidate_changed = False

            for cand in candidates:
                if cand == current_val:
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
                
                with open(BEST_CONFIG_FILE, "w") as f:
                    json.dump(best_params, f, indent=2)
            else:
                print(f"  => No improvement for {hp_key}. Keeping {current_val}.")

        print(f"\n[PASS {pass_num} DONE] Improvements this pass: {pass_improvements}")
        if pass_improvements == 0:
            print("[INFO] Converged — no improvement in this pass. Stopping early.")
            break

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
            changed = "  <-- CHANGED" if str(v) != str(default) else ""
            print(f"  {k:<40} = {v}{changed}")
    else:
        print("No configuration outperformed the baseline. Default hyperparameters are currently optimal.")

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