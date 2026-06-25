#!/usr/bin/env python3
import csv
import math
import sys
from collections import defaultdict
from pathlib import Path


def read_csv(path: Path):
    with path.open(newline="") as fp:
        return list(csv.DictReader(fp))


def row_chunk_algo(row):
    algo = (row.get("chunk_algo") or row.get("algorithm") or "fastcdc").strip()
    if algo == "jc":
        return "gearjump"
    return algo


def algorithms_in_rows(rows):
    seen = []
    for row in rows:
        algo = row_chunk_algo(row)
        if algo not in seen:
            seen.append(algo)
    return seen


def filter_algo_rows(rows, algo):
    return [r for r in rows if row_chunk_algo(r) == algo]


def e2e_gbps(row):
    elapsed = float(row.get("elapsed_ms") or 0)
    nbytes = float(row.get("bytes") or 0)
    if elapsed <= 0:
        return 0.0
    return (nbytes / (1024 ** 3)) / (elapsed / 1000.0)


def e2e_tbh(row):
    return e2e_gbps(row) * 3600.0 / 1024.0


def svg_bar_chart(title, groups, series, values, y_label, out_path):
    width, height = 920, 520
    margin_l, margin_b, margin_t = 90, 110, 70
    plot_w = width - margin_l - 40
    plot_h = height - margin_b - margin_t
    max_v = max(values.values()) if values else 1.0
    if max_v <= 0:
        max_v = 1.0

    bar_group_w = plot_w / max(len(groups), 1)
    bar_w = bar_group_w / max(len(series), 1) * 0.75
    colors = ["#4C78A8", "#F58518", "#E45756", "#72B7B2", "#54A24B", "#B279A2"]

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">',
        f'<rect width="100%" height="100%" fill="white"/>',
        f'<text x="{width/2}" y="34" text-anchor="middle" font-size="20" font-family="sans-serif">{title}</text>',
        f'<text x="24" y="{margin_t + plot_h/2}" transform="rotate(-90 24,{margin_t + plot_h/2})" text-anchor="middle" font-size="14" font-family="sans-serif">{y_label}</text>',
    ]

    for i in range(5):
        y = margin_t + plot_h - plot_h * i / 4
        val = max_v * i / 4
        parts.append(f'<line x1="{margin_l}" y1="{y:.1f}" x2="{margin_l + plot_w}" y2="{y:.1f}" stroke="#ddd"/>')
        parts.append(
            f'<text x="{margin_l - 8}" y="{y + 4:.1f}" text-anchor="end" font-size="11" font-family="sans-serif">{val:.2f}</text>'
        )

    for gi, group in enumerate(groups):
        gx = margin_l + gi * bar_group_w + bar_group_w * 0.12
        for si, s in enumerate(series):
            key = (group, s)
            val = values.get(key, 0.0)
            h = 0 if max_v == 0 else plot_h * val / max_v
            x = gx + si * (bar_w + 4)
            y = margin_t + plot_h - h
            color = colors[si % len(colors)]
            parts.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{bar_w:.1f}" height="{h:.1f}" fill="{color}"/>')
            parts.append(
                f'<text x="{x + bar_w/2:.1f}" y="{y - 4:.1f}" text-anchor="middle" font-size="10" font-family="sans-serif">{val:.2f}</text>'
            )
        parts.append(
            f'<text x="{margin_l + gi * bar_group_w + bar_group_w/2:.1f}" y="{height - 58}" text-anchor="middle" font-size="12" font-family="sans-serif">{group}</text>'
        )

    lx = margin_l
    for si, s in enumerate(series):
        color = colors[si % len(colors)]
        parts.append(f'<rect x="{lx}" y="48" width="14" height="14" fill="{color}"/>')
        parts.append(f'<text x="{lx + 20}" y="60" font-size="12" font-family="sans-serif">{s}</text>')
        lx += 150

    parts.append("</svg>")
    out_path.write_text("\n".join(parts), encoding="utf-8")


ABLATION_SERIES = [
    "A-GPU-Naive",
    "B-Segment-Batch",
    "C-Pipeline",
    "D-Full-ParallelScan",
]

ABLATION_X_LABELS = ["A: Naive", "B: Seg-Batch", "C: Pipeline", "D: Full"]


