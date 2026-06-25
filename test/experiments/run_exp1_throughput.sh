#!/bin/sh
# 实验一：总体端到端吞吐对比 (End-to-End Throughput)
#
# 端到端吞吐使用 chunkingTool 输出的 elapsed_ms（墙钟时间），覆盖完整 GPU 分块路径：
#   PCIe Host-to-Device 拷贝 + Kernel 执行 + Device-to-Host 结果回收
# CPU 基线同样使用墙钟时间，包含读入与分块全过程。
#
# 每个数据集 × 每种分块算法（fastcdc / gear / gearjump，EXP_ALGORITHMS 可覆盖）：
#   CPU-Serial   CPU 单线程
#   CPU-Parallel CPU + --cpu-parallel（默认 8 线程，CPU_PARALLEL_FILE_THREADS 可覆盖）
#   GPU-Naive    仅 fastcdc + --gpu-naive（可选，默认关闭）
#   Ours-Full    fastcdc / gear / gearjump + GPU 完整框架
#   GPU-Naive    三种算法均支持（RUN_GPU_NAIVE=1 时启用）
#   gearjump 自动附加 --jump-mto（JUMP_MASK_DELTA，默认 1）
#
# 用法:
#   ./run_exp1_throughput.sh
#   OUT_DIR=/tmp/exp1 ./run_exp1_throughput.sh
#   DATASET_GCC=/path/to/gcc ./run_exp1_throughput.sh
set -eu

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
. "$SCRIPT_DIR/exp_common.sh"
ROOT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
TEST_DIR="$ROOT_DIR/test"
TOOL="$TEST_DIR/chunkingTool"
ABLATION_DIR="$ROOT_DIR/.worktrees/ablation"
OUT_DIR=${OUT_DIR:-"$SCRIPT_DIR/results/exp1_$(date +%Y%m%d_%H%M%S)"}

GPU_DEVICE=${GPU_DEVICE:-0}
AVG_SIZE=${AVG_SIZE:-4096}
MIN_SIZE=${MIN_SIZE:-1024}
MAX_SIZE=${MAX_SIZE:-16384}
MASK_BITS=${MASK_BITS:-12}
WARP_WINDOW=${WARP_WINDOW:-32}
GPU_BATCH=${GPU_BATCH:-8388608}
GPU_THREADS=${GPU_THREADS:-128}
CPU_PARALLEL_FILE_THREADS=${CPU_PARALLEL_FILE_THREADS:-8}
PIPELINE_FULL=${PIPELINE_FULL:-256}

DATASET_WIKI=${DATASET_WIKI:-/home/xzjin/data/wiki}
DATASET_PAPER=${DATASET_PAPER:-/home/xzjin/data/Paper}
DATASET_GCC=${DATASET_GCC:-/home/xzjin/data/gcc}
DATASET_LINUXDIST=${DATASET_LINUXDIST:-/home/xzjin/data/linuxDist}

COMMON_OPTS="-s $AVG_SIZE --min $MIN_SIZE --max $MAX_SIZE --mask-bits $MASK_BITS --warp-window $WARP_WINDOW"
CSV_HEADER='algorithm,input,mode,files,bytes,cfg_min,cfg_avg,cfg_max,mask_bits,warp_window,chunks,obs_min,obs_max,obs_avg,elapsed_ms,actual_elapsed_ms,actual_throughput_mib_s,fingerprint_updates,cutoff_hits,jump_hits,jump_bytes_skipped,redundant_checks,warp_groups,cutoff_lane_sum,tail_idle_lane_sum,min_checks,max_checks,avg_checks,config_label,dataset,timing,chunk_algo'

mkdir -p "$OUT_DIR" "$ABLATION_DIR"

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

