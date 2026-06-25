#!/usr/bin/env python3
"""Post-process experiment CSVs when config_label patching failed."""
import csv
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent / "results" / "20260622_234014"


def dataset_name(path: str) -> str:
    p = Path(path)
    if "ubuntu" in p.name or p.suffix == ".img":
        return "VM"
    if "gcc-11" in p.name:
        return "FSL"
    if "gcc-4" in p.name or "samba" in p.name:
        return "Silesia"
    return p.name


def fix_exp1(path: Path) -> None:
    rows = []
    with path.open(newline="") as fp:
        reader = csv.DictReader(fp)
        fieldnames = reader.fieldnames
        for row in reader:
            rows.append(row)

    groups = {}
    for row in rows:
        groups.setdefault(row["input"], []).append(row)

    for input_path, group in groups.items():
        ds = dataset_name(input_path)
        labels = ["CPU-Serial", "CPU-Parallel", "GPU-Naive", "Ours-Full"]
        for row, label in zip(group, labels):
            row["config_label"] = label
            row["dataset"] = ds
            row["timing"] = "e2e"

    with path.open("w", newline="") as fp:
        writer = csv.DictWriter(fp, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def fix_exp3(path: Path) -> None:
    labels = [
        "A-GPU-Naive",
        "B-Segment-Batch",
        "C-Pipeline",
        "D-Full-ParallelScan",
    ]
    rows = []
    with path.open(newline="") as fp:
        reader = csv.DictReader(fp)
        fieldnames = reader.fieldnames
        for row in reader:
            rows.append(row)
    for row, label in zip(rows, labels):
        row["config_label"] = label
        row["dataset"] = dataset_name(row["input"])
        row["timing"] = "e2e"
    with path.open("w", newline="") as fp:
        writer = csv.DictWriter(fp, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def main() -> None:
    fix_exp1(ROOT / "exp1_throughput.csv")
    fix_exp3(ROOT / "exp3_ablation.csv")
    print("fixed", ROOT)


if __name__ == "__main__":
    main()