def svg_line_chart(title, x_labels, series_names, values, y_label, out_path):
    width, height = 920, 520
    margin_l, margin_b, margin_t = 90, 110, 70
    plot_w = width - margin_l - 40
    plot_h = height - margin_b - margin_t
    max_v = max(values.values()) if values else 1.0
    if max_v <= 0:
        max_v = 1.0
    colors = ["#4C78A8", "#F58518", "#E45756", "#72B7B2", "#54A24B", "#B279A2"]
    n_x = max(len(x_labels), 1)

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">',
        f'<rect width="100%" height="100%" fill="white"/>',
        f'<text x="{width/2}" y="34" text-anchor="middle" font-size="20" font-family="sans-serif">{title}</text>',
        f'<text x="24" y="{margin_t + plot_h/2}" transform="rotate(-90 24,{margin_t + plot_h/2})" text-anchor="middle" font-size="14" font-family="sans-serif">{y_label}</text>',
    ]

    for i in range(5):
        y = margin_t + plot_h - plot_h * i / 4
        val = max_v * i / 4
        parts.append(f'<line x1="{margin_l}" y1="{y:.1f}" x2="{margin_l + plot_w}" y2="{y:.1f}" stroke="#ddd"/>')
        parts.append(
            f'<text x="{margin_l - 8}" y="{y + 4:.1f}" text-anchor="end" font-size="11" font-family="sans-serif">{val:.2f}</text>'
        )

    for xi, label in enumerate(x_labels):
        x = margin_l + plot_w * xi / max(n_x - 1, 1)
        parts.append(
            f'<text x="{x:.1f}" y="{height - 58}" text-anchor="middle" font-size="12" font-family="sans-serif">{label}</text>'
        )

    for si, name in enumerate(series_names):
        color = colors[si % len(colors)]
        points = []
        for xi, cfg in enumerate(ABLATION_SERIES[: len(x_labels)]):
            val = values.get((name, cfg), 0.0)
            x = margin_l + plot_w * xi / max(n_x - 1, 1)
            y = margin_t + plot_h - (0 if max_v == 0 else plot_h * val / max_v)
            points.append(f"{x:.1f},{y:.1f}")
            parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4" fill="{color}"/>')
        if points:
            parts.append(
                f'<polyline fill="none" stroke="{color}" stroke-width="2.5" points="{" ".join(points)}"/>'
            )

    lx = margin_l
    for si, name in enumerate(series_names):
        color = colors[si % len(colors)]
        parts.append(f'<line x1="{lx}" y1="52" x2="{lx + 18}" y2="52" stroke="{color}" stroke-width="2.5"/>')
        parts.append(f'<text x="{lx + 24}" y="56" font-size="12" font-family="sans-serif">{name}</text>')
        lx += 140

    parts.append("</svg>")
    out_path.write_text("\n".join(parts), encoding="utf-8")


def svg_param_line_chart(title, x_labels, x_keys, series_names, values, y_label, out_path):
    width, height = 920, 520
    margin_l, margin_b, margin_t = 90, 110, 70
    plot_w = width - margin_l - 40
    plot_h = height - margin_b - margin_t
    max_v = max(values.values()) if values else 1.0
    min_v = min(values.values()) if values else 0.0
    if max_v <= min_v:
        max_v = min_v + 1.0
    colors = ["#4C78A8", "#F58518", "#E45756", "#72B7B2", "#54A24B", "#B279A2"]
    n_x = max(len(x_labels), 1)

    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">',
        f'<rect width="100%" height="100%" fill="white"/>',
        f'<text x="{width/2}" y="34" text-anchor="middle" font-size="20" font-family="sans-serif">{title}</text>',
        f'<text x="24" y="{margin_t + plot_h/2}" transform="rotate(-90 24,{margin_t + plot_h/2})" text-anchor="middle" font-size="14" font-family="sans-serif">{y_label}</text>',
    ]

    for i in range(5):
        y = margin_t + plot_h - plot_h * i / 4
        val = min_v + (max_v - min_v) * i / 4
        parts.append(f'<line x1="{margin_l}" y1="{y:.1f}" x2="{margin_l + plot_w}" y2="{y:.1f}" stroke="#ddd"/>')
        parts.append(
            f'<text x="{margin_l - 8}" y="{y + 4:.1f}" text-anchor="end" font-size="11" font-family="sans-serif">{val:.2f}</text>'
        )

    for xi, label in enumerate(x_labels):
        x = margin_l + plot_w * xi / max(n_x - 1, 1)
        parts.append(
            f'<text x="{x:.1f}" y="{height - 58}" text-anchor="middle" font-size="12" font-family="sans-serif">{label}</text>'
        )

    for si, name in enumerate(series_names):
        color = colors[si % len(colors)]
        points = []
        for xi, xkey in enumerate(x_keys):
            val = values.get((name, xkey), min_v)
            x = margin_l + plot_w * xi / max(n_x - 1, 1)
            span = max_v - min_v
            h = 0 if span == 0 else plot_h * (val - min_v) / span
            y = margin_t + plot_h - h
            points.append(f"{x:.1f},{y:.1f}")
            parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4" fill="{color}"/>')
        if points:
            parts.append(
                f'<polyline fill="none" stroke="{color}" stroke-width="2.5" points="{" ".join(points)}"/>'
            )

    lx = margin_l
    for si, name in enumerate(series_names):
        color = colors[si % len(colors)]
        parts.append(f'<line x1="{lx}" y1="52" x2="{lx + 18}" y2="52" stroke="{color}" stroke-width="2.5"/>')
        parts.append(f'<text x="{lx + 24}" y="56" font-size="12" font-family="sans-serif">{name}</text>')
        lx += 140

    parts.append("</svg>")
    out_path.write_text("\n".join(parts), encoding="utf-8")


