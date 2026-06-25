#!/usr/bin/env python3
"""Materialize up to cap bytes from a file or directory for GPU-Naive benchmarks."""
import shutil
import sys
from pathlib import Path


def prepare(src: Path, dest: Path, cap: int) -> Path:
    dest.mkdir(parents=True, exist_ok=True)
    marker = dest / ".source"
    marker_text = f"{src.resolve()}\n{cap}\n"
    if marker.exists() and marker.read_text(encoding="utf-8") == marker_text:
        existing = [p for p in dest.iterdir() if p.name != ".source"]
        if existing:
            result = existing[0] if src.is_file() else dest
            print(result, end="")
            return result

    for child in dest.iterdir():
        if child.name == ".source":
            continue
        if child.is_dir():
            shutil.rmtree(child)
        else:
            child.unlink()

    if src.is_file():
        size = src.stat().st_size
        if size <= cap:
            marker.write_text(marker_text, encoding="utf-8")
            print(src, end="")
            return src
        out = dest / src.name
        with src.open("rb") as fin, out.open("wb") as fout:
            fout.write(fin.read(cap))
        marker.write_text(marker_text, encoding="utf-8")
        print(out, end="")
        return out

    if not src.is_dir():
        raise SystemExit(f"input not found: {src}")

    total = 0
    files = sorted(p for p in src.rglob("*") if p.is_file())
    for path in files:
        size = path.stat().st_size
        rel = path.relative_to(src)
        out = dest / rel
        if total + size <= cap:
            out.parent.mkdir(parents=True, exist_ok=True)
            if not out.exists():
                shutil.copy2(path, out)
            total += size
            if total >= cap:
                break
            continue

        remain = cap - total
        if remain <= 0:
            break
        out.parent.mkdir(parents=True, exist_ok=True)
        with path.open("rb") as fin, out.open("wb") as fout:
            fout.write(fin.read(remain))
        total += remain
        break

    if total == 0:
        raise SystemExit(f"no files under {src}")

    marker.write_text(marker_text, encoding="utf-8")
    print(dest, end="")
    return dest


def main() -> int:
    if len(sys.argv) != 4:
        print("usage: prepare_naive_subset.py SRC DEST CAP_BYTES", file=sys.stderr)
        return 2
    prepare(Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
