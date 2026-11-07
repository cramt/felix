#!/usr/bin/env python3
"""Cold-eval benchmark + drvPath equivalence check for felix.

Evaluates each target's drvPath with a baseline and a candidate `nix`, several
runs each, interleaved so drift (thermal, page cache) hits both equally.
A drvPath mismatch exits non-zero: evaluation must be bit-identical.

usage: bench.py BASELINE_NIX CANDIDATE_NIX [--runs N] [--flake PATH] [--hosts a,b]
"""

import argparse
import json
import os
import statistics
import subprocess
import sys
import tempfile
import time

STATS = ["cpuTime", "nrOpUpdateValuesCopied"]
NESTED = [("gc", "heapSize"), ("gc", "totalBytes"), ("sets", "bytes")]


def eval_once(nix, flake, host):
    attr = f"{flake}#nixosConfigurations.{host}.config.system.build.toplevel.drvPath"
    with tempfile.NamedTemporaryFile(suffix=".json") as stats_file:
        env = dict(os.environ, NIX_SHOW_STATS="1", NIX_SHOW_STATS_PATH=stats_file.name)
        start = time.monotonic()
        proc = subprocess.run(
            ["/usr/bin/env", "time", "-f", "%M", nix, "eval", "--no-eval-cache", "--raw", attr],
            env=env, capture_output=True, text=True,
        )
        wall = time.monotonic() - start
        if proc.returncode != 0:
            sys.exit(f"{nix} failed on {host}:\n{proc.stderr[-3000:]}")
        rss_kb = int(proc.stderr.strip().splitlines()[-1])
        stats = json.load(open(stats_file.name))
    row = {"drv": proc.stdout.strip(), "wall": wall, "rssMB": rss_kb / 1024}
    row.update({k: stats.get(k) for k in STATS})
    row.update({f"{a}.{b}": stats.get(a, {}).get(b) for a, b in NESTED})
    return row


def summarize(rows, key):
    vals = [r[key] for r in rows if r[key] is not None]
    return statistics.median(vals) if vals else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("baseline")
    ap.add_argument("candidate")
    ap.add_argument("--runs", type=int, default=3)
    ap.add_argument("--flake", default=os.path.expanduser("~/nixconf"))
    ap.add_argument("--hosts", default="saturn,mercury,venus,eros")
    args = ap.parse_args()

    failed = False
    metrics = ["wall", "rssMB"] + STATS + [f"{a}.{b}" for a, b in NESTED]
    for host in args.hosts.split(","):
        # one untimed run each so flake fetching/copying isn't in the numbers
        eval_once(args.baseline, args.flake, host)
        eval_once(args.candidate, args.flake, host)
        base, cand = [], []
        for _ in range(args.runs):
            base.append(eval_once(args.baseline, args.flake, host))
            cand.append(eval_once(args.candidate, args.flake, host))

        drvs = {r["drv"] for r in base + cand}
        same = len(drvs) == 1
        failed |= not same
        print(f"\n== {host}: drvPath {'identical' if same else 'MISMATCH ' + str(sorted(drvs))}")
        print(f"   {'metric':<26}{'baseline':>16}{'candidate':>16}{'change':>10}")
        for m in metrics:
            b, c = summarize(base, m), summarize(cand, m)
            if b is None or c is None:
                continue
            pct = f"{(c - b) / b * 100:+.1f}%" if b else "n/a"
            print(f"   {m:<26}{b:>16.2f}{c:>16.2f}{pct:>10}")

    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
