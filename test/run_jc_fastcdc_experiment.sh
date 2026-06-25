#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOOL="$SCRIPT_DIR/chunkingTool"
INPUT_PATH=${1:-"$SCRIPT_DIR/../README.md"}
OUTPUT_CSV=${2:-"$SCRIPT_DIR/jc_fastcdc_experiment.csv"}
AVG_SIZES=${AVG_SIZES:-"4096 8192"}
MASK_BITS_LIST=${MASK_BITS_LIST:-"11 12 13"}
WARP_WINDOWS=${WARP_WINDOWS:-"32"}
GPU_MODE=${GPU_MODE:-0}
GPU_PIPELINE_TASKS=${GPU_PIPELINE_TASKS:-256}
JC_JUMP_MTO=${JC_JUMP_MTO:-1}
ALGORITHMS=${ALGORITHMS:-"baseline fastcdc jc"}

if [ ! -x "$TOOL" ]; then
	make -C "$SCRIPT_DIR" chunkingTool
fi

if [ "$GPU_MODE" = "1" ]; then
	make -C "$SCRIPT_DIR/../src/chunking" fastcdc_gpu_ptx
fi

rm -f "$OUTPUT_CSV"

for avg in $AVG_SIZES; do
	min=$((avg / 4))
	max=$((avg * 4))
	for mask_bits in $MASK_BITS_LIST; do
		for warp_window in $WARP_WINDOWS; do
			for algorithm in $ALGORITHMS; do
				set -- "$TOOL" -a "$algorithm" -i "$INPUT_PATH" \
					-s "$avg" --min "$min" --max "$max" \
					--mask-bits "$mask_bits" --warp-window "$warp_window" \
					--profile-chunking --result-csv "$OUTPUT_CSV"
				if [ "$algorithm" = "jc" ]; then
					set -- "$@" --jump-mto "$JC_JUMP_MTO"
				fi
				if [ "$GPU_MODE" = "1" ]; then
					set -- "$@" --gpu --gpu-pipeline-tasks "$GPU_PIPELINE_TASKS"
				fi
				echo "[experiment] algo=$algorithm avg=$avg mask_bits=$mask_bits warp_window=$warp_window gpu=$GPU_MODE"
				"$@"
			done
		done
	done
done

echo "[experiment] wrote results to $OUTPUT_CSV"