def parse_sweep_label(label: str, prefix: str) -> str:
    if label.startswith(prefix):
        return label[len(prefix) :]
    return label


def find_plateau_task(rows, threshold: float = 0.95):
    points = []
    for row in rows:
        key = parse_sweep_label(row.get("config_label", ""), "pipeline_tasks=")
        try:
            tasks = int(key)
        except ValueError:
            continue
        points.append((tasks, e2e_gbps(row)))
    if not points:
        return None, 0.0
    points.sort()
    peak = max(v for _, v in points)
    if peak <= 0:
        return None, peak
    for tasks, val in points:
        if val >= peak * threshold:
            return tasks, peak
    return points[-1][0], peak


def sweep_int_key(row: dict, prefix: str) -> int:
    label = parse_sweep_label(row.get("config_label", ""), prefix)
    try:
        return int(label)
    except ValueError:
        return 0


def write_exp3_sensitivity(out_dir: Path):
    path31 = out_dir / "exp3_pipeline_tasks.csv"
    path32 = out_dir / "exp3_thread_blocks.csv"
    if not path31.exists() and not path32.exists():
        return

    md = [
        "## 实验三：参数敏感性分析 (Parameter Sensitivity)\n",
        "工具：主仓库 `chunkingTool` + GPU 完整框架（fastcdc / gear / gearjump）。\n",
        "Segment 窗口为编译期固定值（约 1MiB + chunk_max），本实验仅扫描 pipeline_tasks 与 threads/block。\n",
    ]

    def write_exp31_for_algo(algo_rows, algo, chart_suffix):
        if not algo_rows:
            return
        by_ds = defaultdict(list)
        dataset_order = []
        task_keys = []
        for row in algo_rows:
            ds = row.get("dataset") or Path(row.get("input", "")).name
            key = parse_sweep_label(row.get("config_label", ""), "pipeline_tasks=")
            if ds not in by_ds:
                dataset_order.append(ds)
            by_ds[ds].append(row)
            if key not in task_keys:
                task_keys.append(key)
        task_keys.sort(key=lambda x: int(x) if x.isdigit() else x)

        values31 = {}
        for ds in dataset_order:
            for row in by_ds[ds]:
                key = parse_sweep_label(row.get("config_label", ""), "pipeline_tasks=")
                values31[(ds, key)] = e2e_gbps(row)

        svg_param_line_chart(
            f"Experiment 3.1 ({algo}): Pipeline Task Count vs End-to-End Throughput",
            task_keys,
            task_keys,
            dataset_order,
            values31,
            "Throughput (GB/s)",
            out_dir / chart_suffix,
        )

        md.append(f"### 3.1 子批次任务数 — {algo}\n")
        md.append("固定 `threads/block` 为脚本中的 `GPU_THREADS`（默认 128）。\n")
        md.append("| 数据集 | pipeline_tasks | 端到端 (GB/s) | 相对最小值提升 |")
        md.append("| --- | ---: | ---: | ---: |")
        for ds in dataset_order:
            pts = []
            for row in sorted(by_ds[ds], key=lambda r: sweep_int_key(r, "pipeline_tasks=")):
                key = parse_sweep_label(row.get("config_label", ""), "pipeline_tasks=")
                pts.append((key, e2e_gbps(row)))
            base = min((v for _, v in pts), default=0.0)
            for key, val in pts:
                gain = ((val - base) / base * 100.0) if base > 0 else 0.0
                md.append(f"| {ds} | {key} | {val:.3f} | {gain:+.1f}% |")
            plateau, peak = find_plateau_task(by_ds[ds])
            if plateau is not None:
                md.append(f"\n**{ds}** 平坦区起点（≥峰值 95%）：`pipeline_tasks={plateau}`，峰值 **{peak:.3f} GB/s**。\n")
        md.append("")

    def write_exp32_for_algo(algo_rows, algo, chart_suffix):
        if not algo_rows:
            return
        by_ds = defaultdict(list)
        dataset_order = []
        thread_keys = []
        for row in algo_rows:
            ds = row.get("dataset") or Path(row.get("input", "")).name
            key = parse_sweep_label(row.get("config_label", ""), "threads=")
            if ds not in by_ds:
                dataset_order.append(ds)
            by_ds[ds].append(row)
            if key not in thread_keys:
                thread_keys.append(key)
        thread_keys.sort(key=lambda x: int(x) if x.isdigit() else x)

        values32 = {}
        for ds in dataset_order:
            for row in by_ds[ds]:
                key = parse_sweep_label(row.get("config_label", ""), "threads=")
                ms = float(row.get("actual_elapsed_ms") or 0)
                values32[(ds, key)] = ms

        svg_param_line_chart(
            f"Experiment 3.2 ({algo}): Thread Block Size vs Kernel Time",
            thread_keys,
            thread_keys,
            dataset_order,
            values32,
            "Kernel Time (ms)",
            out_dir / chart_suffix,
        )

        md.append(f"### 3.2 线程块规模 — {algo}\n")
        md.append("指标：`actual_elapsed_ms`（GPU kernel 累计时间，不含 H2D/D2H）。\n")
        md.append("| 数据集 | threads/block | Kernel (ms) | 端到端 (GB/s) |")
        md.append("| --- | ---: | ---: | ---: |")
        for ds in dataset_order:
            for row in sorted(by_ds[ds], key=lambda r: sweep_int_key(r, "threads=")):
                key = parse_sweep_label(row.get("config_label", ""), "threads=")
                kernel_ms = float(row.get("actual_elapsed_ms") or 0)
                md.append(f"| {ds} | {key} | {kernel_ms:.2f} | {e2e_gbps(row):.3f} |")
        md.append("")

    if path31.exists():
        rows31 = read_csv(path31)
        for algo in algorithms_in_rows(rows31):
            write_exp31_for_algo(filter_algo_rows(rows31, algo), algo, f"exp3_pipeline_tasks_{algo}.svg")
        if len(algorithms_in_rows(rows31)) == 1:
            single = algorithms_in_rows(rows31)[0]
            src = out_dir / f"exp3_pipeline_tasks_{single}.svg"
            if src.exists():
                (out_dir / "exp3_pipeline_tasks.svg").write_text(src.read_text(encoding="utf-8"), encoding="utf-8")

    if path32.exists():
        rows32 = read_csv(path32)
        for algo in algorithms_in_rows(rows32):
            write_exp32_for_algo(filter_algo_rows(rows32, algo), algo, f"exp3_thread_blocks_{algo}.svg")
        if len(algorithms_in_rows(rows32)) == 1:
            single = algorithms_in_rows(rows32)[0]
            src = out_dir / f"exp3_thread_blocks_{single}.svg"
            if src.exists():
                (out_dir / "exp3_thread_blocks.svg").write_text(src.read_text(encoding="utf-8"), encoding="utf-8")

    (out_dir / "exp3_sensitivity_report.md").write_text("\n".join(md), encoding="utf-8")


