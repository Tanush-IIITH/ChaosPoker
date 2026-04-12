#!/usr/bin/env python3
import subprocess
import concurrent.futures
import sys
import os
import argparse
from typing import List, Tuple

def run_match(engine_path: str, bots: List[str], target_bot_index: int) -> bool:
    """
    Runs a single match and returns True if the target_bot_index won, False otherwise.
    """
    # Build the engine command with full hand history enabled
    cmd = [
        engine_path,
        "--history",
        "1000", "5", "15", "25", "50",
    ] + bots

    try:
        # Execute process and wait for completion
        result = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, timeout=30)
        output = result.stderr

        # Parse engine logs to find the winner
        for line in reversed(output.splitlines()):
            if "wins" in line and ">> " in line:
                parts = line.split()

                if parts[1].startswith("P"):
                    winner_index = int(parts[1][1:])
                    return winner_index == target_bot_index
        return False
    except subprocess.TimeoutExpired:
        print("x", end="", flush=True)
        return False
    except Exception as e:
        print(f"E", end="", flush=True)
        return False

def benchmark_scenario(name: str, engine_path: str, bots: List[str], target_bot_index: int, num_games: int) -> Tuple[str, int, int]:
    """
    Runs a scenario multiple times concurrently and returns the results.
    """
    # Dispatch multiple games via a thread pool
    print(f"Running '{name}' ({num_games} games)... ", end="", flush=True)
    wins = 0

    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as executor:
        futures = [executor.submit(run_match, engine_path, bots, target_bot_index) for _ in range(num_games)]

        for future in concurrent.futures.as_completed(futures):
            is_win = future.result()
            if is_win:
                wins += 1

            print(".", end="", flush=True)

    win_rate = (wins / num_games) * 100
    print(f" Done! {wins}/{num_games} ({win_rate:.1f}%)")
    return name, wins, num_games

def main():
    # Parse arguments and defaults
    parser = argparse.ArgumentParser(description="Benchmark a Chaos Poker bot.")
    parser.add_argument("--bot", default="../bots/smart_bot", help="Path to the bot you want to test")
    parser.add_argument("--games", type=int, default=50, help="Number of games per scenario")
    parser.add_argument("--loops", type=int, default=1, help="Number of times to run the full benchmark suite")
    parser.add_argument("--engine", default="../chaos_poker", help="Path to the chaos_poker engine")
    args = parser.parse_args()

    bot_under_test = args.bot
    random_bot = "../bots/random_bot"
    example_bot = "../bots/example_bot"
    engine = args.engine

    # Validate prerequisites
    if not os.path.exists(engine) or not os.path.exists(bot_under_test):
        print(f"Error: Make sure the engine ({engine}) and bot ({bot_under_test}) exist.")
        print("Did you run 'make'?")
        sys.exit(1)

    print(f"============================================================")
    print(f" Benchmarking Bot: {bot_under_test}")
    print(f" Games per scenario: {args.games}")
    print(f"============================================================\n")

    base_scenarios = [
        ("Heads-Up vs Random bot", [random_bot]),
        ("Heads-Up vs Example bot", [example_bot]),
        ("vs 2 Random bots", [random_bot, random_bot]),
        ("vs 2 Example bots", [example_bot, example_bot]),
        ("vs 1 Random, 1 Example", [random_bot, example_bot]),
        ("4-Player Mixed", [random_bot, example_bot, random_bot]),
        ("5-Player Mixed", [random_bot, example_bot, random_bot, example_bot]),
        ("6-Player Mixed", [random_bot, example_bot, random_bot, example_bot, random_bot]),
        ("7-Player Random", [random_bot, random_bot, random_bot, random_bot, random_bot, random_bot]),
        ("7-Player Example", [example_bot, example_bot, example_bot, example_bot, example_bot, example_bot]),
        ("7-Player Mixed", [random_bot, example_bot, random_bot, example_bot, random_bot, example_bot]),
    ]

    scenarios = []
    for base_name, opponents in base_scenarios:
        num_seats = len(opponents) + 1
        for seat in range(num_seats):

            if num_seats > 2:
                if seat == 0:
                    pos_str = "early"
                elif seat == num_seats - 1:
                    pos_str = "late"
                else:
                    pos_str = "mid"
                name = f"{base_name} (Seat {seat}, {pos_str})"
            else:
                name = f"{base_name} (Seat {seat})"

            bots = list(opponents)
            bots.insert(seat, bot_under_test)
            scenarios.append((name, bots, seat))

    grand_totals = {name: {'wins': 0, 'games': 0} for name, _, _ in scenarios}

    for loop_idx in range(args.loops):
        if args.loops > 1:
            print(f"\n+++ BENCHMARK ITERATION {loop_idx + 1} OF {args.loops} +++\n")

        results = []

        for name, bots, target_idx in scenarios:
            res = benchmark_scenario(name, engine, bots, target_idx, args.games)
            results.append(res)

        print("\n============================================================")
        print(" SUMMARY REPORT")
        print("============================================================")
        print(f"{'Scenario':<40} | {'Win Rate':<10} | {'Wins/Games'}")
        print("-" * 65)

        total_wins = 0
        total_games = 0

        for name, wins, games in results:
            win_rate = (wins / games) * 100
            print(f"{name:<40} | {win_rate:>6.1f}%    | {wins}/{games}")
            total_wins += wins
            total_games += games

            grand_totals[name]['wins'] += wins
            grand_totals[name]['games'] += games

        overall_rate = (total_wins / total_games) * 100
        print("-" * 65)
        print(f"{'OVERALL':<40} | {overall_rate:>6.1f}%    | {total_wins}/{total_games}")
        print("============================================================\n")

    # Output grand totals across all execution loops
    if args.loops > 1:
        print("\n" + "=" * 60)
        print(" GRAND TOTAL SUMMARY (ALL ITERATIONS)")
        print("=" * 60)
        print(f"{'Scenario':<40} | {'Win Rate':<10} | {'Wins/Games'}")
        print("-" * 65)

        grand_overall_wins = 0
        grand_overall_games = 0
        for name, _, _ in scenarios:
            w = grand_totals[name]['wins']
            g = grand_totals[name]['games']
            win_rate = (w / g) * 100 if g > 0 else 0
            print(f"{name:<40} | {win_rate:>6.1f}%    | {w}/{g}")
            grand_overall_wins += w
            grand_overall_games += g

        grand_overall_rate = (grand_overall_wins / grand_overall_games) * 100 if grand_overall_games > 0 else 0
        print("-" * 65)
        print(f"{'GRAND OVERALL':<40} | {grand_overall_rate:>6.1f}%    | {grand_overall_wins}/{grand_overall_games}")
        print("=" * 60 + "\n")

if __name__ == "__main__":
    main()
