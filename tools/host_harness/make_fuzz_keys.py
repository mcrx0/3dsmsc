#!/usr/bin/env python3
"""Writes build/harness/fuzzN.keys: seeded random sequences of key presses and touches.

Usage: make_fuzz_keys.py [count]   (default 10 sequences of ~700 frames)
The same seed always gives the same sequence, so a failing run can be replayed exactly.
"""
import os
import random
import sys

KEYS = ["A", "B", "X", "Y", "UP", "DOWN", "LEFT", "RIGHT"]
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
out_dir = os.path.join(root, "build", "harness")
os.makedirs(out_dir, exist_ok=True)
for seed in range(1, int(sys.argv[1] if len(sys.argv) > 1 else 10) + 1):
    rng = random.Random(seed)
    tokens = []
    for frame in range(3, 700):
        if rng.random() < 0.35:
            if rng.random() < 0.45:
                tokens.append(f"{frame}:TOUCH@{rng.randint(0, 319)},{rng.randint(0, 239)}")
            else:
                tokens.append(f"{frame}:{rng.choice(KEYS)}")
    with open(os.path.join(out_dir, f"fuzz{seed}.keys"), "w") as handle:
        handle.write(",".join(tokens))
print(f"wrote fuzz keys to {out_dir}")