def pct_throughput_gain(prev: float, cur: float) -> float:
    if prev <= 0:
        return 0.0
    return (cur - prev) / prev * 100.0


def write_exp1(out_dir: Path):
    rows = read_csv(out_dir / "exp1_throughput.csv")
    deduped = {}
    order = []
    for row in rows:
        key = (row_chunk_algo(row), row.get("dataset"), row.get("config_label"), row.get("mode"))
        if key not in deduped:
            order.append(key)
        deduped[key] = row

    series_order = ["CPU-Serial", "CPU-Parallel", "GPU-Naive", "Ours-Full"]
    md = [
        "## 实验一：端到端吞吐对比\n",
        "端到端吞吐使用 `elapsed_ms`（墙钟时间），包含 PCIe H2D 拷贝、Kernel 执行与 D2H 回收。\n",
        "默认覆盖 fastcdc、gear、gearjump 三种分块算法（`chunk_algo` 列）。\n",
    ]

    for algo in algorithms_in_rows(rows):
        algo_rows = [deduped[k] for k in order if k[0] == algo]
        groups = []
        series_seen = []
        values = {}
        for row in algo_rows:
            ds = row.get("dataset") or Path(row.get("input", "")).name
            label = row.get("config_label") or row.get("algorithm", "")
            if ds not in groups:
                groups.append(ds)
            if label not in series_seen:
                series_seen.append(label)
            values[(ds, label)] = e2e_gbps(row)
        series = [s for s in series_order if s in series_seen]
        series.extend(s for s in series_seen if s not in series)

        chart_path = out_dir / f"exp1_throughput_{algo}.svg"
        svg_bar_chart(
            f"Experiment 1 ({algo}): End-to-End Chunking Throughput (H2D + Kernel + D2H)",
            groups,
            series,
            values,
            "Throughput (GB/s)",
            chart_path,
        )
        if len(algorithms_in_rows(rows)) == 1:
            (out_dir / "exp1_throughput.svg").write_text(
                chart_path.read_text(encoding="utf-8"), encoding="utf-8"
            )

        md.append(f"### {algo}\n")
        md.append("| 数据集 | 配置 | 原始大小 (GiB) | 端到端吞吐 (GB/s) | 端到端吞吐 (TB/h) | Kernel-only (GB/s) |")
        md.append("| --- | --- | ---: | ---: | ---: | ---: |")
        for row in algo_rows:
            ds = row.get("dataset") or Path(row.get("input", "")).name
            label = row.get("config_label") or row.get("algorithm", "")
            nbytes = float(row.get("bytes") or 0)
            kernel_ms = float(row.get("actual_elapsed_ms") or 0)
            kernel_gbps = (nbytes / (1024 ** 3)) / (kernel_ms / 1000.0) if kernel_ms > 0 else 0.0
            md.append(
                f"| {ds} | {label} | {nbytes/(1024**3):.3f} | {e2e_gbps(row):.3f} | {e2e_tbh(row):.3f} | {kernel_gbps:.3f} |"
            )
        md.append("")

    (out_dir / "exp1_report.md").write_text("\n".join(md), encoding="utf-8")


