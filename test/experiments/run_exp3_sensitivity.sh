#!/bin/sh
# 实验三：参数敏感性分析 (Parameter Sensitivity)
#
# 使用与实验一相同的数据集与主仓库 chunkingTool（Ours-Full GPU）。
# 对 fastcdc 与 gearjump 扫描 GPU 参数；gear 无 GPU 路径，自动跳过。
#
# 实验 3.1：子批次任务数 (pipeline_tasks) 敏感性
#   X 轴：--gpu-pipeline-tasks（默认 16,32,64,128,256,512）
#   Y 轴：端到端吞吐 elapsed_ms（H2D + Kernel + D2H）
#   固定：--gpu-threads-per-block=$GPU_THREADS（默认 128）
#
# 实验 3.2：线程块规模敏感性
#   X 轴：--gpu-threads-per-block（默认 128,256,512）
#   Y 轴：Kernel 执行时间 actual_elapsed_ms
#   固定：--gpu-pipeline-tasks=$PIPELINE_FULL（默认 256）
#   Segment 窗口由编译期 DEFAULT_BLOCK_SIZE(1MiB)+chunk_max 决定，本实验不单独扫参。
#
# 用法:
#   ./run_exp3_sensitivity.sh
#   OUT_DIR=/tmp/exp3 ./run_exp3_sensitivity.sh
#   SKIP_DATASETS=Wiki ./run_exp3_sensitivity.sh
#   PIPELINE_TASKS_LIST="16 32 64 128 256 512" THREAD_BLOCKS_LIST="128 256 512" ./run_exp3_sensitivity.sh
set -eu

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

color_dataset() {
	case "$1" in
	Wiki) printf '%s' "$C_WIKI" ;;
	Paper) printf '%s' "$C_PAPER" ;;
	LinuxDist) printf '%s' "$C_LINUXDIST" ;;
	GCC) printf '%s' "$C_GCC" ;;
	*) printf '%s' "$C_VAL" ;;
	esac
}

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$SCRIPT_DIR/exp_common.sh"
ROOT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
TEST_DIR="$ROOT_DIR/test"
TOOL="$TEST_DIR/chunkingTool"
OUT_DIR=${OUT_DIR:-"$SCRIPT_DIR/results/exp3_$(date +%Y%m%d_%H%M%S)"}

GPU_DEVICE=${GPU_DEVICE:-0}
AVG_SIZE=${AVG_SIZE:-4096}
MIN_SIZE=${MIN_SIZE:-1024}
MAX_SIZE=${MAX_SIZE:-16384}
MASK_BITS=${MASK_BITS:-12}
WARP_WINDOW=${WARP_WINDOW:-32}
GPU_BATCH=${GPU_BATCH:-8388608}
GPU_THREADS=${GPU_THREADS:-128}
PIPELINE_FULL=${PIPELINE_FULL:-256}
PIPELINE_TASKS_LIST=${PIPELINE_TASKS_LIST:-16 32 64 128 256 512}
THREAD_BLOCKS_LIST=${THREAD_BLOCKS_LIST:-128 256 512}

DATASET_WIKI=${DATASET_WIKI:-/home/xzjin/data/wiki}
DATASET_PAPER=${DATASET_PAPER:-/home/xzjin/data/Paper}
DATASET_GCC=${DATASET_GCC:-/home/xzjin/data/gcc}
DATASET_LINUXDIST=${DATASET_LINUXDIST:-/home/xzjin/data/linuxDist}

COMMON_OPTS="-s $AVG_SIZE --min $MIN_SIZE --max $MAX_SIZE --mask-bits $MASK_BITS --warp-window $WARP_WINDOW"
CSV_HEADER='algorithm,input,mode,files,bytes,cfg_min,cfg_avg,cfg_max,mask_bits,warp_window,chunks,obs_min,obs_max,obs_avg,elapsed_ms,actual_elapsed_ms,actual_throughput_mib_s,fingerprint_updates,cutoff_hits,jump_hits,jump_bytes_skipped,redundant_checks,warp_groups,cutoff_lane_sum,tail_idle_lane_sum,min_checks,max_checks,avg_checks,config_label,dataset,timing,chunk_algo'

mkdir -p "$OUT_DIR"

build_tools() {
	echo "[build] chunkingTool + GPU PTX"
	make -C "$ROOT_DIR/src/chunking" fastcdc_gpu_ptx >/dev/null 2>&1
	(
		cd "$ROOT_DIR/src/chunking"
		for src in rabin_chunking.c rabinjump_chunking.c ae_chunking.c fastcdc_chunking.c \
			gear_common.c gear_chunking.c gearjump_chunking.c fastcdc_gpu.c fastcdc_gpu_naive.c leap_chunking.c; do
			gcc -O3 -Wall $(pkg-config --cflags glib-2.0) -I../../src -c "$src" >/dev/null 2>&1
		done
		ar rcs libchunk.a ./*.o
	)
	make -C "$TEST_DIR" chunkingTool >/dev/null 2>&1
	echo "[build] done"
}

dataset_skipped() {
	ds_name="$1"
	case ",${SKIP_DATASETS:-}," in
	*,$ds_name,*)
		return 0
		;;
	esac
	return 1
}

run_bench_gpu() {
	csv="$1"
	label="$2"
	algo="$3"
	input="$4"
	dataset_name="$5"
	timing="$6"
	shift 6
	extra="$*"

	export DESTOR_FASTCDC_GPU_PTX="$ROOT_DIR/src/chunking/fastcdc_gpu_kernel.ptx"
	ds_color=$(color_dataset "$dataset_name")
	algo_color=$(color_algorithm "$algo")
	printf '[bench] algo=%s%s%s dataset=%s%s%s config=%s%s%s mode=%s%s%s input=%s\n' \
		"$algo_color" "$algo" "$C_ALGO_RST" \
		"$ds_color" "$dataset_name" "$C_RST" \
		"$C_SWEEP" "$label" "$C_RST" \
		"$C_GPU" "GPU" "$C_RST" "$input"

	set -- "$TOOL" -a "$algo" -i "$input" $COMMON_OPTS $extra --result-csv "$csv"
	case "$algo" in
	gearjump|jc)
		set -- "$@" --jump-mto "$JUMP_MASK_DELTA"
		;;
	esac
	set -- "$@" --gpu --gpu-device "$GPU_DEVICE" --gpu-batch "$GPU_BATCH"
	"$@"
	patch_result_row "$csv" "$label" "$dataset_name" "$algo" "$timing"
}

run_exp31_dataset() {
	ds_name="$1"
	input="$2"
	csv="$3"

	if dataset_skipped "$ds_name"; then
		echo "[skip] $ds_name (SKIP_DATASETS)" >&2
		return 0
	fi
	if [ ! -e "$input" ]; then
		echo "[warn] skip $ds_name: path not found: $input" >&2
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
			run_bench_gpu "$csv" "pipeline_tasks=$tasks" "$algo" "$input" "$ds_name" e2e \
				--gpu-pipeline-tasks "$tasks" \
				--gpu-threads-per-block "$GPU_THREADS"
		done
	done
}

run_exp32_dataset() {
	ds_name="$1"
	input="$2"
	csv="$3"

	if dataset_skipped "$ds_name"; then
		return 0
	fi
	if [ ! -e "$input" ]; then
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
			run_bench_gpu "$csv" "threads=$threads" "$algo" "$input" "$ds_name" kernel \
				--gpu-pipeline-tasks "$PIPELINE_FULL" \
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
