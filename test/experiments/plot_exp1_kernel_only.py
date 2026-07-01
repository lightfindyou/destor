#!/usr/bin/env python3
"""Plot Experiment 1 kernel-only throughput: CPU vs GPU-Naive vs GPU (Ours-Full).

Data source: exp1_throughput.csv only (all series, same dataset view).
Requires RUN_GPU_NAIVE=1 when collecting exp1 data.

Metric: actual_elapsed_ms with timing=kernel (pure compute wall clock).
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

# --- edit here -----------------------------------------------------------
ALGORITHMS = ["fastcdc", "gear", "gearjump"]
ALGO_LABEL = {"fastcdc": "FastCDC", "gear": "Gear", "gearjump": "JC"}
ALGO_SHORT = {"fastcdc": "FastCDC", "gear": "Gear", "gearjump": "JC"}
DATASETS = ["Wiki", "Paper", "LinuxDist", "GCC"]

# (legend label, exp1 config_label)
SERIES = [
    ("CPU", "CPU-Serial"),
    ("GPU-Naive", "GPU-Naive"),
    ("GPU", "Ours-Full"),
]
COLORS = ["#4C78A8", "#E45756", "#54A24B"]
# -------------------------------------------------------------------------


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as fp:
        return list(csv.DictReader(fp))


def row_algo(row: dict[str, str]) -> str:
    return (row.get("chunk_algo") or row.get("algorithm") or "").strip()


def kernel_gbps(row: dict[str, str] | None) -> float:
    if not row:
        return 0.0
    cached = row.get("kernel_throughput_gbps")
    if cached not in (None, ""):
        try:
            return float(cached)
        except ValueError:
            pass
    actual_ms = float(row.get("actual_elapsed_ms") or 0.0)
    if actual_ms <= 0:
        return 0.0
    bytes_ = float(row.get("bytes") or 0.0)
    return bytes_ * 1000.0 / (actual_ms * 1024.0**3)


def pick_row(rows: list[dict[str, str]], *, dataset: str, algo: str, config: str) -> dict[str, str] | None:
    for row in rows:
        if row.get("dataset") != dataset:
            continue
        if row_algo(row) != algo:
            continue
        if row.get("config_label") != config:
            continue
        timing = (row.get("timing") or "").strip()
        if timing and timing not in ("kernel", ""):
            continue
        return row
    return None


def load_values(exp1: list[dict]) -> dict[tuple[str, str, str], float]:
    """Key: (dataset, algo, series legend label)."""
    out: dict[tuple[str, str, str], float] = {}
    for ds in DATASETS:
        for algo in ALGORITHMS:
            for legend, exp1_cfg in SERIES:
                row = pick_row(exp1, dataset=ds, algo=algo, config=exp1_cfg)
                out[(ds, algo, legend)] = kernel_gbps(row)
    return out


def svg_combined(values: dict[tuple[str, str, str], float], out_path: Path) -> None:
    width, height = 1100, 520
    margin_l, margin_t = 90, 70
    # 底部留白：越大 → 柱底与文字间距越大（plot 区域越矮）
    margin_b = 45
    # 标签距 SVG 底边的距离：越大 → 文字越靠下（与柱底间距也变大）
    label_dataset_y = height - 12
    label_algo_y = height - 28

    plot_w = width - margin_l - 40
    plot_h = height - margin_b - margin_t

    groups: list[str] = []
    for ds in DATASETS:
        for algo in ALGORITHMS:
            groups.append(f"{ds}\n{ALGO_SHORT[algo]}")

    max_v = max(values.values()) if values else 1.0
    if max_v <= 0:
        max_v = 1.0

    n_series = len(SERIES)
    group_w = plot_w / len(groups)
    bar_w = group_w / n_series * 0.75
    legends = [s[0] for s in SERIES]

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        f'<text x="{width/2}" y="34" text-anchor="middle" font-size="20" font-family="sans-serif">'
        "Experiment 1: Kernel-only Throughput (FastCDC / Gear / GearJump)</text>",
        f'<text x="{width/2}" y="56" text-anchor="middle" font-size="12" fill="#666" font-family="sans-serif">'
        "CPU (exp1) | GPU-Naive (exp2 A, 1GiB cap) | GPU Ours-Full (exp1)</text>",
    ]

    # Y grid
    for tick in range(6):
        y = margin_t + plot_h - plot_h * tick / 5
        val = max_v * tick / 5
        parts.append(f'<line x1="{margin_l}" y1="{y:.1f}" x2="{margin_l + plot_w}" y2="{y:.1f}" stroke="#eee"/>')
        parts.append(
            f'<text x="{margin_l - 8}" y="{y + 4:.1f}" text-anchor="end" font-size="11" '
            f'font-family="sans-serif">{val:.2f}</text>'
        )

    parts.append(
        f'<text x="20" y="{margin_t + plot_h/2}" font-size="13" font-family="sans-serif" '
        f'transform="rotate(-90 20,{margin_t + plot_h/2})" text-anchor="middle">'
        "Kernel-only Throughput (GB/s)</text>"
    )

    for gi, group in enumerate(groups):
        ds, short = group.split("\n")
        algo = next(a for a in ALGORITHMS if ALGO_SHORT[a] == short)
        gx = margin_l + gi * group_w + group_w * 0.12
        for si, legend in enumerate(legends):
            v = values.get((ds, algo, legend), 0.0)
            h = (v / max_v) * plot_h if max_v > 0 else 0
            x = gx + si * bar_w
            y = margin_t + plot_h - h
            color = COLORS[si % len(COLORS)]
            parts.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{bar_w:.1f}" height="{h:.1f}" fill="{color}"/>')
            if v > 0 and h > 14:
                parts.append(
                    f'<text x="{x + bar_w/2:.1f}" y="{y - 4:.1f}" text-anchor="middle" '
                    f'font-size="9" font-family="sans-serif">{v:.2f}</text>'
                )

        # dataset separator every 3 algos
        if (gi + 1) % len(ALGORITHMS) == 0 and gi + 1 < len(groups):
            sx = margin_l + (gi + 1) * group_w
            parts.append(f'<line x1="{sx:.1f}" y1="{margin_t}" x2="{sx:.1f}" y2="{margin_t + plot_h}" '
                         'stroke="#ccc" stroke-dasharray="4,4"/>')

    # X labels: dataset name under each block of 3
    for di, ds in enumerate(DATASETS):
        cx = margin_l + (di * len(ALGORITHMS) + len(ALGORITHMS) / 2) * group_w
        parts.append(
            f'<text x="{cx:.1f}" y="{label_dataset_y}" text-anchor="middle" font-size="13" '
            f'font-weight="bold" font-family="sans-serif">{ds}</text>'
        )
        for ai, algo in enumerate(ALGORITHMS):
            gx = margin_l + (di * len(ALGORITHMS) + ai) * group_w + group_w / 2
            parts.append(
                f'<text x="{gx:.1f}" y="{label_algo_y}" text-anchor="middle" font-size="10" '
                f'font-family="sans-serif">{ALGO_SHORT[algo]}</text>'
            )

    # legend
    lx = margin_l
    for i, name in enumerate(legends):
        parts.append(f'<rect x="{lx}" y="58" width="14" height="14" fill="{COLORS[i]}"/>')
        parts.append(f'<text x="{lx + 20}" y="70" font-size="12" font-family="sans-serif">{name}</text>')
        lx += 110

    parts.append("</svg>")
    out_path.write_text("\n".join(parts), encoding="utf-8")


def svg_per_algo(algo: str, values: dict[tuple[str, str, str], float], out_path: Path) -> None:
    width, height = 640, 420
    margin_l, margin_b, margin_t = 80, 80, 60
    plot_w = width - margin_l - 30
    plot_h = height - margin_b - margin_t
    legends = [s[0] for s in SERIES]

    max_v = max((values.get((ds, algo, leg), 0.0) for ds in DATASETS for leg in legends), default=1.0)
    if max_v <= 0:
        max_v = 1.0

    group_w = plot_w / len(DATASETS)
    bar_w = group_w / len(SERIES) * 0.75

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        f'<text x="{width/2}" y="30" text-anchor="middle" font-size="18" font-family="sans-serif">'
        f"Experiment 1: Kernel-only ({ALGO_LABEL[algo]})</text>",
    ]

    for di, ds in enumerate(DATASETS):
        gx = margin_l + di * group_w + group_w * 0.12
        for si, legend in enumerate(legends):
            v = values.get((ds, algo, legend), 0.0)
            h = (v / max_v) * plot_h
            x = gx + si * bar_w
            y = margin_t + plot_h - h
            parts.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{bar_w:.1f}" height="{h:.1f}" fill="{COLORS[si]}"/>')
        cx = margin_l + di * group_w + group_w / 2
        parts.append(f'<text x="{cx:.1f}" y="{height - 20}" text-anchor="middle" font-size="12" font-family="sans-serif">{ds}</text>')

    lx = margin_l
    for i, name in enumerate(legends):
        parts.append(f'<rect x="{lx}" y="42" width="12" height="12" fill="{COLORS[i]}"/>')
        parts.append(f'<text x="{lx + 18}" y="52" font-size="11" font-family="sans-serif">{name}</text>')
        lx += 100

    parts.append("</svg>")
    out_path.write_text("\n".join(parts), encoding="utf-8")


def write_report(values: dict[tuple[str, str, str], float], out_path: Path) -> None:
    lines = [
        "## 实验一：Kernel-only 吞吐对比\n",
        "![Kernel-only combined](exp1_kernel_only_combined.svg)\n",
        "指标：`actual_elapsed_ms`（纯分块计算墙钟，不含读盘/H2D/D2H；`timing=kernel`）。\n",
        "- **CPU**：`CPU-Serial`\n",
        "- **GPU-Naive**：`GPU-Naive`（与 CPU/GPU 相同数据范围）\n",
        "- **GPU**：`Ours-Full`\n",
    ]
    for algo in ALGORITHMS:
        lines.append(f"\n### {ALGO_LABEL[algo]}\n")
        lines.append("| 数据集 | CPU (GB/s) | GPU-Naive (GB/s) | GPU (GB/s) |")
        lines.append("| --- | ---: | ---: | ---: |")
        for ds in DATASETS:
            cpu = values.get((ds, algo, "CPU"), 0.0)
            naive = values.get((ds, algo, "GPU-Naive"), 0.0)
            gpu = values.get((ds, algo, "GPU"), 0.0)
            lines.append(f"| {ds} | {cpu:.3f} | {naive:.3f} | {gpu:.3f} |")
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def find_latest(results_dir: Path, prefix: str, csv_name: str) -> Path | None:
    dirs = sorted(results_dir.glob(f"{prefix}_*"), reverse=True)
    for d in dirs:
        p = d / csv_name
        if p.is_file():
            return p
    return None


def main() -> int:
    script_dir = Path(__file__).resolve().parent
    results_dir = script_dir / "results"

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exp1", type=Path, help="exp1_throughput.csv")
    parser.add_argument("--out-dir", type=Path, help="output directory (default: exp1 result dir)")
    args = parser.parse_args()

    exp1_path = args.exp1 or find_latest(results_dir, "exp1", "exp1_throughput.csv")
    if not exp1_path or not exp1_path.is_file():
        raise SystemExit("missing exp1_throughput.csv; pass --exp1")

    out_dir = args.out_dir or exp1_path.parent
    out_dir.mkdir(parents=True, exist_ok=True)

    values = load_values(read_csv(exp1_path))

    combined = out_dir / "exp1_kernel_only_combined.svg"
    svg_combined(values, combined)
    print(f"[chart] {combined}")

    for algo in ALGORITHMS:
        path = out_dir / f"exp1_kernel_only_{algo}.svg"
        svg_per_algo(algo, values, path)
        print(f"[chart] {path}")

    report = out_dir / "exp1_kernel_only_report.md"
    write_report(values, report)
    print(f"[report] {report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