def write_exp2(out_dir: Path):
    rows = read_csv(out_dir / "exp2_correctness.csv")
    if rows and "mode" not in rows[0]:
        md = (out_dir / "exp2_report.md").read_text(encoding="utf-8") if (out_dir / "exp2_report.md").exists() else ""
        if not md:
            md = "## 实验二：正确性与去重率一致性\n"
            md += "| 数据集 | Chunk Count (CPU/GPU) | Avg Size (CPU/GPU) | Dedup Ratio (CPU/GPU) | 一致? |\n"
            md += "| --- | --- | --- | --- | --- |\n"
            for row in rows:
                md += (
                    f"| {row['dataset']} | {row['cpu_chunk_count']} / {row['gpu_chunk_count']} | "
                    f"{row['cpu_avg_chunk_size']} / {row['gpu_avg_chunk_size']} | "
                    f"{row['cpu_dedup_ratio']} / {row['gpu_dedup_ratio']} | {row['match']} |\n"
                )
        (out_dir / "exp2_report.md").write_text(md, encoding="utf-8")
        return

    md = ["## 实验二：正确性与去重率一致性\n"]
    md.append(
        "| 数据集 | CPU Chunk Count | GPU Chunk Count | CPU Avg Size | GPU Avg Size | CPU Dedup Ratio | GPU Dedup Ratio | 一致? |"
    )
    md.append("| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |")
    by_ds = defaultdict(dict)
    for row in rows:
        by_ds[row["dataset"]][row["mode"]] = row
    for ds, modes in sorted(by_ds.items()):
        cpu = modes.get("cpu", {})
        gpu = modes.get("gpu", {})
        same = (
            cpu.get("chunk_count") == gpu.get("chunk_count")
            and cpu.get("avg_chunk_size") == gpu.get("avg_chunk_size")
            and cpu.get("dedup_ratio") == gpu.get("dedup_ratio")
        )
        md.append(
            f"| {ds} | {cpu.get('chunk_count','')} | {gpu.get('chunk_count','')} | "
            f"{cpu.get('avg_chunk_size','')} | {gpu.get('avg_chunk_size','')} | "
            f"{cpu.get('dedup_ratio','')} | {gpu.get('dedup_ratio','')} | {'✅' if same else '❌'} |"
        )
    (out_dir / "exp2_report.md").write_text("\n".join(md), encoding="utf-8")



