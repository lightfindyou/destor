#!/usr/bin/env python3

import csv
import os
import sys


TARGET_METRIC = "sm__sass_average_branch_targets_threads_per_instruction.pct"


def safe_float(value, default=0.0):
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def safe_int(value, default=0):
    try:
        return int(float(value))
    except (TypeError, ValueError):
        return default


def parse_metric_from_ncu_csv(path, metric_name):
    if not path or not os.path.exists(path) or os.path.getsize(path) == 0:
        return ""

    with open(path, newline="", encoding="utf-8", errors="replace") as handle:
        reader = csv.reader(handle)
        best = ""
        for row in reader:
            if not row:
                continue
            joined = ",".join(row)
            if metric_name not in joined:
                continue
            for cell in reversed(row):
                text = cell.strip().strip('"')
                try:
                    float(text)
                    best = text
                    break
                except ValueError:
                    continue
        return best


def read_single_row_csv(path):
    if not path or not os.path.exists(path):
        return None
    with open(path, newline="", encoding="utf-8", errors="replace") as handle:
        reader = csv.DictReader(handle)
        for row in reader:
            return row
    return None


def build_merged_row(summary_row):
    experiment = read_single_row_csv(summary_row.get("experiment_csv", "")) or {}
    chunks = safe_int(experiment.get("chunks", 0))
    cutoff_hits = safe_int(experiment.get("cutoff_hits", 0))
    fingerprint_updates = safe_int(experiment.get("fingerprint_updates", 0))
    jump_hits = safe_int(experiment.get("jump_hits", 0))
    jump_bytes_skipped = safe_int(experiment.get("jump_bytes_skipped", 0))
    redundant_checks = safe_int(experiment.get("redundant_checks", 0))
    warp_groups = safe_int(experiment.get("warp_groups", 0))
    cutoff_lane_sum = safe_int(experiment.get("cutoff_lane_sum", 0))
    tail_idle_lane_sum = safe_int(experiment.get("tail_idle_lane_sum", 0))
    bytes_total = safe_int(experiment.get("bytes", 0))
    elapsed_ms = safe_float(experiment.get("elapsed_ms", 0.0))

    return {
        "algorithm": summary_row.get("algorithm", experiment.get("algorithm", "")),
        "input": summary_row.get("input", experiment.get("input", "")),
        "mode": experiment.get("mode", ""),
        "avg_size": summary_row.get("avg_size", experiment.get("cfg_avg", "")),
        "mask_bits": summary_row.get("mask_bits", experiment.get("mask_bits", "")),
        "warp_window": summary_row.get("warp_window", experiment.get("warp_window", "")),
        "jump_mto": summary_row.get("jump_mto", ""),
        "status": summary_row.get("status", ""),
        "ncu_exit": summary_row.get("ncu_exit", ""),
        "bytes": bytes_total,
        "chunks": chunks,
        "elapsed_ms": experiment.get("elapsed_ms", ""),
        "throughput_mib_s": (
            (bytes_total / (1024.0 * 1024.0)) / (elapsed_ms / 1000.0)
            if elapsed_ms > 0.0
            else 0.0
        ),
        "fingerprint_updates": fingerprint_updates,
        "cutoff_hits": cutoff_hits,
        "cutoff_hit_ratio": (cutoff_hits / chunks) if chunks > 0 else 0.0,
        "jump_hits": jump_hits,
        "jump_bytes_skipped": jump_bytes_skipped,
        "avg_jump_bytes_per_chunk": (jump_bytes_skipped / chunks) if chunks > 0 else 0.0,
        "redundant_checks": redundant_checks,
        "avg_redundant_checks_per_chunk": (redundant_checks / chunks) if chunks > 0 else 0.0,
        "warp_groups": warp_groups,
        "avg_warp_groups_per_chunk": (warp_groups / chunks) if chunks > 0 else 0.0,
        "cutoff_lane_sum": cutoff_lane_sum,
        "avg_cutoff_lane": (cutoff_lane_sum / cutoff_hits) if cutoff_hits > 0 else 0.0,
        "tail_idle_lane_sum": tail_idle_lane_sum,
        "avg_tail_idle_lanes": (tail_idle_lane_sum / cutoff_hits) if cutoff_hits > 0 else 0.0,
        "avg_checks": experiment.get("avg_checks", ""),
        TARGET_METRIC: parse_metric_from_ncu_csv(summary_row.get("ncu_csv", ""), TARGET_METRIC),
        "experiment_csv": summary_row.get("experiment_csv", ""),
        "ncu_csv": summary_row.get("ncu_csv", ""),
        "ncu_log": summary_row.get("ncu_log", ""),
    }


def main(argv):
    if len(argv) != 3:
        print(
            f"Usage: {argv[0]} NCU_SUMMARY_CSV OUTPUT_CSV",
            file=sys.stderr,
        )
        return 2

    summary_csv = argv[1]
    output_csv = argv[2]

    with open(summary_csv, newline="", encoding="utf-8", errors="replace") as handle:
        summary_rows = list(csv.DictReader(handle))

    merged_rows = [build_merged_row(row) for row in summary_rows]
    fieldnames = [
        "algorithm",
        "input",
        "mode",
        "avg_size",
        "mask_bits",
        "warp_window",
        "jump_mto",
        "status",
        "ncu_exit",
        "bytes",
        "chunks",
        "elapsed_ms",
        "throughput_mib_s",
        "fingerprint_updates",
        "cutoff_hits",
        "cutoff_hit_ratio",
        "jump_hits",
        "jump_bytes_skipped",
        "avg_jump_bytes_per_chunk",
        "redundant_checks",
        "avg_redundant_checks_per_chunk",
        "warp_groups",
        "avg_warp_groups_per_chunk",
        "cutoff_lane_sum",
        "avg_cutoff_lane",
        "tail_idle_lane_sum",
        "avg_tail_idle_lanes",
        "avg_checks",
        TARGET_METRIC,
        "experiment_csv",
        "ncu_csv",
        "ncu_log",
    ]

    with open(output_csv, "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in merged_rows:
            writer.writerow(row)

    print(f"[summary] wrote merged results to {output_csv}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))