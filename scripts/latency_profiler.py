#!/usr/bin/env python3
import subprocess
import time
import sys
import os

BOT_PATH = "../bots/smart_bot"

def measure_scenario(scenario):
    proc = subprocess.Popen([BOT_PATH], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    peaks = []
    
    for _ in range(5):
        # Send full setup before each prompt to ensure cleanly initialized state
        for cmd in scenario["setup"]:
            proc.stdin.write(cmd + "\n")
        proc.stdin.flush()
        
        start_time = time.perf_counter()
        proc.stdin.write(scenario["prompt"] + "\n")
        proc.stdin.flush()
        
        output = proc.stdout.readline()
        end_time = time.perf_counter()
        
        if not output:
             print("Bot exited unexpectedly!")
             break
             
        output = output.strip()
        latency_ms = (end_time - start_time) * 1000.0
        peaks.append(latency_ms)
        
    proc.terminate()
    return max(peaks) if peaks else 0.0

def main():
    if not os.path.exists(BOT_PATH):
        print(f"Error: Bot executable {BOT_PATH} not found.")
        sys.exit(1)
        
    print(f"Starting {BOT_PATH} latency profile...")
    
    scenarios = [
        {
            "name": "Pre-Flop ACTION (Fast)",
            "setup": [
                "GAME_START 4 0 2500 5 15 25 50",
                "HAND_START 1 0 1 2 25 50",
                "CHIPS 2500 2500 2500 2500",
                "HOLE Ks Kd",
            ],
            "prompt": "ACTION_PROMPT 50 2500"
        },
        {
            "name": "Pre-Flop SWAP",
            "setup": [
                "GAME_START 4 0 2500 5 15 25 50",
                "HAND_START 1 0 1 2 25 50",
                "CHIPS 2500 2500 2500 2500",
                "HOLE Jc 2d"
            ],
            "prompt": "SWAP_PROMPT 5 2500"
        },
        {
            "name": "Flop VOTE (4.5ms deadline)",
            "setup": [
                "GAME_START 4 0 2500 5 15 25 50",
                "HAND_START 1 0 1 2 25 50",
                "CHIPS 2500 2500 2500 2500",
                "HOLE Ks Kd",
                "MATCH_STATE 1",
                "BOARD 2h 5d 9c"
            ],
            "prompt": "VOTE_PROMPT 2500"
        },
        {
            "name": "Flop SWAP (MC Evals)",
            "setup": [
                "GAME_START 4 0 2500 5 15 25 50",
                "HAND_START 1 0 1 2 25 50",
                "CHIPS 2500 2500 2500 2500",
                "HOLE 2h 7d",
                "MATCH_STATE 1",
                "BOARD Ad As Ac"
            ],
            "prompt": "SWAP_PROMPT 10 2500"
        },
        {
            "name": "Flop ACTION (6.0ms deadline)",
            "setup": [
                "GAME_START 4 0 2500 5 15 25 50",
                "HAND_START 1 0 1 2 25 50",
                "CHIPS 2500 2500 2500 2500",
                "HOLE 2c 3c",
                "MATCH_STATE 1",
                "BOARD 2h 5d 9c"
            ],
            "prompt": "ACTION_PROMPT 0 2500"
        },
        {
            "name": "Turn ACTION (6.0ms deadline)",
            "setup": [
                 "GAME_START 4 0 2500 5 15 25 50",
                 "HAND_START 1 0 1 2 25 50",
                 "CHIPS 2500 2500 2500 2500",
                 "HOLE As Ah",
                 "MATCH_STATE 2",
                 "BOARD 2h 5d 9c Jc"
            ],
            "prompt": "ACTION_PROMPT 100 2500"
        },
        {
            "name": "River ACTION (7.5ms deadline) - Worst Case",
            "setup": [
                 "GAME_START 4 0 2500 5 15 25 50",
                 "HAND_START 1 0 1 2 25 50",
                 "CHIPS 2500 2500 2500 2500",
                 "HOLE As Ah",
                 "MATCH_STATE 3",
                 "BOARD 2h 5d 9c Jc Tc"
            ],
            "prompt": "ACTION_PROMPT 500 2500"
        }
    ]
    
    all_pass = True
    max_latency = 0.0
    
    print("-" * 60)
    print(f"{'Scenario Name':<45} | {'Latency':>10}")
    print("-" * 60)
    
    for scenario in scenarios:
        peak_ms = measure_scenario(scenario)
        max_latency = max(max_latency, peak_ms)
        
        status = f"[{peak_ms:>5.2f} ms]"
        if peak_ms > 10.0:
            status += " FAIL"
            all_pass = False
        else:
            status += " PASS"
            
        print(f"{scenario['name']:<45} | {status:>12}")
        
    print("-" * 60)
    if all_pass:
        print(f"SUCCESS: All decisions stayed under 10.00 ms (Peak: {max_latency:.2f} ms).")
    else:
        print(f"FAILURE: One or more decisions EXCEEDED 10.00 ms limit! (Peak: {max_latency:.2f} ms)")

if __name__ == "__main__":
    main()