def _write_exp2_gpu_ablation(out_dir: Path, algo: str, rows, md):
    by_ds = defaultdict(dict)
    dataset_order = []
    for row in rows:
        ds = row.get("dataset") or Path(row.get("input", "")).name
        label = row.get("config_label") or row.get("algorithm", "")
        if ds not in by_ds:
            dataset_order.append(ds)
        by_ds[ds][label] = row

    values = {}
    for ds in dataset_order:
        for label in ABLATION_SERIES:
            row = by_ds[ds].get(label)
            if row:
                values[(ds, label)] = e2e_gbps(row)

    svg_bar_chart(
        f"Experiment 2 ({algo}): Ablation Study (End-to-End Throughput)",
        dataset_order,
        ABLATION_SERIES,
        values,
        "Throughput (GB/s)",
        out_dir / f"exp2_ablation_{algo}.svg",
    )

    line_values = {}
    for ds in dataset_order:
        for label in ABLATION_SERIES:
            line_values[(ds, label)] = values.get((ds, label), 0.0)
    svg_line_chart(
        f"Experiment 2 ({algo}): Ablation Progression (A → B → C → D)",
        ABLATION_X_LABELS,
        dataset_order,
        line_values,
        "Throughput (GB/s)",
        out_dir / f"exp2_ablation_progress_{algo}.svg",
    )
    if algo == "fastcdc":
        (out_dir / "exp2_ablation.svg").write_text(
            (out_dir / f"exp2_ablation_{algo}.svg").read_text(encoding="utf-8"), encoding="utf-8"
        )
        (out_dir / "exp2_ablation_progress.svg").write_text(
            (out_dir / f"exp2_ablation_progress_{algo}.svg").read_text(encoding="utf-8"),
            encoding="utf-8",
        )

    step_keys = [
        ("A→B", "A-GPU-Naive", "B-Segment-Batch", "Segment-Batch"),
        ("B→C", "B-Segment-Batch", "C-Pipeline", "Pipeline 重叠"),
        ("C→D", "C-Pipeline", "D-Full-ParallelScan", "Parallel Scan"),
    ]
    step_gains = {key: [] for key, _, _, _ in step_keys}
    total_gains = []

    md.append(f"### {algo} 消融 (A→D)\n")
    for ds in dataset_order:
        md.append(f"#### {ds}\n")
        md.append("| 配置 | 吞吐 (GB/s) | 相对上一级 | 相对 Naive 累计 |")
        md.append("| --- | ---: | ---: | ---: |")
        prev = None
        base = values.get((ds, ABLATION_SERIES[0]), 0.0)
        for label in ABLATION_SERIES:
            cur = values.get((ds, label), 0.0)
            step = pct_throughput_gain(prev, cur) if prev is not None else 0.0
            total = pct_throughput_gain(base, cur) if base > 0 else 0.0
            step_s = f"{step:+.1f}%" if prev is not None else "—"
            md.append(f"| {label} | {cur:.3f} | {step_s} | {total:+.1f}% |")
            prev = cur
        final = values.get((ds, ABLATION_SERIES[-1]), 0.0)
        if base > 0:
            total_gains.append(pct_throughput_gain(base, final))
        for step_key, from_l, to_l, _ in step_keys:
            v0 = values.get((ds, from_l), 0.0)
            v1 = values.get((ds, to_l), 0.0)
            if v0 > 0:
                step_gains[step_key].append(pct_throughput_gain(v0, v1))
        md.append("")

    md.append(f"#### {algo} 跨数据集平均提升\n")
    md.append("| 步骤 | 含义 | 平均吞吐提升 |")
    md.append("| --- | --- | ---: |")
    step_avgs = {}
    for step_key, _, _, meaning in step_keys:
        gains = step_gains[step_key]
        avg = sum(gains) / len(gains) if gains else 0.0
        step_avgs[step_key] = avg
        md.append(f"| {step_key} | {meaning} | **{avg:+.1f}%** |")
    if total_gains:
        avg_total = sum(total_gains) / len(total_gains)
        md.append(f"\nNaive → Full 平均累计提升：**{avg_total:+.1f}%**。\n")
    md.append("")


def write_exp2_ablation(out_dir: Path):
    csv_path = out_dir / "exp2_ablation.csv"
    if not csv_path.exists():
        return

    rows = read_csv(csv_path)
    md = [
        "## 实验二：消融分析 (Ablation Study)\n",
        "fastcdc / gear / gearjump 均执行 **A→D** GPU 消融：\n",
        "- **fastcdc / gearjump**：B/C 使用历史 worktree 二进制（不修改 worktree 源码）\n",
        "- **gear**：B/C/D 使用主仓库 `chunkingTool`（Gear GPU 仅实现在主仓库）\n",
        "**A-GPU-Naive 仅处理每个数据集的前 1GB**；B/C/D 使用完整数据集。\n",
        "端到端吞吐使用 `elapsed_ms`（墙钟时间 = H2D + Kernel + D2H）。\n",
    ]

    for algo in algorithms_in_rows(rows):
        algo_rows = filter_algo_rows(rows, algo)
        if algo_rows and algo in ("fastcdc", "gear", "gearjump"):
            _write_exp2_gpu_ablation(out_dir, algo, algo_rows, md)

    (out_dir / "exp2_ablation_report.md").write_text("\n".join(md), encoding="utf-8")


