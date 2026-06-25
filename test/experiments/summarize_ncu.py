#!/usr/bin/env python3
"""Parse NCU CSV exports and build experiment-four profiling summary rows."""
import argparse
import csv
import re
import sys
from pathlib import Path
from typing import Optional


METRIC_HINTS = {
    "warp execution efficiency": ["warps_active", "branch_targets"],
    "memory throughput": ["dram__bytes", "l1tex__t_bytes", "sm__throughput"],
    "achieved occupancy": ["achieved_occupancy", "warps_active"],
}

EXP4_METRIC_SPECS = [
    # nvprof warp_execution_efficiency -> thread_inst_executed_per_inst_executed on modern NCU
    ("warp_exec_eff_pct", ["smsp__thread_inst_executed_per_inst_executed.pct"]),
    # nvprof branch_efficiency -> uniform branch targets (higher = less divergence)
    ("branch_targets_pct", [
        "smsp__sass_average_branch_targets_threads_uniform.pct",
        "smsp__sass_average_branch_targets_threads_per_instruction.pct",
    ]),
    ("dram_throughput_pct", ["dram__throughput.avg.pct_of_peak_sustained_elapsed"]),
    ("l1tex_throughput_pct", ["l1tex__throughput.avg.pct_of_peak_sustained_elapsed"]),
    ("occupancy_pct", [
        "sm__warps_active.avg.pct_of_peak_sustained_active",
        "smsp__warps_active.avg.pct_of_peak_sustained_active",
    ]),
    ("dram_bytes", ["dram__bytes.sum"]),
    ("l1tex_bytes", ["l1tex__t_bytes.sum"]),
    ("gpu_time_ns", ["gpu__time_duration.sum"]),
]

SUMMARY_FIELDS = [
    "dataset",
    "algorithm",
    "variant",
    "input",
    "kernel_name",
    "status",
    "ncu_exit",
    "warp_exec_eff_pct",
    "branch_targets_pct",
    "dram_throughput_pct",
    "l1tex_throughput_pct",
    "occupancy_pct",
    "dram_bytes",
    "l1tex_bytes",
    "gpu_time_ns",
    "ncu_report",
    "ncu_csv",
    "ncu_log",
]


def load_csv(path: Path):
    with path.open(newline="", errors="replace") as fp:
        return list(csv.DictReader(fp))


def metric_key(name: str) -> str:
    return re.sub(r"\s+", " ", (name or "").strip().lower())


def parse_metric_value(raw: str):
    if raw is None:
        return ""
    text = str(raw).strip()
    if not text or text.lower() in ("n/a", "na", "-"):
        return ""
    text = text.replace(",", "")
    try:
        val = float(text)
        if val.is_integer():
            return str(int(val))
        return f"{val:.6g}"
    except ValueError:
        return text


def pick_metric(rows, patterns, kernel_hint: str = ""):
    """Average a metric across profiled kernel launches matching kernel_hint."""
    patterns = [p.lower() for p in patterns]
    values = []
    metric_name = ""
    for row in rows:
        name = row.get("Metric Name", "") or row.get("metric_name", "")
        lower = metric_key(name)
        if not any(p in lower for p in patterns):
            continue
        kernel = row.get("Kernel Name", "") or row.get("kernel_name", "")
        if kernel_hint and kernel_hint.lower() not in kernel.lower():
            continue
        value = row.get("Metric Value", row.get("metric_value", ""))
        parsed = parse_metric_value(value)
        if not parsed:
            continue
        try:
            values.append(float(parsed))
            metric_name = name
        except ValueError:
            continue
    if not values:
        return "", ""
    avg = sum(values) / len(values)
    return metric_name, parse_metric_value(f"{avg:.6g}")


def detect_kernel(rows, hint: str):
    if hint:
        for row in rows:
            kernel = row.get("Kernel Name", "") or row.get("kernel_name", "")
            if hint.lower() in kernel.lower():
                return kernel
    for row in rows:
        kernel = row.get("Kernel Name", "") or row.get("kernel_name", "")
        if kernel:
            return kernel
    return hint


