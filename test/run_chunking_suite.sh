#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOOL="$SCRIPT_DIR/chunkingTool"
INPUT_PATH=${1:-"$SCRIPT_DIR/../README.md"}
AVG_SIZE=${AVG_SIZE:-4096}
MIN_SIZE=${MIN_SIZE:-$((AVG_SIZE / 4))}
MAX_SIZE=${MAX_SIZE:-$((AVG_SIZE * 4))}
CPU_ALGOS=${CPU_ALGOS:-"rabin,normalized-rabin,rabin-jump,tttd,ae,sc,fastcdc,gear,jc,jctttd,normalized-gearjump,tttdgear,leap"}

if [ ! -x "$TOOL" ]; then
	make -C "$SCRIPT_DIR" chunkingTool
fi

echo "[cpu-suite] input=$INPUT_PATH avg=$AVG_SIZE min=$MIN_SIZE max=$MAX_SIZE"
"$TOOL" --batch "$CPU_ALGOS" -i "$INPUT_PATH" -s "$AVG_SIZE" --min "$MIN_SIZE" --max "$MAX_SIZE"

if [ "${RUN_GPU:-0}" = "1" ]; then
	echo
	echo "[gpu-suite] fastcdc + jc"
	"$TOOL" --batch "fastcdc,jc" -i "$INPUT_PATH" -s "$AVG_SIZE" --min "$MIN_SIZE" --max "$MAX_SIZE" --gpu
fi