def write_exp3(out_dir: Path):
    rows = read_csv(out_dir / "exp3_ablation.csv")
    ordered = ["A-GPU-Naive", "B-Segment-Batch", "C-Pipeline", "D-Full-ParallelScan"]
    vals = {}
    for row in rows:
        label = row.get("config_label", "")
        vals[label] = e2e_gbps(row)

    md = ["## 实验三：消融分析\n", "| 配置 | 端到端吞吐 (GB/s) | 相对上一级提升 | 相对 Naive 累计提升 |"]
    md.append("| --- | ---: | ---: | ---: |")
    prev = None
    base = vals.get(ordered[0], 0.0)
    for label in ordered:
        cur = vals.get(label, 0.0)
        step = ((cur - prev) / prev * 100.0) if prev and prev > 0 else 0.0
        total = ((cur - base) / base * 100.0) if base > 0 else 0.0
        md.append(f"| {label} | {cur:.3f} | {step:+.1f}% | {total:+.1f}% |")
        prev = cur
    (out_dir / "exp3_report.md").write_text("\n".join(md), encoding="utf-8")

    values = {("Ablation", label): vals.get(label, 0.0) for label in ordered}
    svg_bar_chart(
        "Experiment 3: Ablation Study (End-to-End)",
        ["Ablation"],
        ordered,
        values,
        "Throughput (GB/s)",
        out_dir / "exp3_ablation.svg",
    )


def _fmt_metric(val, suffix=""):
    if val is None or val == "":
        return "—"
    if str(val).strip().lower() in ("n/a", "na", "-"):
        return "—"
    try:
        f = float(val)
        if suffix == "%":
            return f"{f:.2f}%"
        if abs(f) >= 1e9:
            return f"{f / 1e9:.3f} G"
        if abs(f) >= 1e6:
            return f"{f / 1e6:.3f} M"
        if abs(f) >= 1e3:
            return f"{f / 1e3:.3f} K"
        return f"{f:.4g}{suffix}"
    except (TypeError, ValueError):
        return str(val)


def _metric_delta(naive, ours, higher_is_better=True):
    try:
        n = float(naive)
        o = float(ours)
    except (TypeError, ValueError):
        return "—"
    if n == 0:
        return "—"
    pct = (o - n) / n * 100.0
    if not higher_is_better:
        pct = -pct
    return f"{pct:+.1f}%"


