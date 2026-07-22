#!/usr/bin/env python3
"""Plot the dictionary-size sweep.

Reads results.csv (from run_sweep.sh) and renders, per stream group, total
compressed size vs. dictionary size with the per-file / pool / pool+LDM
baselines as horizontal reference lines.

Run: uv run --no-project --with matplotlib python plot.py <results.csv> <out.png>
"""
import csv
import sys
from collections import defaultdict

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter

MB = 1024.0 * 1024.0


def load(path):
    rows = []
    with open(path) as f:
        for r in csv.DictReader(f):
            rows.append(r)
    return rows


def num(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return None


def main():
    csv_path, out_png = sys.argv[1], sys.argv[2]
    rows = load(csv_path)

    # index: (stream, level) -> {scheme/dict -> total}
    streams = ["xml", "mz", "inten"]
    # collect per (stream) the levels present
    by_stream = defaultdict(lambda: defaultdict(dict))
    baselines = defaultdict(dict)  # (stream,level) -> {perfile,pool,pool_ldm}
    raw = {}
    for r in rows:
        s, lvl, scheme = r["stream"], int(r["level"]), r["scheme"]
        tot = num(r["total_bytes"])
        if tot is None:
            continue
        raw[s] = num(r["raw_bytes"]) or raw.get(s)
        if scheme == "dict":
            md = int(r["maxdict"])
            by_stream[s][lvl][md] = tot
        else:
            baselines[(s, lvl)][scheme] = tot

    fig, axes = plt.subplots(1, 3, figsize=(16, 5.2))
    colors = {19: "#1f77b4", 3: "#d62728"}
    base_style = {"perfile": (":", "per-file"),
                  "pool": ("--", "pool"),
                  "pool_ldm": ("-.", "pool+LDM")}

    for ax, s in zip(axes, streams):
        levels = sorted(by_stream[s].keys(), reverse=True)
        title_extra = []
        for lvl in levels:
            pts = sorted(by_stream[s][lvl].items())
            xs = [p[0] / 1024.0 for p in pts]      # KB
            ys = [p[1] / MB for p in pts]           # MB
            c = colors.get(lvl, "#555")
            ax.plot(xs, ys, "o-", color=c, label=f"dict L{lvl}", zorder=3)
            # baselines for this level
            for scheme, (ls, lab) in base_style.items():
                b = baselines.get((s, lvl), {}).get(scheme)
                if b is None:
                    continue
                ax.axhline(b / MB, ls=ls, color=c, alpha=0.6, lw=1.3,
                           label=f"{lab} L{lvl}")
            # optimal dict (smallest total) for this level
            if pts:
                best = min(pts, key=lambda p: p[1])
                title_extra.append(f"L{lvl} best dict={best[0]//1024}KB")
        ax.set_xscale("log", base=2)
        ax.set_xlabel("dictionary size (KB, log2)")
        ax.set_ylabel("total compressed (MB)")
        rawmb = (raw.get(s) or 0) / MB
        ax.set_title(f"{s} stream  (raw {rawmb:,.0f} MB)")
        ax.grid(True, which="both", alpha=0.25)
        ax.legend(fontsize=7, ncol=2, loc="best")

    fig.suptitle("MSZX shared-dictionary sweep — total compressed size vs dict "
                 "size (48 same-instrument mzML, LL2092_HYEAB)", fontsize=12)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    fig.savefig(out_png, dpi=130)
    print("wrote", out_png)


if __name__ == "__main__":
    main()
