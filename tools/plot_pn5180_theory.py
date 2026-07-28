#!/usr/bin/env python3
"""Plot cube 2 nfc_max_us over time, illustrating the bimodal-at-boot theory."""
import json
from datetime import datetime
from pathlib import Path

import matplotlib.dates as mdates
import matplotlib.pyplot as plt

LOG = Path(__file__).parent.parent / "logs" / "diag_cubes.log"
SWAP_EVENT = datetime(2026, 4, 29, 22, 30)
HEALTHY_BAND_LO, HEALTHY_BAND_HI = 18000, 30000
BROKEN_THRESHOLD = 200000

per_cube = {"2": [], "6": []}
with LOG.open() as f:
    for line in f:
        try:
            d = json.loads(line)
        except json.JSONDecodeError:
            continue
        if d.get("timeout"):
            continue
        c = d.get("cube")
        if c not in per_cube:
            continue
        ts = datetime.fromisoformat(d["timestamp"].rstrip("Z"))
        per_cube[c].append((ts, d.get("nfc_max_us", 0)))

fig, ax = plt.subplots(figsize=(13, 6))

ts6 = [r[0] for r in per_cube["6"]]
y6 = [r[1] for r in per_cube["6"]]
ax.scatter(ts6, y6, s=3, alpha=0.35, color="#1f77b4",
           label="cube 6 (control — never failed)")

ts2 = [r[0] for r in per_cube["2"]]
y2 = [r[1] for r in per_cube["2"]]
ax.scatter(ts2, y2, s=4, alpha=0.55, color="#d62728",
           label="cube 2 (PN5180 swapped 2026-04-29)")

ax.axhspan(HEALTHY_BAND_LO, HEALTHY_BAND_HI, alpha=0.08, color="green",
           label="healthy band (~20–28 ms)")
ax.axhline(BROKEN_THRESHOLD, color="orange", linestyle=":", alpha=0.5,
           label=f"broken threshold ({BROKEN_THRESHOLD//1000} ms)")
ax.axvline(SWAP_EVENT, color="black", linestyle="--", alpha=0.6)
ax.annotate("PN5180 swap", xy=(SWAP_EVENT, 1_500_000),
            xytext=(8, 0), textcoords="offset points",
            fontsize=10, fontweight="bold", va="center")

ax.set_yscale("log")
ax.set_ylabel("nfc_max_us (log scale, since-boot peak NFC read time)")
ax.set_xlabel("date")
ax.set_title("PN5180 failure mode: bimodal at boot — either ~20 ms or stuck at ~1 s\n"
             "Swap restores cube 2 to the healthy band (matches cube 6 reference)")
ax.xaxis.set_major_locator(mdates.AutoDateLocator())
ax.xaxis.set_major_formatter(mdates.DateFormatter("%Y-%m-%d"))
fig.autofmt_xdate()
ax.legend(loc="center right", framealpha=0.9)
ax.grid(True, which="both", alpha=0.2)

# Theory annotations
ax.text(0.02, 0.97,
        "Theory:\n"
        " • Healthy: nfc_max_us ≈ 20–28 ms (baseline scan cycle)\n"
        " • Broken: nfc_max_us ≈ 1 s (chip stuck, firmware resets it)\n"
        " • Boots latch into one state; rare mid-run transitions\n"
        " • Cause: module-level wear (chip / passives / antenna)\n"
        " • Fix demonstrated: replace the PN5180 module",
        transform=ax.transAxes, fontsize=9, va="top",
        bbox=dict(boxstyle="round", facecolor="white", alpha=0.85, edgecolor="gray"))

out = Path(__file__).parent.parent / "logs" / "pn5180_theory.png"
fig.tight_layout()
fig.savefig(out, dpi=140)
print(f"saved {out}")
