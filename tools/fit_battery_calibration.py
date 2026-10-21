#!/usr/bin/env python3
"""Fit a measured, workload-specific usable-runtime curve, not a chemical SOC claim.

Input is the separate calculator calibration build's CSV report. A completed
discharge and an explicitly confirmed or assumed near-full upper endpoint are
required. An assumed endpoint remains marked unverified. Interrupted reports
never silently become a 0..100% profile. No production model is installed.
"""
import argparse
import csv
import hashlib
import io
import json
import math
from pathlib import Path
from statistics import median

FIELDS = ("elapsed_ms", "rtc", "mv", "cached_mv", "raw", "os_percent", "charge_mode",
          "guard", "result", "valid", "restored", "cache_unchanged", "flags", "cpu_mhz",
          "brightness", "volume", "muted", "codec", "rate", "channels", "stable_ms", "playing_ms")


def read_report(path):
    raw = Path(path).read_bytes()
    if len(raw) > 1024 * 1024:
        raise ValueError("Calibration report exceeds its bounded size.")
    lines = raw.decode("utf-8-sig").splitlines()
    if not lines or lines[0] != "# nMedia battery calibration v1":
        raise ValueError("Not a supported nMedia battery calibration report.")
    metadata = {}
    for line in lines:
        if line.startswith("# ") and "=" in line:
            key, value = line[2:].split("=", 1)
            if key in metadata:
                raise ValueError("Duplicate calibration metadata: " + key)
            metadata[key] = value
    reader = csv.DictReader(io.StringIO("\n".join(line for line in lines if not line.startswith("#"))))
    if tuple(reader.fieldnames or ()) != FIELDS:
        raise ValueError("Unexpected calibration columns.")
    try:
        rows = [{k: int(row[k]) for k in FIELDS} for row in reader]
    except (ValueError, TypeError, KeyError) as error:
        raise ValueError("Malformed calibration observation.") from error
    if not 2 <= len(rows) <= 4096 or int(metadata.get("rows", -1)) != len(rows):
        raise ValueError("Incomplete or inconsistent calibration row count.")
    if any(a["elapsed_ms"] >= b["elapsed_ms"] for a, b in zip(rows, rows[1:])):
        raise ValueError("Calibration time must increase strictly.")
    if any(not 0 <= r["elapsed_ms"] <= 86400000 or any(v < 0 for v in r.values()) for r in rows):
        raise ValueError("Calibration contains an out-of-range observation.")
    return metadata, rows, hashlib.sha256(raw).hexdigest()


def decreasing_fit(values):
    """Least-squares nonincreasing isotonic fit, via pooled adjacent violators."""
    blocks = []
    for value in values:
        blocks.append([float(value), 1])
        while len(blocks) > 1 and blocks[-2][0] / blocks[-2][1] < blocks[-1][0] / blocks[-1][1]:
            b = blocks.pop()
            blocks[-1][0] += b[0]
            blocks[-1][1] += b[1]
    return [total / size for total, size in blocks for _ in range(size)]


def voltage_points(rows):
    begin, end = rows[0]["elapsed_ms"], rows[-1]["elapsed_ms"]
    voltages = decreasing_fit([r["mv"] for r in rows])
    groups = []
    for row, mv in zip(rows, voltages):
        percent = 100 * (end - row["elapsed_ms"]) / (end - begin)
        if groups and math.isclose(groups[-1]["mv"], mv, abs_tol=1e-9):
            groups[-1]["percent_low"] = percent
        else:
            groups.append({"mv": mv, "percent_high": percent, "percent_low": percent})
    for group in groups:
        group["percent"] = (group["percent_high"] + group["percent_low"]) / 2
    return list(reversed(groups))


def interpolate(points, mv):
    if mv <= points[0]["mv"]:
        return points[0]["percent"]
    for a, b in zip(points, points[1:]):
        if mv <= b["mv"]:
            weight = (mv - a["mv"]) / (b["mv"] - a["mv"])
            return a["percent"] + weight * (b["percent"] - a["percent"])
    return points[-1]["percent"]


