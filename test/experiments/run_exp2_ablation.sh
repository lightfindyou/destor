#!/bin/sh
# 实验二：消融分析（仅当前仓库 chunkingTool，统一计时口径）
#
# 用法: sh ./run_exp2_ablation.sh [-h]

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
case "${1:-}" in
-h|--help)
	. "$SCRIPT_DIR/exp_common.sh"
	exp2_usage
	exit 0
	;;
esac

set -u

if [ -t 1 ]; then
	C_RST=$(printf '\033[0m')
	C_GPU=$(printf '\033[1;92m')
	C_CPU=$(printf '\033[1;96m')
	C_KW=$(printf '\033[1;93m')
	C_VAL=$(printf '\033[1;97m')
	C_A=$(printf '\033[1;95m')
	C_B=$(printf '\033[1;96m')
	C_C=$(printf '\033[1;94m')
	C_D=$(printf '\033[1;93m')
	C_WIKI=$(printf '\033[1;36m')
	C_PAPER=$(printf '\033[1;35m')
	C_LINUXDIST=$(printf '\033[1;32m')
	C_GCC=$(printf '\033[1;91m')
else
	C_RST='' C_GPU='' C_CPU='' C_KW='' C_VAL='' C_A='' C_B='' C_C='' C_D=''
	C_WIKI='' C_PAPER='' C_LINUXDIST='' C_GCC=''
fi

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
	A-GPU-Naive) printf '%s' "$C_A" ;;
	B-Segment-Batch) printf '%s' "$C_B" ;;
	C-Pipeline) printf '%s' "$C_C" ;;
	D-Full-ParallelScan|GPU-Ours-Full) printf '%s' "$C_D" ;;
	CPU-Full) printf '%s' "$C_CPU" ;;
	*) printf '%s' "$C_RST" ;;
	esac
}

. "$SCRIPT_DIR/exp_config.sh"
. "$SCRIPT_DIR/exp_common.sh"
. "$SCRIPT_DIR/exp_bench.sh"
exp_init_signals

OUT_DIR=${OUT_DIR:-"$SCRIPT_DIR/results/exp2_$(date +%Y%m%d_%H%M%S)"}
mkdir -p "$OUT_DIR"

run_gpu_ablation() {
	algo="$1"
	ds_name="$2"
	src="$3"
	csv="$4"
	resolve_exp2_naive_input "$ds_name" "$src"
	naive_input="$RESOLVED_DATASET_INPUT"
	resolve_exp2_full_input "$ds_name" "$src"
	full_input="$RESOLVED_DATASET_INPUT"
	naive_gib=$(awk "BEGIN {printf \"%.2f\", ${GPU_NAIVE_BYTE_CAP:-1073741824} / (1024*1024*1024)}")

	ds_color=$(color_dataset "$ds_name")
	echo "[exp2] dataset=${ds_color}${ds_name}${C_RST} A=${naive_gib}GiB B/C/D=full" >&2
	echo "[exp2]   A input=$naive_input" >&2
	echo "[exp2]   B/C/D input=$full_input" >&2

	run_bench_gpu_naive "$csv" "A-GPU-Naive" "$algo" "$src" "$ds_name"
	run_bench_gpu_ours "$csv" "B-Segment-Batch" "$algo" "$full_input" "$ds_name" "$EXP2_B_PIPELINE_TASKS"
	run_bench_gpu_ours "$csv" "C-Pipeline" "$algo" "$full_input" "$ds_name" "$EXP2_C_PIPELINE_TASKS"
	run_bench_gpu_ours "$csv" "D-Full-ParallelScan" "$algo" "$full_input" "$ds_name" "$PIPELINE_FULL"
}

run_dataset() {
	ds_name="$1"
	src="$2"
	csv="$3"

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
			printf '[skip] algorithm=%s%s%s (SKIP_ALGORITHMS)\n' \
				"$(color_algorithm "$algo")" "$algo" "$C_ALGO_RST" >&2
			continue
		fi
		if ! algo_supports_gpu "$algo"; then
			printf '[skip] algorithm=%s%s%s (no GPU ablation)\n' \
				"$(color_algorithm "$algo")" "$algo" "$C_ALGO_RST" >&2
			continue
		fi
		printf '\n--- algorithm: %s%s%s (A→D ablation) ---\n' \
			"$(color_algorithm "$algo")" "$algo" "$C_ALGO_RST"
		run_gpu_ablation "$algo" "$ds_name" "$src" "$csv"
	done
}

echo "============================================================"
printf '实验二：消融分析 (当前 chunkingTool, 统一计时)\n'
printf '  算法: '
print_colored_algorithms "$EXP_ALGORITHMS"
printf '\n'
printf '  A-GPU-Naive: 前 %.2f GiB (GPU_NAIVE_BYTE_CAP=%s)\n' \
	"$(awk "BEGIN {print ${GPU_NAIVE_BYTE_CAP:-1073741824} / (1024*1024*1024)}")" \
	"${GPU_NAIVE_BYTE_CAP:-1073741824}"
printf '  B/C/D: 全量数据集\n'
printf '  A → B pipeline=%s → C pipeline=%s → D pipeline=%s\n' \
	"$EXP2_B_PIPELINE_TASKS" "$EXP2_C_PIPELINE_TASKS" "$PIPELINE_FULL"
printf '  指标: %s\n' "$(timing_metric_ms)"
printf '  输出: %s%s%s\n' "$C_VAL" "$OUT_DIR" "$C_RST"
echo "============================================================"

build_tools

EXP2_CSV="$OUT_DIR/exp2_ablation.csv"
printf '%s\n' "$CSV_HEADER" > "$EXP2_CSV"

run_dataset "Wiki" "$DATASET_WIKI" "$EXP2_CSV"
run_dataset "Paper" "$DATASET_PAPER" "$EXP2_CSV"
run_dataset "LinuxDist" "$DATASET_LINUXDIST" "$EXP2_CSV"
run_dataset "GCC" "$DATASET_GCC" "$EXP2_CSV"

python3 - "$OUT_DIR" <<'PY'
import sys
from pathlib import Path

sys.path.insert(0, str(Path.cwd()))
from generate_report import write_exp2_ablation

out_dir = Path(sys.argv[1])
write_exp2_ablation(out_dir)
print(f"[report] {out_dir / 'exp2_ablation_report.md'}")
for p in sorted(out_dir.glob("exp2_ablation_*.svg")):
    print(f"[chart]  {p}")
if (out_dir / "exp2_ablation.svg").exists():
    print(f"[chart]  {out_dir / 'exp2_ablation.svg'}")
PY

if [ "$BENCH_FAILURES" -gt 0 ]; then
	echo "[warn] $BENCH_FAILURES benchmark(s) failed" >&2
	exit 1
fi
echo "[done] results in $OUT_DIR"
