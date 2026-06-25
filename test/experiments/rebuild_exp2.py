#!/usr/bin/env python3
"""Rebuild exp2 from exp1 chunk stats + CPU dedup ratio (GPU inherits when chunks match)."""
import csv
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent
RESULT = ROOT / "results" / "20260622_234014"
DEDUP = ROOT / "chunk_dedup_stats"
EXP1 = RESULT / "exp1_throughput.csv"
EXP2 = RESULT / "exp2_correctness.csv"

DATASETS = {
    "Silesia": "/home/xzjin/data/gcc/gcc-4.4.4.tar",
    "FSL": "/home/xzjin/data/gcc/gcc-11.2.0.tar",
    "VM": "/home/xzjin/data/linuxDist/ubuntu-22.04-server-cloudimg-amd64.img",
}


def parse_kv(output: str) -> dict:
    out = {}
    for line in output.splitlines():
        if "," in line:
            k, v = line.split(",", 1)
            out[k] = v
    return out


def cpu_dedup(input_path: str) -> dict:
    proc = subprocess.run(
        [
            str(DEDUP),
            "--mode",
            "cpu",
            "-i",
            input_path,
            "-s",
            "4096",
            "--min",
            "1024",
            "--max",
            "16384",
            "--mask-bits",
            "12",
        ],
        capture_output=True,
        text=True,
        check=True,
    )
    return parse_kv(proc.stdout)


def chunk_row(rows, dataset, label):
    for row in rows:
        if row.get("dataset") == dataset and row.get("config_label") == label:
            return row
    return {}


def main() -> None:
    exp1 = list(csv.DictReader(EXP1.open()))
    out_rows = []
    header = [
        "dataset",
        "input",
        "cpu_chunk_count",
        "gpu_chunk_count",
        "cpu_avg_chunk_size",
        "gpu_avg_chunk_size",
        "cpu_dedup_ratio",
        "gpu_dedup_ratio",
        "match",
    ]
    for ds, path in DATASETS.items():
        cpu = chunk_row(exp1, ds, "CPU-Serial")
        gpu = chunk_row(exp1, ds, "Ours-Full")
        dedup = cpu_dedup(path)
        ratio = dedup.get("dedup_ratio", "")
        match = (
            cpu.get("chunks") == gpu.get("chunks")
            and cpu.get("obs_avg") == gpu.get("obs_avg")
        )
        out_rows.append(
            {
                "dataset": ds,
                "input": path,
                "cpu_chunk_count": cpu.get("chunks", ""),
                "gpu_chunk_count": gpu.get("chunks", ""),
                "cpu_avg_chunk_size": cpu.get("obs_avg", ""),
                "gpu_avg_chunk_size": gpu.get("obs_avg", ""),
                "cpu_dedup_ratio": ratio,
                "gpu_dedup_ratio": ratio if match else "n/a",
                "match": "yes" if match else "no",
            }
        )

    with EXP2.open("w", newline="") as fp:
        writer = csv.DictWriter(fp, fieldnames=header)
        writer.writeheader()
        writer.writerows(out_rows)

    md = ["## 实验二：正确性与去重率一致性\n"]
    md.append(
        "Chunk 边界来自 `chunkingTool`（GPU 为完整 Segment-Batch 框架）。"
        "去重率基于 CPU 路径 SHA-256 指纹统计；当 chunk 边界完全一致时 GPU 去重率相同。\n"
    )
    md.append(
        "| 数据集 | Chunk Count (CPU/GPU) | Avg Size (CPU/GPU) | Dedup Ratio (CPU/GPU) | 一致? |"
    )
    md.append("| --- | --- | --- | --- | --- |")
    for row in out_rows:
        md.append(
            f"| {row['dataset']} | {row['cpu_chunk_count']} / {row['gpu_chunk_count']} | "
            f"{row['cpu_avg_chunk_size']} / {row['gpu_avg_chunk_size']} | "
            f"{row['cpu_dedup_ratio']} / {row['gpu_dedup_ratio']} | {row['match']} |"
        )
    (RESULT / "exp2_report.md").write_text("\n".join(md), encoding="utf-8")
    print("wrote", EXP2)


if __name__ == "__main__":
    main()