build_libchunk_manual() {
	chunk_dir="$1/src/chunking"
	(
		cd "$chunk_dir"
		rm -f ./*.o libchunk.a 2>/dev/null || true
		for src in rabin_chunking.c rabinjump_chunking.c ae_chunking.c fastcdc_chunking.c \
			gear_common.c gear_chunking.c gearjump_chunking.c fastcdc_gpu.c leap_chunking.c; do
			if [ -f "$src" ]; then
				gcc -O3 -Wall $(pkg-config --cflags glib-2.0) -I../../src -c "$src"
			fi
		done
		ar rcs libchunk.a ./*.o
		nvcc -ptx -o fastcdc_gpu_kernel.ptx fastcdc_gpu_kernel.cu
	)
}

build_ablation_tool() {
	commit="$1"
	out="$2"
	worktree="$ROOT_DIR/.worktrees/$commit"
	src="$worktree/test/chunkingTool.c"
	if [ "${REBUILD_ABLATION:-0}" = "1" ] || [ ! -x "$out" ] || [ "$src" -nt "$out" ]; then
		echo "[build] GPU-Naive ablation tool ($commit)"
		mkdir -p "$(dirname "$out")"
		if [ ! -d "$worktree" ]; then
			git -C "$ROOT_DIR" worktree add "$worktree" "$commit" >/dev/null
		fi
		if ! build_libchunk_manual "$worktree" >/dev/null 2>&1; then
			echo "[error] failed to build libchunk for $commit" >&2
			return 1
		fi
		python3 "$SCRIPT_DIR/patch_chunking_tool_largefile.py" "$worktree/test/chunkingTool.c" >/dev/null
		if ! gcc -O3 -Wall $(pkg-config --cflags glib-2.0) \
			-o "$worktree/test/chunkingTool" "$worktree/test/chunkingTool.c" \
			"$worktree/src/chunking/libchunk.a" -lm -lcrypto -lssl \
			$(pkg-config --libs glib-2.0) -ldl -pthread >/dev/null 2>&1; then
			echo "[error] failed to link chunkingTool for $commit" >&2
			return 1
		fi
		cp "$worktree/test/chunkingTool" "$out"
		echo "[build] ablation tool ready: $out"
	fi
}

run_bench() {
	csv="$1"
	label="$2"
	algo="$3"
	gpu_flag="$4"
	input="$5"
	dataset_name="$6"
	shift 6
	tool="$TOOL"
	if [ $# -gt 0 ] && [ -x "$1" ]; then
		tool="$1"
		shift
	fi
	extra="$*"

	case "$(basename "$tool")" in
	ee34179_chunkingTool)
		export DESTOR_FASTCDC_GPU_PTX="$ROOT_DIR/.worktrees/ee34179/src/chunking/fastcdc_gpu_kernel.ptx"
		;;
	*)
		export DESTOR_FASTCDC_GPU_PTX="$ROOT_DIR/src/chunking/fastcdc_gpu_kernel.ptx"
		;;
	esac

	cfg_color=$(color_config "$label")
	ds_color=$(color_dataset "$dataset_name")
	algo_color=$(color_algorithm "$algo")
	if [ "$gpu_flag" = "gpu" ]; then
		mode_color=$C_GPU
		mode_name=GPU
	else
		mode_color=$C_CPU
		mode_name=CPU
	fi
	printf '[bench] algo=%s%s%s dataset=%s%s%s config=%s%s%s mode=%s%s%s input=%s\n' \
		"$algo_color" "$algo" "$C_ALGO_RST" \
		"$ds_color" "$dataset_name" "$C_RST" \
		"$cfg_color" "$label" "$C_RST" \
		"$mode_color" "$mode_name" "$C_RST" "$input"
	if [ "$label" = "GPU-Naive" ]; then
		if [ -f "$input" ]; then
			file_count=1
		else
			file_count=$(find "$input" -type f 2>/dev/null | wc -l)
		fi
		if [ "$file_count" -gt 500 ]; then
			echo "[warn] $dataset_name has $file_count files; GPU-Naive uses per-chunk GPU launches and may take many hours" >&2
		fi
	fi
	set -- "$tool" -a "$algo" -i "$input" $COMMON_OPTS $extra --result-csv "$csv"
	case "$algo" in
	gearjump|jc)
		set -- "$@" --jump-mto "$JUMP_MASK_DELTA"
		;;
	esac
	if [ "$gpu_flag" = "gpu" ]; then
		set -- "$@" --gpu-device "$GPU_DEVICE"
		if [ "$label" = "GPU-Naive" ]; then
			:
		else
			set -- "$@" --gpu --gpu-batch "$GPU_BATCH"
		fi
	fi
	if [ "$label" = "CPU-Parallel" ]; then
		export CPU_PARALLEL_FILE_THREADS
	fi
	if [ "$label" = "GPU-Naive" ] && [ -n "${GPU_NAIVE_FILE_THREADS:-}" ]; then
		export GPU_NAIVE_FILE_THREADS
	fi
	"$@"
	patch_result_row "$csv" "$label" "$dataset_name" "$algo" e2e
}

run_dataset() {
	ds_name="$1"
	input="$2"
	csv="$3"

	if [ ! -e "$input" ]; then
		echo "[warn] skip $ds_name: path not found: $input" >&2
		return 0
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
			run_bench "$csv" "Ours-Full" "$algo" gpu "$input" "$ds_name" "$TOOL" \
				--gpu-pipeline-tasks "$PIPELINE_FULL" \
				--gpu-threads-per-block "$GPU_THREADS" \
				--profile-chunking
		fi
		if algo_supports_gpu_naive "$algo" && [ "${RUN_GPU_NAIVE:-0}" = "1" ]; then
			run_bench "$csv" "GPU-Naive" "$algo" gpu "$input" "$ds_name" --gpu-naive
		fi
	done
}

echo "============================================================"
printf '实验一：%s端到端吞吐%s对比 (End-to-End Throughput)\n' "$C_KW" "$C_RST"
printf '  算法: '
print_colored_algorithms "$EXP_ALGORITHMS"
printf '\n'
printf '  指标: %selapsed_ms%s 墙钟时间 = H2D + Kernel + D2H (%sGPU%s)\n' \
	"$C_KW" "$C_RST" "$C_GPU" "$C_RST"
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

sys.path.insert(0, str(Path(__file__).resolve().parent if "__file__" in dir() else Path.cwd()))
from generate_report import write_exp1

out_dir = Path(sys.argv[1])
write_exp1(out_dir)
print(f"[report] {out_dir / 'exp1_report.md'}")
for p in sorted(out_dir.glob("exp1_throughput_*.svg")):
    print(f"[chart]  {p}")
if (out_dir / "exp1_throughput.svg").exists():
    print(f"[chart]  {out_dir / 'exp1_throughput.svg'}")
PY

echo "[done] results in $OUT_DIR"
