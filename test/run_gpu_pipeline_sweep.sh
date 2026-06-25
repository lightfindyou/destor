#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOOL="$SCRIPT_DIR/chunkingTool"
INPUT_PATH=${1:-"$SCRIPT_DIR/../README.md"}
OUTPUT_CSV=${2:-"$SCRIPT_DIR/gpu_pipeline_sweep.csv"}
PIPELINE_TASKS_LIST=${PIPELINE_TASKS_LIST:-"128 256 512 1024"}
ALGORITHMS=${ALGORITHMS:-"fastcdc jc"}
AVG_SIZE=${AVG_SIZE:-4096}
MIN_SIZE=${MIN_SIZE:-$((AVG_SIZE / 4))}
MAX_SIZE=${MAX_SIZE:-$((AVG_SIZE * 4))}
GPU_DEVICE=${GPU_DEVICE:-0}
GPU_BATCH=${GPU_BATCH:-8388608}
JC_JUMP_MTO=${JC_JUMP_MTO:-1}

if [ ! -x "$TOOL" ]; then
	make -C "$SCRIPT_DIR" chunkingTool
fi

rm -f "$OUTPUT_CSV"

for tasks in $PIPELINE_TASKS_LIST; do
	for algo in $ALGORITHMS; do
		set -- "$TOOL" -a "$algo" -i "$INPUT_PATH" \
			-s "$AVG_SIZE" --min "$MIN_SIZE" --max "$MAX_SIZE" \
			--gpu --gpu-device "$GPU_DEVICE" --gpu-batch "$GPU_BATCH" \
			--gpu-pipeline-tasks "$tasks" --result-csv "$OUTPUT_CSV"
		if [ "$algo" = "jc" ]; then
			set -- "$@" --jump-mto "$JC_JUMP_MTO"
		fi
		echo "[pipeline-sweep] algo=$algo tasks=$tasks input=$INPUT_PATH"
		"$@"
	done
done

echo "[pipeline-sweep] wrote results to $OUTPUT_CSV"
