#!/bin/sh
# 实验一：吞吐对比（统一计时口径，见 EXP_TIMING_MODE）
#
# 默认 EXP_TIMING_MODE=kernel：elapsed_ms = actual_elapsed_ms = 纯分块时间（不含读盘/H2D/D2H）
#
# 所有配置使用相同数据集视图（EXP_INPUT_BYTE_CAP 为空则全量）。
# 仅使用主仓库 chunkingTool，不依赖 worktree。
#
# 用法:
#   ./run_exp1_throughput.sh
#   EXP_TIMING_MODE=e2e OUT_DIR=/tmp/exp1 ./run_exp1_throughput.sh
#   EXP_INPUT_BYTE_CAP=1073741824 ./run_exp1_throughput.sh
set -u

if [ -t 1 ]; then
	C_RST=$(printf '\033[0m')
	C_GPU=$(printf '\033[1;92m')
	C_CPU=$(printf '\033[1;96m')
	C_NAIVE=$(printf '\033[1;95m')
	C_OURS=$(printf '\033[1;93m')
	C_PARALLEL=$(printf '\033[1;94m')
	C_SERIAL=$(printf '\033[1;34m')
	C_KW=$(printf '\033[1;93m')
	C_VAL=$(printf '\033[1;97m')
	C_WIKI=$(printf '\033[1;36m')
	C_PAPER=$(printf '\033[1;35m')
	C_LINUXDIST=$(printf '\033[1;32m')
	C_GCC=$(printf '\033[1;91m')
else
	C_RST='' C_GPU='' C_CPU='' C_NAIVE='' C_OURS='' C_PARALLEL='' C_SERIAL='' C_KW='' C_VAL=''
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
	GPU-Naive) printf '%s' "$C_NAIVE" ;;
	Ours-Full) printf '%s' "$C_OURS" ;;
	CPU-Parallel) printf '%s' "$C_PARALLEL" ;;
	CPU-Serial) printf '%s' "$C_SERIAL" ;;
	*) printf '%s' "$C_RST" ;;
	esac
}

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$SCRIPT_DIR/exp_config.sh"
. "$SCRIPT_DIR/exp_common.sh"
. "$SCRIPT_DIR/exp_bench.sh"

OUT_DIR=${OUT_DIR:-"$SCRIPT_DIR/results/exp1_$(date +%Y%m%d_%H%M%S)"}
mkdir -p "$OUT_DIR"

run_dataset() {
	ds_name="$1"
	src="$2"
	csv="$3"
	input=$(resolve_dataset_input "$ds_name" "$src")

	if dataset_skipped "$ds_name"; then
		echo "[skip] $ds_name (SKIP_DATASETS)" >&2
		return 0
	fi
	if [ ! -e "$src" ]; then
		echo "[warn] skip $ds_name: path not found: $src" >&2
		return 0
	fi
	if [ -n "${EXP_INPUT_BYTE_CAP:-}" ]; then
		cap_gib=$(awk "BEGIN {printf \"%.2f\", $EXP_INPUT_BYTE_CAP / (1024*1024*1024)}")
		echo "[input] $ds_name capped to ${cap_gib}GiB -> $input" >&2
	fi

	for algo in $EXP_ALGORITHMS; do
		if algorithm_skipped "$algo"; then
			printf '[skip] algorithm=%s%s%s (SKIP_ALGORITHMS)\n' \
				"$(color_algorithm "$algo")" "$algo" "$C_ALGO_RST" >&2
			continue
		fi
		printf '\n--- algorithm: %s%s%s ---\n' \
			"$(color_algorithm "$algo")" "$algo" "$C_ALGO_RST"
		run_bench "$csv" "CPU-Serial" "$algo" cpu "$input" "$ds_name"
		run_bench "$csv" "CPU-Parallel" "$algo" cpu "$input" "$ds_name" --cpu-parallel
		if algo_supports_gpu "$algo"; then
			run_bench_gpu_ours "$csv" "Ours-Full" "$algo" "$input" "$ds_name" "$PIPELINE_FULL"
		fi
		if algo_supports_gpu_naive "$algo" && [ "${RUN_GPU_NAIVE:-0}" = "1" ]; then
			run_bench "$csv" "GPU-Naive" "$algo" gpu "$input" "$ds_name"
		fi
	done
}

echo "============================================================"
printf '实验一：吞吐对比 (timing=%s)\n' "${EXP_TIMING_MODE:-kernel}"
printf '  算法: '
print_colored_algorithms "$EXP_ALGORITHMS"
printf '\n'
printf '  指标字段: %s\n' "$(timing_metric_ms)"
if [ -n "${EXP_INPUT_BYTE_CAP:-}" ]; then
	printf '  数据上限: %s bytes (全部配置一致)\n' "$EXP_INPUT_BYTE_CAP"
else
	printf '  数据上限: 全量数据集\n'
fi
printf '  输出: %s%s%s\n' "$C_VAL" "$OUT_DIR" "$C_RST"
echo "============================================================"

build_tools

EXP1_CSV="$OUT_DIR/exp1_throughput.csv"
printf '%s\n' "$CSV_HEADER" > "$EXP1_CSV"

run_dataset "Wiki" "$DATASET_WIKI" "$EXP1_CSV"
run_dataset "Paper" "$DATASET_PAPER" "$EXP1_CSV"
run_dataset "LinuxDist" "$DATASET_LINUXDIST" "$EXP1_CSV"
run_dataset "GCC" "$DATASET_GCC" "$EXP1_CSV"

python3 - "$OUT_DIR" <<'PY'
import sys
from pathlib import Path

sys.path.insert(0, str(Path.cwd()))
from generate_report import write_exp1

out_dir = Path(sys.argv[1])
write_exp1(out_dir)
print(f"[report] {out_dir / 'exp1_report.md'}")
for p in sorted(out_dir.glob("exp1_throughput_*.svg")):
    print(f"[chart]  {p}")
if (out_dir / "exp1_throughput.svg").exists():
    print(f"[chart]  {out_dir / 'exp1_throughput.svg'}")
PY

if [ "$BENCH_FAILURES" -gt 0 ]; then
	echo "[warn] $BENCH_FAILURES benchmark(s) failed" >&2
	exit 1
fi
echo "[done] results in $OUT_DIR"
