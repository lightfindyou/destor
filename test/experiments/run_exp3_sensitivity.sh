#!/bin/sh
# 实验三：参数敏感性分析 (Parameter Sensitivity)
#
# 用法: sh ./run_exp3_sensitivity.sh [-h]

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case "${1:-}" in
-h|--help)
	. "$SCRIPT_DIR/exp_common.sh"
	exp3_usage
	exit 0
	;;
esac

set -u

if [ -t 1 ]; then
	C_RST=$(printf '\033[0m')
	C_GPU=$(printf '\033[1;92m')
	C_KW=$(printf '\033[1;93m')
	C_VAL=$(printf '\033[1;97m')
	C_SWEEP=$(printf '\033[1;96m')
	C_WIKI=$(printf '\033[1;36m')
	C_PAPER=$(printf '\033[1;35m')
	C_LINUXDIST=$(printf '\033[1;32m')
	C_GCC=$(printf '\033[1;91m')
else
	C_RST='' C_GPU='' C_KW='' C_VAL='' C_SWEEP=''
	C_WIKI='' C_PAPER='' C_LINUXDIST='' C_GCC=''
fi

color_config() {
	case "$1" in
	pipeline_tasks=*|threads=*)
		printf '%s' "$C_SWEEP"
		;;
	*)
		printf '%s' "$C_RST"
		;;
	esac
}

color_dataset() {
	case "$1" in
	Wiki) printf '%s' "$C_WIKI" ;;
	Paper) printf '%s' "$C_PAPER" ;;
	LinuxDist) printf '%s' "$C_LINUXDIST" ;;
	GCC) printf '%s' "$C_GCC" ;;
	*) printf '%s' "$C_VAL" ;;
	esac
}

color_config() {
	case "$1" in
	pipeline_tasks=*|threads=*)
		printf '%s' "$C_SWEEP"
		;;
	*)
		printf '%s' "$C_RST"
		;;
	esac
}

. "$SCRIPT_DIR/exp_config.sh"
. "$SCRIPT_DIR/exp_common.sh"
. "$SCRIPT_DIR/exp_bench.sh"
exp_init_signals

OUT_DIR=${OUT_DIR:-"$SCRIPT_DIR/results/exp3_$(date +%Y%m%d_%H%M%S)"}
mkdir -p "$OUT_DIR"

run_exp31_dataset() {
	ds_name="$1"
	src="$2"
	csv="$3"
	resolve_dataset_input "$ds_name" "$src"
	input="$RESOLVED_DATASET_INPUT"

	if dataset_skipped "$ds_name"; then
		echo "[skip] $ds_name (SKIP_DATASETS)" >&2
		return 0
	fi
	if [ ! -e "$src" ]; then
		echo "[warn] skip $ds_name: path not found: $src" >&2
		return 0
	fi

	for algo in $EXP_ALGORITHMS; do
		if algorithm_skipped "$algo"; then
			continue
		fi
		if ! algo_supports_gpu_sensitivity "$algo"; then
			printf '[skip] algorithm=%s%s%s (no GPU sensitivity sweep)\n' \
				"$(color_algorithm "$algo")" "$algo" "$C_ALGO_RST" >&2
			continue
		fi
		printf '\n--- algorithm: %s%s%s (3.1 pipeline_tasks) ---\n' \
			"$(color_algorithm "$algo")" "$algo" "$C_ALGO_RST"
		for tasks in $PIPELINE_TASKS_LIST; do
			run_bench_gpu_ours "$csv" "pipeline_tasks=$tasks" "$algo" "$input" "$ds_name" "$tasks"
		done
	done
}

run_exp32_dataset() {
	ds_name="$1"
	src="$2"
	csv="$3"
	resolve_dataset_input "$ds_name" "$src"
	input="$RESOLVED_DATASET_INPUT"

	if dataset_skipped "$ds_name"; then
		return 0
	fi
	if [ ! -e "$src" ]; then
		return 0
	fi

	for algo in $EXP_ALGORITHMS; do
		if algorithm_skipped "$algo"; then
			continue
		fi
		if ! algo_supports_gpu_sensitivity "$algo"; then
			continue
		fi
		printf '\n--- algorithm: %s%s%s (3.2 threads/block) ---\n' \
			"$(color_algorithm "$algo")" "$algo" "$C_ALGO_RST"
		for threads in $THREAD_BLOCKS_LIST; do
			run_bench_gpu_ours "$csv" "threads=$threads" "$algo" "$input" "$ds_name" "$PIPELINE_FULL" \
				--gpu-threads-per-block "$threads"
		done
	done
}

echo "============================================================"
printf '实验三：%s参数敏感性分析%s (Parameter Sensitivity)\n' "$C_KW" "$C_RST"
printf '  算法: '
print_colored_algorithms "$EXP_ALGORITHMS"
printf ' (GPU: fastcdc + gear + gearjump)\n'
printf '  3.1 pipeline_tasks: %s%s%s  (threads/block=%s)\n' \
	"$C_SWEEP" "$PIPELINE_TASKS_LIST" "$C_RST" "$GPU_THREADS"
printf '  3.2 threads/block: %s%s%s  (pipeline_tasks=%s)\n' \
	"$C_SWEEP" "$THREAD_BLOCKS_LIST" "$C_RST" "$PIPELINE_FULL"
printf '  工具: %s%s%s\n' "$C_VAL" "$TOOL" "$C_RST"
printf '  输出: %s%s%s\n' "$C_VAL" "$OUT_DIR" "$C_RST"
echo "============================================================"

build_tools

EXP31_CSV="$OUT_DIR/exp3_pipeline_tasks.csv"
EXP32_CSV="$OUT_DIR/exp3_thread_blocks.csv"
printf '%s\n' "$CSV_HEADER" > "$EXP31_CSV"
printf '%s\n' "$CSV_HEADER" > "$EXP32_CSV"

run_exp31_dataset "Wiki" "$DATASET_WIKI" "$EXP31_CSV"
run_exp31_dataset "Paper" "$DATASET_PAPER" "$EXP31_CSV"
run_exp31_dataset "LinuxDist" "$DATASET_LINUXDIST" "$EXP31_CSV"
run_exp31_dataset "GCC" "$DATASET_GCC" "$EXP31_CSV"

run_exp32_dataset "Wiki" "$DATASET_WIKI" "$EXP32_CSV"
run_exp32_dataset "Paper" "$DATASET_PAPER" "$EXP32_CSV"
run_exp32_dataset "LinuxDist" "$DATASET_LINUXDIST" "$EXP32_CSV"
run_exp32_dataset "GCC" "$DATASET_GCC" "$EXP32_CSV"

python3 - "$OUT_DIR" <<'PY'
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent if "__file__" in dir() else Path.cwd()))
from generate_report import write_exp3_sensitivity

out_dir = Path(sys.argv[1])
write_exp3_sensitivity(out_dir)
print(f"[report] {out_dir / 'exp3_sensitivity_report.md'}")
for p in sorted(out_dir.glob("exp3_pipeline_tasks_*.svg")):
    print(f"[chart]  {p}")
for p in sorted(out_dir.glob("exp3_thread_blocks_*.svg")):
    print(f"[chart]  {p}")
if (out_dir / "exp3_pipeline_tasks.svg").exists():
    print(f"[chart]  {out_dir / 'exp3_pipeline_tasks.svg'}")
if (out_dir / "exp3_thread_blocks.svg").exists():
    print(f"[chart]  {out_dir / 'exp3_thread_blocks.svg'}")
PY

echo "[done] results in $OUT_DIR"