def extract_exp4_metrics(ncu_csv: Path, kernel_hint: str):
    if not ncu_csv or not ncu_csv.is_file():
        return {field: "" for field, _ in EXP4_METRIC_SPECS}, kernel_hint
    rows = load_csv(ncu_csv)
    kernel = detect_kernel(rows, kernel_hint)
    out = {}
    for field, patterns in EXP4_METRIC_SPECS:
        _, value = pick_metric(rows, patterns, kernel_hint)
        out[field] = value
    return out, kernel


def append_summary_row(summary_csv: Path, row: dict):
    summary_csv.parent.mkdir(parents=True, exist_ok=True)
    write_header = not summary_csv.exists() or summary_csv.stat().st_size == 0
    with summary_csv.open("a", newline="") as fp:
        writer = csv.DictWriter(fp, fieldnames=SUMMARY_FIELDS, extrasaction="ignore")
        if write_header:
            writer.writeheader()
        writer.writerow({k: row.get(k, "") for k in SUMMARY_FIELDS})


DISPLAY_METRICS = [
    ("warp_exec_eff_pct", "WarpEff"),
    ("branch_targets_pct", "BranchEff"),
    ("dram_throughput_pct", "DRAM"),
    ("l1tex_throughput_pct", "L1/Tex"),
    ("occupancy_pct", "Occ"),
]


def _fmt_display(val, pct=False):
    if val is None or str(val).strip() == "":
        return "—"
    if str(val).strip().lower() in ("n/a", "na", "-"):
        return "—"
    try:
        f = float(val)
        if pct:
            return f"{f:.2f}%"
        if abs(f) >= 1e6:
            return f"{f / 1e6:.2f}M"
        if abs(f) >= 1e3:
            return f"{f / 1e3:.2f}K"
        return f"{f:.4g}"
    except (TypeError, ValueError):
        return str(val)


def load_bench_stats(bench_csv: Path):
    if not bench_csv or not bench_csv.is_file():
        return {}
    rows = load_csv(bench_csv)
    if not rows:
        return {}
    row = rows[-1]
    nbytes = float(row.get("bytes") or 0)
    elapsed = float(row.get("elapsed_ms") or 0)
    actual = float(row.get("actual_elapsed_ms") or 0)
    e2e_gbps = (nbytes / (1024 ** 3)) / (elapsed / 1000.0) if elapsed > 0 else 0.0
    return {
        "files": row.get("files", ""),
        "chunks": row.get("chunks", ""),
        "bytes": nbytes,
        "elapsed_ms": elapsed,
        "actual_elapsed_ms": actual,
        "e2e_gbps": e2e_gbps,
    }


def print_exp4_result(row: dict, bench_csv: Optional[Path] = None):
    ds = row.get("dataset", "")
    algo = row.get("algorithm", "")
    variant = row.get("variant", "")
    status = row.get("status", "")
    kernel = row.get("kernel_name", "")
    print(f"  ┌─ {ds} / {algo} / {variant}  status={status}")
    print(f"  │  kernel: {kernel}")
    stats = load_bench_stats(bench_csv) if bench_csv else {}
    if stats:
        gib = stats["bytes"] / (1024 ** 3)
        print(
            f"  │  bench: {stats['files']} files, {stats['chunks']} chunks, "
            f"{gib:.2f} GiB"
        )
        print(
            f"  │         e2e {stats['e2e_gbps']:.3f} GB/s "
            f"(elapsed {stats['elapsed_ms']:.0f} ms, kernel {stats['actual_elapsed_ms']:.0f} ms)"
        )
    ncu_parts = [
        f"{label} {_fmt_display(row.get(field), pct=True)}"
        for field, label in DISPLAY_METRICS
    ]
    print(f"  │  NCU (avg over profiled launches): {' | '.join(ncu_parts)}")
    if status not in ("ok", "skipped", ""):
        log_path = row.get("ncu_log", "")
        if log_path:
            log = Path(log_path)
            if log.is_file():
                for line in log.read_text(errors="replace").splitlines():
                    if any(tok in line for tok in ("==ERROR==", "ERR_", "No kernels were profiled")):
                        print(f"  │  ! {line}")
    print("  └─")