def fit_report(path, full_charge_confirmed=False, minimum_minutes=30, *, near_full_assumed=False, settle_seconds=60):
    metadata, rows, digest = read_report(path)
    if full_charge_confirmed and near_full_assumed:
        raise ValueError("Choose a confirmed or assumed upper endpoint, not both.")
    if not full_charge_confirmed and not near_full_assumed:
        raise ValueError("Confirm that this run started fully charged; a partial charge cannot define the full endpoint.")
    if not 5 <= settle_seconds <= 120:
        raise ValueError("Settling interval must be between 5 and 120 seconds.")
    if metadata.get("end_reason") != "low_voltage":
        raise ValueError("This run did not reach the low-voltage endpoint. Keep it as partial evidence, not a percentage calibration.")
    if metadata.get("low_endpoint_mv") != "3500":
        raise ValueError("Unexpected reserve endpoint; do not mix calibration profiles.")
    attempts, valid, rejected = (int(metadata.get(k, -1)) for k in ("attempts", "valid_attempts", "rejected"))
    if attempts <= 0 or valid + rejected != attempts or rejected < 0 or rejected / attempts > 0.05:
        raise ValueError("Too many rejected or unaccounted acquisition attempts.")
    if any(r["charge_mode"] != 1 for r in rows if r["valid"]):
        raise ValueError("The run includes charging or an unknown charger state.")
    if not rows[-1]["valid"] or rows[-1]["mv"] > 3500:
        raise ValueError("The final observation does not confirm the reserve endpoint.")
    # Constant reference workload: music playing from USB, panel off, no
    # visualization or pause. Never mix differing clock/format/volume states.
    candidates = [r for r in rows if r["valid"] and not r["guard"] and not r["result"]
                  and r["restored"] and r["cache_unchanged"] and 2500 <= r["mv"] <= 4500
                  and r["flags"] == 13 and r["stable_ms"] >= settle_seconds * 1000]
    if len(candidates) < 20:
        raise ValueError("Not enough settled screen-off USB playback observations.")
    keys = ("cpu_mhz", "volume", "muted", "codec", "rate", "channels")
    workloads = {}
    for row in candidates:
        key = tuple(row[k] for k in keys)
        workloads.setdefault(key, []).append(row)
    workload, settled = max(workloads.items(), key=lambda item: len(item[1]))
    if len(settled) < 0.95 * len(candidates):
        raise ValueError("The workload changed substantially; fit a stable run instead.")
    if any(b["elapsed_ms"] - a["elapsed_ms"] > 180000 for a, b in zip(settled, settled[1:])):
        raise ValueError("A pause, disconnect or sampling gap interrupted the stable discharge profile.")
    begin, end = settled[0]["elapsed_ms"], settled[-1]["elapsed_ms"]
    reference_coverage = len(settled) / sum(begin <= r["elapsed_ms"] <= end for r in rows)
    if reference_coverage < 0.9:
        raise ValueError("Too much of the run used a different playback/display load.")
    if end - begin < minimum_minutes * 60000:
        raise ValueError("The completed discharge profile is too short to calibrate reliably.")
    if begin > 5 * 60000 or rows[-1]["elapsed_ms"] - end > 30000:
        raise ValueError("The settled profile does not cover both charge endpoints closely enough.")
    if settled[0]["mv"] - settled[-1]["mv"] < 150:
        raise ValueError("Insufficient voltage range for a meaningful profile.")
    observed = settled[-1]["playing_ms"] - settled[0]["playing_ms"]
    if observed < 0.98 * (end - begin):
        raise ValueError("Unobserved or nonplaying time is too large for runtime normalization.")
    points = voltage_points(settled)
    # Interleaved holdout only checks interpolation stability on this run. A
    # second discharge is still needed to measure independent prediction error.
    train = settled[::2]
    if train[-1] is not settled[-1]:
        train = train + [settled[-1]]
    held = [r for i, r in enumerate(settled) if i % 2]
    train_points = voltage_points(train)
    errors = [abs(interpolate(train_points, r["mv"]) -
                  100 * (end-r["elapsed_ms"]) / (end-begin)) for r in held]
    plateau = max(p["percent_high"] - p["percent_low"] for p in points)
    return {
        "version": 1, "status": "experimental_requires_independent_run",
        "source_sha256": digest,
        "quantity": "estimated usable playback runtime fraction to 3500mV reserve",
        "not_measured": ["chemical state of charge", "current", "capacity in mAh", "temperature"],
        "full_charge_user_confirmed": full_charge_confirmed,
        "near_full_upper_endpoint_assumed": near_full_assumed, "reserve_mv": 3500,
        "settle_seconds": settle_seconds,
        "workload": {**dict(zip(keys, workload)), "usb_source": True, "screen_off": True, "visualization": False},
        "coverage": {"rows": len(settled), "start_ms": begin, "end_ms": end,
                     "reference_runtime_minutes": (end-begin)/60000,
                     "reference_observation_fraction": reference_coverage,
                     "unmodeled_startup_ms": begin, "all_attempts": attempts, "rejected_attempts": rejected},
        "quality": {"same_run_holdout_median_error_points": median(errors),
                    "same_run_holdout_max_error_points": max(errors),
                    "widest_voltage_plateau_points": plateau,
                    "independent_validation": "pending; holdout is not independent battery validation"},
        "points": [{k: round(v, 4) for k, v in p.items()} for p in points],
        "use_limits": ["Use only with matching load and a comparable battery/temperature.",
                       "Reserve is not chemical zero charge; changing load can change usable capacity.",
                       "Do not ship this single-run profile as a universal CX/CX II percentage curve."]
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    start = parser.add_mutually_exclusive_group()
    start.add_argument("--full-charge-confirmed", action="store_true")
    start.add_argument("--near-full-assumed", action="store_true", help="Keep the upper endpoint explicitly unverified")
    parser.add_argument("--settle-seconds", type=int, default=60)
    args = parser.parse_args()
    try:
        model = fit_report(args.report, args.full_charge_confirmed,
                           near_full_assumed=args.near_full_assumed, settle_seconds=args.settle_seconds)
    except (ValueError, OSError) as error:
        parser.exit(2, f"Cannot fit calibration: {error}\n")
    args.output.write_text(json.dumps(model, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"model": str(args.output), "coverage": model["coverage"], "quality": model["quality"]}, indent=2))


if __name__ == "__main__":
    main()
