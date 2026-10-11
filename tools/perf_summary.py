#!/usr/bin/env python3
"""Summarize ReXGlue CPU frame CSVs without interpreting them as GPU timings."""
import argparse
import csv
import json
import math
from pathlib import Path


COUNTERS = ("draw_calls", "command_buffer_stalls", "texture_cache_hits",
            "texture_cache_misses", "pipeline_cache_hits", "pipeline_cache_misses")


def percentile(values, fraction):
    """Nearest-rank percentile: actual observed frame time, no interpolation."""
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)]


def timings(values):
    return {"mean_ms": sum(values) / len(values) / 1000,
            "p50_ms": percentile(values, .50) / 1000,
            "p95_ms": percentile(values, .95) / 1000,
            "p99_ms": percentile(values, .99) / 1000,
            "max_ms": max(values) / 1000}


def summarize(path, skip_seconds=0, duration=None, target_fps=60, stall_ms=None):
    options = (skip_seconds, target_fps, duration, stall_ms)
    if any(value is not None and not math.isfinite(value) for value in options):
        raise ValueError("time limits, target FPS and stall threshold must be finite")
    if skip_seconds < 0 or (duration is not None and duration <= 0) or target_fps <= 0:
        raise ValueError("skip must be nonnegative; duration and target FPS must be positive")
    if stall_ms is not None and stall_ms <= 0:
        raise ValueError("stall threshold must be positive")
    frame_times, swap_times, rows = [], [], []
    elapsed, zero_frames = 0, 0
    with Path(path).open(newline="", encoding="utf-8-sig") as stream:
        reader = csv.DictReader(stream)
        if not reader.fieldnames or "frame_time_us" not in reader.fieldnames:
            raise ValueError("CSV must contain frame_time_us")
        fields = reader.fieldnames
        for line, row in enumerate(reader, 2):
            if None in row or any(value is None for value in row.values()):
                raise ValueError(f"line {line}: incomplete or extra CSV fields")
            numeric = {}
            for key in fields:
                try:
                    value = float(row[key])
                except ValueError as error:
                    raise ValueError(f"line {line}: invalid {key}") from error
                if not math.isfinite(value) or value < 0:
                    raise ValueError(f"line {line}: {key} must be finite and nonnegative")
                numeric[key] = value
            dt = numeric["frame_time_us"]
            elapsed = numeric.get("elapsed_us", elapsed + dt)
            if dt == 0:
                zero_frames += 1
                continue
            if elapsed < skip_seconds * 1e6:
                continue
            if duration is not None and elapsed > (skip_seconds + duration) * 1e6:
                continue
            rows.append(numeric)
            frame_times.append(dt)
            if "issue_swap_cpu_us" in numeric:
                swap_times.append(numeric["issue_swap_cpu_us"])
    if not rows:
        raise ValueError("no positive frame intervals in selected window")
    budget = 1000 / target_fps
    threshold = stall_ms if stall_ms is not None else budget * 2
    counters = {key: sum(row[key] for row in rows) for key in COUNTERS if key in fields}
    cache_rates = {}
    for cache in ("texture", "pipeline"):
        hit, miss = f"{cache}_cache_hits", f"{cache}_cache_misses"
        if hit in counters and miss in counters:
            total = counters[hit] + counters[miss]
            cache_rates[cache] = counters[hit] / total if total else None
    notes = ["CPU guest-swap intervals; not GPU execution or displayed FPS."]
    if cache_rates:
        notes.append("Zero cache events may mean missing backend instrumentation; they do not imply cache hits.")
    return {"file": str(path), "frames": len(rows), "interval_seconds": sum(frame_times) / 1e6,
            "zero_intervals_skipped": zero_frames,
            "interval_fps": len(rows) * 1e6 / sum(frame_times),
            "frame_time": timings(frame_times),
            "target_fps": target_fps,
            "over_budget_frames": sum(dt > budget * 1000 for dt in frame_times),
            "stall_threshold_ms": threshold,
            "stall_frames": sum(dt > threshold * 1000 for dt in frame_times),
            "issue_swap_cpu": timings(swap_times) if swap_times else None,
            "counter_totals": counters, "cache_hit_rates": cache_rates, "notes": notes}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", nargs="+", type=Path)
    parser.add_argument("--skip-seconds", type=float, default=0)
    parser.add_argument("--duration", type=float, help="seconds after the skipped period")
    parser.add_argument("--target-fps", type=float, default=60)
    parser.add_argument("--stall-ms", type=float, help="default: twice target frame budget")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args()
    try:
        results = [summarize(path, args.skip_seconds, args.duration, args.target_fps, args.stall_ms)
                   for path in args.csv]
    except (ValueError, OSError) as error:
        parser.exit(2, f"perf_summary: {error}\n")
    if args.json:
        print(json.dumps(results, indent=2))
        return
    for result in results:
        ft = result["frame_time"]
        print(f'{result["file"]}: {result["frames"]} intervals, '
              f'{result["interval_seconds"]:.2f}s, {result["interval_fps"]:.2f} guest-swap FPS')
        print("  Frame ms: " + ", ".join(f'{name[:-3]} {value:.3f}' for name, value in ft.items()))
        print(f'  Over {1000 / result["target_fps"]:.3f} ms: {result["over_budget_frames"]}; '
              f'over {result["stall_threshold_ms"]:.3f} ms: {result["stall_frames"]}')
        if result["issue_swap_cpu"]:
            print("  IssueSwap CPU ms: " + ", ".join(
                f'{name[:-3]} {value:.3f}' for name, value in result["issue_swap_cpu"].items()))
        if result["counter_totals"]:
            print("  Counter totals: " + ", ".join(
                f'{key}={value:g}' for key, value in result["counter_totals"].items()))
        for note in result["notes"]:
            print(f"  {note}")


if __name__ == "__main__":
    main()