def print_exp4_summary_table(summary_csv: Path, dataset: str = ""):
    if not summary_csv.is_file():
        return
    rows = [r for r in load_csv(summary_csv) if not dataset or r.get("dataset") == dataset]
    if not rows:
        return
    title = f"累计结果 ({dataset})" if dataset else "累计结果 (全部数据集)"
    print(f"\n  ══ {title} ══")
    header = (
        f"  {'Dataset':<10} {'Algo':<10} {'Variant':<12} {'WarpEff':>8} {'Branch':>8} "
        f"{'DRAM':>8} {'L1/Tex':>8} {'Occ':>8} {'E2E GB/s':>9} {'Status':<12}"
    )
    print(header)
    print("  " + "-" * (len(header) - 2))
    for row in rows:
        bench = Path(row.get("ncu_csv", "").replace(".csv", "_bench.csv"))
        stats = load_bench_stats(bench)
        e2e = f"{stats['e2e_gbps']:.3f}" if stats else "—"
        print(
            f"  {row.get('dataset', ''):<10} {row.get('algorithm', ''):<10} {row.get('variant', ''):<12} "
            f"{_fmt_display(row.get('warp_exec_eff_pct'), pct=True):>8} "
            f"{_fmt_display(row.get('branch_targets_pct'), pct=True):>8} "
            f"{_fmt_display(row.get('dram_throughput_pct'), pct=True):>8} "
            f"{_fmt_display(row.get('l1tex_throughput_pct'), pct=True):>8} "
            f"{_fmt_display(row.get('occupancy_pct'), pct=True):>8} "
            f"{e2e:>9} {row.get('status', ''):<12}"
        )
    print("")


def cmd_append_row(args) -> int:
    ncu_csv = Path(args.ncu_csv) if args.ncu_csv else None
    metrics, kernel = extract_exp4_metrics(ncu_csv, args.kernel)
    row = {
        "dataset": args.dataset,
        "algorithm": args.algorithm,
        "variant": args.variant,
        "input": args.input,
        "kernel_name": kernel,
        "status": args.status,
        "ncu_exit": args.ncu_exit,
        "ncu_report": args.ncu_report,
        "ncu_csv": args.ncu_csv,
        "ncu_log": args.ncu_log,
        **metrics,
    }
    append_summary_row(Path(args.append_row), row)
    if getattr(args, "print", False):
        bench_csv = Path(args.bench_csv) if getattr(args, "bench_csv", "") else None
        print_exp4_result(row, bench_csv)
    return 0


def cmd_print_summary(args) -> int:
    print_exp4_summary_table(Path(args.print_summary), args.dataset or "")
    return 0


def cmd_summarize_dir(ncu_dir: Path) -> int:
    print("# Microarchitectural Profiling Summary\n")
    for csv_path in sorted(ncu_dir.glob("*.csv")):
        if csv_path.name.endswith("_bench.csv"):
            continue
        rows = load_csv(csv_path)
        print(f"## {csv_path.stem}\n")
        print("| Metric | Value |")
        print("| --- | --- |")
        for label, hints in METRIC_HINTS.items():
            name, value = pick_metric(rows, hints)
            if name:
                print(f"| {label} ({name}) | {value} |")
        print()
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Summarize NCU profiling CSV exports")
    parser.add_argument("ncu_dir", nargs="?", help="Directory of NCU CSV files (legacy mode)")
    parser.add_argument("--append-row", metavar="SUMMARY_CSV", help="Append one exp4 summary row")
    parser.add_argument("--dataset", default="")
    parser.add_argument("--algorithm", default="")
    parser.add_argument("--variant", default="")
    parser.add_argument("--input", default="")
    parser.add_argument("--kernel", default="")
    parser.add_argument("--status", default="")
    parser.add_argument("--ncu-exit", default="0")
    parser.add_argument("--ncu-report", default="")
    parser.add_argument("--ncu-csv", default="")
    parser.add_argument("--ncu-log", default="")
    parser.add_argument("--bench-csv", default="", help="chunkingTool bench CSV for e2e stats")
    parser.add_argument("--print", action="store_true", help="Print human-readable result after append")
    parser.add_argument("--print-summary", metavar="SUMMARY_CSV", help="Print running summary table")
    args = parser.parse_args()

    if args.print_summary:
        return cmd_print_summary(args)
    if args.append_row:
        return cmd_append_row(args)
    if not args.ncu_dir:
        parser.print_usage(file=sys.stderr)
        return 2
    return cmd_summarize_dir(Path(args.ncu_dir))


if __name__ == "__main__":
    raise SystemExit(main())
