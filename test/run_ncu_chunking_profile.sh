#!/bin/sh

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TOOL="$SCRIPT_DIR/chunkingTool"
INPUT_PATH=${1:-"$SCRIPT_DIR/../README.md"}
OUTPUT_DIR=${2:-"$SCRIPT_DIR/ncu-results"}
ALGORITHMS=${ALGORITHMS:-"fastcdc jc"}
AVG_SIZES=${AVG_SIZES:-"4096"}
MASK_BITS_LIST=${MASK_BITS_LIST:-"12"}
WARP_WINDOWS=${WARP_WINDOWS:-"32"}
JC_JUMP_MTO=${JC_JUMP_MTO:-1}
GPU_DEVICE=${GPU_DEVICE:-0}
GPU_BATCH=${GPU_BATCH:-8388608}
GPU_PIPELINE_TASKS=${GPU_PIPELINE_TASKS:-256}
NCU_BIN=${NCU_BIN:-ncu}
NCU_METRICS=${NCU_METRICS:-"sm__sass_average_branch_targets_threads_per_instruction.pct"}

if ! command -v "$NCU_BIN" >/dev/null 2>&1; then
	echo "ncu not found: $NCU_BIN" >&2
	exit 2
fi

if [ ! -x "$TOOL" ]; then
	make -C "$SCRIPT_DIR" chunkingTool
fi

make -C "$SCRIPT_DIR/../src/chunking" fastcdc_gpu_ptx
mkdir -p "$OUTPUT_DIR"

SUMMARY_CSV="$OUTPUT_DIR/ncu_summary.csv"
rm -f "$SUMMARY_CSV"
printf 'algorithm,input,avg_size,mask_bits,warp_window,jump_mto,status,ncu_exit,experiment_csv,ncu_report,ncu_csv,ncu_log\n' > "$SUMMARY_CSV"

for avg in $AVG_SIZES; do
	min=$((avg / 4))
	max=$((avg * 4))
	for mask_bits in $MASK_BITS_LIST; do
		for warp_window in $WARP_WINDOWS; do
			for algorithm in $ALGORITHMS; do
				tag="${algorithm}_avg${avg}_mask${mask_bits}_warp${warp_window}"
				experiment_csv="$OUTPUT_DIR/${tag}_experiment.csv"
				ncu_report="$OUTPUT_DIR/${tag}.ncu-rep"
				ncu_csv="$OUTPUT_DIR/${tag}_ncu.csv"
				ncu_log="$OUTPUT_DIR/${tag}_ncu.log"
				status="ok"
				ncu_exit=0
				rm -f "$experiment_csv" "$ncu_report" "$ncu_csv" "$ncu_log"

				set -- "$TOOL" -a "$algorithm" -i "$INPUT_PATH" \
					-s "$avg" --min "$min" --max "$max" \
					--mask-bits "$mask_bits" --warp-window "$warp_window" \
					--gpu --gpu-device "$GPU_DEVICE" --gpu-batch "$GPU_BATCH" \
					--gpu-pipeline-tasks "$GPU_PIPELINE_TASKS" \
					--profile-chunking --result-csv "$experiment_csv"
				if [ "$algorithm" = "jc" ]; then
					set -- "$@" --jump-mto "$JC_JUMP_MTO"
				fi

				echo "[ncu] algo=$algorithm avg=$avg mask_bits=$mask_bits warp_window=$warp_window"
				set +e
				"$NCU_BIN" \
					--target-processes all \
					--metrics "$NCU_METRICS" \
					--export "$ncu_report" \
					"$@" > "$ncu_log" 2>&1
				ncu_exit=$?
				set -e
				if [ "$ncu_exit" -ne 0 ]; then
					status="ncu_failed"
				fi
				if [ -f "$ncu_report" ]; then
					if ! "$NCU_BIN" --import "$ncu_report" --csv > "$ncu_csv" 2>> "$ncu_log"; then
						status="import_failed"
					fi
				else
					status="no_report"
				fi
				printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
					"$algorithm" \
					"$INPUT_PATH" \
					"$avg" \
					"$mask_bits" \
					"$warp_window" \
					"$JC_JUMP_MTO" \
					"$status" \
					"$ncu_exit" \
					"$experiment_csv" \
					"$ncu_report" \
					"$ncu_csv" \
					"$ncu_log" >> "$SUMMARY_CSV"
			done
		done
	done
done

echo "[ncu] wrote summary to $SUMMARY_CSV"