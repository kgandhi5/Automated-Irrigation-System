"""Plot the output of sim/plant_sim.cpp as light and dark PNGs for the README.

Usage: python tools/plot_simulation.py build/sim.csv build/events.csv docs/images
"""

import csv
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.lines import Line2D  # noqa: E402
from matplotlib.patches import Patch  # noqa: E402

THEMES = {
    "light": {
        "surface": "#fcfcfb",
        "text": "#0b0b0b",
        "text2": "#52514e",
        "grid": "#e4e3df",
        "measured": "#2a78d6",
        "pump": "#eb6834",
        "true": "#8d8c87",
    },
    "dark": {
        "surface": "#1a1a19",
        "text": "#f0efec",
        "text2": "#c3c2b7",
        "grid": "#33332f",
        "measured": "#3987e5",
        "pump": "#d95926",
        "true": "#8d8c87",
    },
}
CRITICAL = "#d03b3b"
WARNING = "#fab219"
TITLES = [
    "P1  fern, dries fast",
    "P2  succulent, dries slowly",
    "P3  basil, reservoir runs dry at hour 24",
    "P4  pothos, sensor cable pulled for an hour",
]


def load(sim_path, events_path):
    rows = list(csv.DictReader(open(sim_path, newline="")))
    hours = [float(r["hours"]) for r in rows]
    zones = []
    for z in range(1, 5):
        true = [float(r[f"true{z}"]) for r in rows]
        # Hide the estimate before the first sample and while the zone reports
        # a sensor fault (the value shown then is just the last good one, frozen).
        measured = [
            float("nan") if r[f"state{z}"] == "E" or float(r[f"measured{z}"]) == 0.0
            else float(r[f"measured{z}"])
            for r in rows
        ]
        zones.append({"true": true, "measured": measured, "pumps": [], "faults": []})
    for e in csv.DictReader(open(events_path, newline="")):
        if not e["zone"].startswith("P"):
            continue
        z = int(e["zone"][1]) - 1
        h = float(e["hours"])
        if e["event"] == "pump_on":
            zones[z]["pumps"].append(h)
        elif e["event"] in ("fault_sensor", "fault_no_rise", "sensor_recovered"):
            zones[z]["faults"].append((h, e["event"]))
    return hours, zones


def fault_spans(events, end):
    spans, start = [], None
    for h, kind in events:
        if kind == "fault_sensor":
            start = (h, "sensor")
        elif kind == "sensor_recovered" and start:
            spans.append((start[0], h, "sensor"))
            start = None
        elif kind == "fault_no_rise":
            spans.append((h, end, "norise"))
    return spans


def plot(hours, zones, theme_name, out_dir):
    t = THEMES[theme_name]
    plt.rcParams.update({"font.size": 10, "font.family": "DejaVu Sans"})
    fig, axes = plt.subplots(4, 1, figsize=(11, 9), sharex=True, facecolor=t["surface"])
    end = hours[-1]

    for z, ax in enumerate(axes):
        d = zones[z]
        ax.set_facecolor(t["surface"])
        for spine in ("top", "right", "left"):
            ax.spines[spine].set_visible(False)
        ax.spines["bottom"].set_color(t["grid"])
        ax.tick_params(colors=t["text2"], length=0)
        ax.grid(axis="y", color=t["grid"], linewidth=0.8)
        ax.set_ylim(50, 100)
        ax.set_yticks([60, 70, 80, 90])
        ax.set_yticklabels(["60%", "70%", "80%", "90%"])
        ax.set_xlim(0, end)

        for level, label in ((70, "start 70%"), (80, "target 80%")):
            ax.axhline(level, color=t["text2"], linewidth=0.9, linestyle=(0, (3, 3)), zorder=1)
            ax.text(end + 0.3, level, label, va="center", ha="left", fontsize=8.5, color=t["text2"])

        for start, stop, kind in fault_spans(d["faults"], end):
            color = CRITICAL if kind == "norise" else WARNING
            ax.axvspan(start, stop, color=color, alpha=0.14, linewidth=0, zorder=0)
            label = ("FAULT: no moisture rise, pump locked out" if kind == "norise"
                     else "FAULT: sensor implausible, auto-recovered")
            ax.text(start + 0.3, 96, label, fontsize=8.5, color=t["text"], va="top")

        ax.plot(hours, d["true"], color=t["true"], linewidth=1.2, linestyle=(0, (1, 1.5)), zorder=2)
        ax.plot(hours, d["measured"], color=t["measured"], linewidth=2, zorder=3,
                solid_capstyle="round")
        if d["pumps"]:
            ax.vlines(d["pumps"], 51, 55, color=t["pump"], linewidth=1.6, zorder=3)

        ax.set_title(TITLES[z], loc="left", fontsize=10.5, color=t["text"], pad=4)

    axes[-1].set_xticks(range(0, int(end) + 1, 6))
    axes[-1].set_xlabel("simulated time (hours from 08:00)", color=t["text2"])

    handles = [
        Line2D([], [], color=t["measured"], linewidth=2, label="firmware moisture estimate"),
        Line2D([], [], color=t["true"], linewidth=1.2, linestyle=(0, (1, 1.5)),
               label="true soil moisture (model)"),
        Line2D([], [], color=t["pump"], linewidth=1.6, marker="|", markersize=9, linestyle="none",
               label="pump pulse"),
        Patch(color=CRITICAL, alpha=0.25, label="latched fault"),
        Patch(color=WARNING, alpha=0.35, label="self-clearing fault"),
    ]
    legend = fig.legend(handles=handles, loc="upper left", ncol=5, frameon=False,
                        bbox_to_anchor=(0.06, 0.955), fontsize=9)
    for text in legend.get_texts():
        text.set_color(t["text2"])
    fig.suptitle("48 h closed-loop simulation: the firmware's control library driving a soil model",
                 x=0.06, y=0.995, ha="left", fontsize=12.5, color=t["text"])
    fig.subplots_adjust(left=0.06, right=0.9, top=0.9, bottom=0.07, hspace=0.38)

    out = Path(out_dir) / f"simulation-{theme_name}.png"
    fig.savefig(out, dpi=140, facecolor=t["surface"])
    plt.close(fig)
    print(f"wrote {out}")


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    hours, zones = load(sys.argv[1], sys.argv[2])
    Path(sys.argv[3]).mkdir(parents=True, exist_ok=True)
    for theme in THEMES:
        plot(hours, zones, theme, sys.argv[3])


if __name__ == "__main__":
    main()