def write_exp4_profiling(out_dir: Path):
    csv_path = out_dir / "exp4_profiling.csv"
    if not csv_path.exists():
        return

    rows = read_csv(csv_path)
    by_algo_ds = defaultdict(lambda: defaultdict(dict))
    algo_order = []
    dataset_order = []
    for row in rows:
        ds = row.get("dataset", "")
        algo = (row.get("algorithm") or "fastcdc").strip()
        variant = row.get("variant", "")
        if not ds:
            continue
        if algo not in algo_order:
            algo_order.append(algo)
        if ds not in dataset_order:
            dataset_order.append(ds)
        by_algo_ds[algo][ds][variant] = row

    variants = ["GPU-Naive", "Ours-Full"]
    metric_rows = [
        ("warp_exec_eff_pct", "Warp Execution Efficiency (active threads %)", True),
        ("branch_targets_pct", "Branch Efficiency (uniform targets %)", True),
        ("dram_throughput_pct", "Global Memory Throughput (% peak)", True),
        ("l1tex_throughput_pct", "L1/Tex Throughput (% peak, incl. shared)", True),
        ("occupancy_pct", "Achieved Occupancy (% peak)", True),
        ("dram_bytes", "DRAM Bytes (profiled launches)", True),
        ("l1tex_bytes", "L1/Tex Bytes (profiled launches)", True),
    ]

    md = [
        "## 实验四：NCU 微观 Profiling\n",
        "按分块算法分别对比 GPU kernel 微观指标（fastcdc / gear / gearjump）。\n",
        "- **fastcdc / gear / gearjump**：GPU-Naive vs Ours-Full（各自 naive/ours kernel）\n",
        "两种变体均在同一 **1GB 子集** 上 profile；指标来自 Nsight Compute。\n",
        "Warp Execution Efficiency 使用 `smsp__thread_inst_executed_per_inst_executed.pct`。\n",
    ]

    failed = [r for r in rows if r.get("status") not in ("ok", "", "skipped")]
    if failed:
        md.append("### 运行状态\n")
        md.append("| 数据集 | 算法 | 变体 | 状态 |")
        md.append("| --- | --- | --- | --- |")
        for row in failed:
            md.append(
                f"| {row.get('dataset', '')} | {row.get('algorithm', '')} | "
                f"{row.get('variant', '')} | {row.get('status', '')} |"
            )
        if any(r.get("status") == "ncu_perm_denied" for r in failed):
            md.append(
                "\n> 若状态为 `ncu_perm_denied`，需为当前用户开放 GPU performance counter 权限"
                "（NVIDIA ERR_NVGPUCTRPERM）。\n"
            )
        md.append("")

    chart_metrics = [
        ("warp_exec_eff_pct", "Warp Execution Efficiency (%)"),
        ("dram_throughput_pct", "Global Memory Throughput (% peak)"),
        ("occupancy_pct", "Achieved Occupancy (% peak)"),
    ]

    for algo in algo_order:
        md.append(f"### 算法：{algo}\n")
        algo_variants = variants

        for ds in dataset_order:
            ds_rows = by_algo_ds[algo].get(ds, {})
            if not ds_rows:
                continue
            md.append(f"#### {ds}\n")
            naive = ds_rows.get("GPU-Naive", {})
            ours = ds_rows.get("Ours-Full", {})
            if naive:
                md.append(f"- Naive kernel: `{naive.get('kernel_name', '—')}`")
            md.append(f"- Ours kernel: `{ours.get('kernel_name', '—')}`\n")

            if naive or ours:
                md.append("| 指标 | GPU-Naive | Ours-Full | Ours vs Naive |")
                md.append("| --- | ---: | ---: | ---: |")
                for field, label, higher_better in metric_rows:
                    nval = naive.get(field, "")
                    oval = ours.get(field, "")
                    suffix = "%" if field.endswith("_pct") else ""
                    delta = _metric_delta(nval, oval, higher_is_better=higher_better)
                    md.append(
                        f"| {label} | {_fmt_metric(nval, suffix)} | {_fmt_metric(oval, suffix)} | {delta} |"
                    )
            md.append("")

        for field, chart_title in chart_metrics:
            values = {}
            for ds in dataset_order:
                for variant in algo_variants:
                    row = by_algo_ds[algo].get(ds, {}).get(variant, {})
                    raw = str(row.get(field) or "").strip()
                    if not raw or raw.lower() in ("n/a", "na", "-"):
                        continue
                    try:
                        values[(ds, variant)] = float(raw)
                    except ValueError:
                        continue
            if not values:
                continue
            chart_path = out_dir / f"exp4_{field}_{algo}.svg"
            svg_bar_chart(
                f"Experiment 4 ({algo}): {chart_title}",
                dataset_order,
                algo_variants,
                values,
                chart_title,
                chart_path,
            )
            if algo == "fastcdc" and field == "warp_exec_eff_pct":
                (out_dir / f"exp4_{field}.svg").write_text(
                    chart_path.read_text(encoding="utf-8"), encoding="utf-8"
                )

    (out_dir / "exp4_profiling_report.md").write_text("\n".join(md), encoding="utf-8")


def write_exp4(out_dir: Path):
    if not (out_dir / "exp4_1_pipeline_tasks.csv").exists():
        return
    rows41 = read_csv(out_dir / "exp4_1_pipeline_tasks.csv")
    tasks = []
    vals41 = {}
    for row in rows41:
        label = row.get("config_label", "").replace("pipeline_tasks=", "")
        tasks.append(label)
        vals41[("Sweep", label)] = e2e_gbps(row)
    svg_bar_chart(
        "Experiment 4.1: Pipeline Task Count Sensitivity",
        ["Sweep"],
        tasks,
        vals41,
        "End-to-End Throughput (GB/s)",
        out_dir / "exp4_1_pipeline_tasks.svg",
    )

    rows42 = read_csv(out_dir / "exp4_2_thread_blocks.csv")
    threads = []
    vals42 = {}
    for row in rows42:
        label = row.get("config_label", "").replace("threads=", "")
        threads.append(label)
        ms = float(row.get("actual_elapsed_ms") or 0)
        vals42[("Kernel", label)] = ms
    svg_bar_chart(
        "Experiment 4.2: Kernel Time vs Thread Block Size",
        ["Kernel"],
        threads,
        vals42,
        "Kernel Time (ms)",
        out_dir / "exp4_2_thread_blocks.svg",
    )


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: generate_report.py OUT_DIR", file=sys.stderr)
        return 2
    out_dir = Path(sys.argv[1])
    write_exp1(out_dir)
    write_exp2(out_dir)
    write_exp3(out_dir)
    write_exp4(out_dir)
  # exp5 markdown already generated by summarize_ncu.py
    parts = ["# GPU Chunking Paper Experiments\n"]
    for name in ["exp1_report.md", "exp2_report.md", "exp3_report.md", "exp5_ncu_summary.md"]:
        p = out_dir / name
        if p.exists():
            parts.append(p.read_text(encoding="utf-8"))
            parts.append("")
    (out_dir / "REPORT.md").write_text("\n".join(parts), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
