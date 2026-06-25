#!/usr/bin/env python3
import csv
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 4 or (len(sys.argv) - 2) % 2 != 0:
        print("usage: patch_csv.py CSV key value [key value ...]", file=sys.stderr)
        return 2

    path = Path(sys.argv[1])
    patches = dict(zip(sys.argv[2::2], sys.argv[3::2]))
    rows = []
    with path.open(newline="") as fp:
        reader = csv.DictReader(fp)
        fieldnames = list(reader.fieldnames or [])
        for key in patches:
            if key not in fieldnames:
                fieldnames.append(key)
        for row in reader:
            rows.append(row)

    if rows:
        rows[-1].update(patches)

    with path.open("w", newline="") as fp:
        writer = csv.DictWriter(fp, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
