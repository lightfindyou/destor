#!/usr/bin/env python3
"""Build a byte-capped view of a file or directory for GPU-Naive / NCU runs.

By default uses symlinks into DEST (no data copy). Set SUBSET_MATERIALIZE=copy to
duplicate bytes under DEST (legacy behaviour). Partial tail chunks are written as
a single small file when a source file must be truncated.

DEST should be a temporary directory; experiment scripts remove it on exit.
"""
from __future__ import annotations

import os
import shutil
import sys
from pathlib import Path


def _use_copy() -> bool:
	return os.environ.get("SUBSET_MATERIALIZE", "symlink").strip().lower() in (
		"copy",
		"1",
		"yes",
		"true",
	)


def _link_or_copy(src: Path, dest: Path, *, copy: bool) -> None:
	dest.parent.mkdir(parents=True, exist_ok=True)
	if dest.exists() or dest.is_symlink():
		dest.unlink()
	if copy:
		shutil.copy2(src, dest)
	else:
		os.symlink(src.resolve(), dest)


def prepare(src: Path, dest: Path, cap: int) -> Path:
	copy = _use_copy()
	dest.mkdir(parents=True, exist_ok=True)

	if src.is_file():
		size = src.stat().st_size
		if size <= cap:
			print(src, end="")
			return src
		out = dest / src.name
		with src.open("rb") as fin, out.open("wb") as fout:
			fout.write(fin.read(cap))
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
			_link_or_copy(path, out, copy=copy)
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
