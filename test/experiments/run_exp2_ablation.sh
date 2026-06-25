#!/bin/sh
# 实验二：消融分析 (Ablation Study)
#
# 目的：量化 Segment-Batch、流水线重叠、块内仿射前缀扫描各自的性能贡献。
#
# 递进配置（同一分块参数，与实验一相同数据集）：
#
# fastcdc（GPU 消融，A→B→C→D）：
#   A  GPU-Naive              仅前 1GB 数据（GPU_NAIVE_BYTE_CAP）
#   B  + Segment-Batch       faad046
#   C  + Pipeline             a109ba0
#   D  + Parallel Scan        主仓库 Ours-Full
#
# fastcdc / gearjump：B/C 使用历史 worktree 二进制（只读 checkout，不修改 worktree 源码）。
# gear：B/C/D 均使用主仓库 chunkingTool（Gear GPU 仅在主仓库实现）。
#
# 指标：elapsed_ms 端到端墙钟时间（H2D + Kernel + D2H）。
# 报告会计算 A→B→C→D 每一步的吞吐提升百分比，以及相对 Naive 的累计提升。
#
# 用法:
#   ./run_exp2_ablation.sh
#   OUT_DIR=/tmp/exp2 ./run_exp2_ablation.sh
#   SKIP_DATASETS=Wiki ./run_exp2_ablation.sh
#   GPU_NAIVE_BYTE_CAP=1073741824 ./run_exp2_ablation.sh
#   GPU_NAIVE_FILE_THREADS=8 ./run_exp2_ablation.sh
set -eu

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

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
. "$SCRIPT_DIR/exp_common.sh"
ROOT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
TEST_DIR="$ROOT_DIR/test"
TOOL="$TEST_DIR/chunkingTool"
ABLATION_DIR="$ROOT_DIR/.worktrees/ablation"
OUT_DIR=${OUT_DIR:-"$SCRIPT_DIR/results/exp2_$(date +%Y%m%d_%H%M%S)"}

GPU_DEVICE=${GPU_DEVICE:-0}
AVG_SIZE=${AVG_SIZE:-4096}
MIN_SIZE=${MIN_SIZE:-1024}
MAX_SIZE=${MAX_SIZE:-16384}
MASK_BITS=${MASK_BITS:-12}
WARP_WINDOW=${WARP_WINDOW:-32}
GPU_BATCH=${GPU_BATCH:-8388608}
GPU_THREADS=${GPU_THREADS:-128}
PIPELINE_FULL=${PIPELINE_FULL:-256}
GPU_NAIVE_BYTE_CAP=${GPU_NAIVE_BYTE_CAP:-1073741824}

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
		echo "[build] ablation tool ($commit)"
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

prepare_naive_input() {
	ds_name="$1"
	src="$2"
	naive_input=$(prepare_capped_input "$ds_name" "$src" "$GPU_NAIVE_BYTE_CAP")
	cap_gib=$(awk "BEGIN {printf \"%.2f\", $GPU_NAIVE_BYTE_CAP / (1024*1024*1024)}")
	ds_color=$(color_dataset "$ds_name")
	echo "[naive] dataset=${ds_color}${ds_name}${C_RST} cap=${cap_gib}GiB input=$naive_input (tmp, auto-clean)" >&2
	printf '%s' "$naive_input"
}

run_bench_cpu() {
	csv="$1"
	label="$2"
	algo="$3"
	bench_input="$4"
	dataset_name="$5"
	shift 5
	extra="$*"

	cfg_color=$(color_config "$label")
	ds_color=$(color_dataset "$dataset_name")
	algo_color=$(color_algorithm "$algo")
	printf '[bench] algo=%s%s%s dataset=%s%s%s config=%s%s%s mode=%s%s%s input=%s\n' \
		"$algo_color" "$algo" "$C_ALGO_RST" \
		"$ds_color" "$dataset_name" "$C_RST" \
		"$cfg_color" "$label" "$C_RST" \
		"$C_CPU" "CPU" "$C_RST" "$bench_input"

	set -- "$TOOL" -a "$algo" -i "$bench_input" $COMMON_OPTS $extra --result-csv "$csv"
	case "$algo" in
	gearjump|jc)
		set -- "$@" --jump-mto "$JUMP_MASK_DELTA"
		;;
	esac
	"$@"
	patch_result_row "$csv" "$label" "$dataset_name" "$algo" e2e
}

run_bench() {
	csv="$1"
	label="$2"
	algo="$3"
	bench_input="$4"
	dataset_name="$5"
	shift 5
	tool="$TOOL"
	if [ $# -gt 0 ] && [ -x "$1" ]; then
		tool="$1"
		shift
	fi
	extra="$*"

	case "$(basename "$tool")" in
	faad046_chunkingTool)
		export DESTOR_FASTCDC_GPU_PTX="$ROOT_DIR/.worktrees/faad046/src/chunking/fastcdc_gpu_kernel.ptx"
		;;
	a109ba0_chunkingTool)
		export DESTOR_FASTCDC_GPU_PTX="$ROOT_DIR/.worktrees/a109ba0/src/chunking/fastcdc_gpu_kernel.ptx"
		;;
	*)
		export DESTOR_FASTCDC_GPU_PTX="$ROOT_DIR/src/chunking/fastcdc_gpu_kernel.ptx"
		;;
	esac

	cfg_color=$(color_config "$label")
	ds_color=$(color_dataset "$dataset_name")
	algo_color=$(color_algorithm "$algo")
	printf '[bench] algo=%s%s%s dataset=%s%s%s config=%s%s%s mode=%s%s%s input=%s\n' \
		"$algo_color" "$algo" "$C_ALGO_RST" \
		"$ds_color" "$dataset_name" "$C_RST" \
		"$cfg_color" "$label" "$C_RST" \
		"$C_GPU" "GPU" "$C_RST" "$bench_input"

	if [ "$label" = "A-GPU-Naive" ]; then
		cap_gib=$(awk "BEGIN {printf \"%.2f\", $GPU_NAIVE_BYTE_CAP / (1024*1024*1024)}")
		echo "[info] A-GPU-Naive uses first ${cap_gib}GiB of dataset (B/C/D use full data)" >&2
	fi

	set -- "$tool" -a "$algo" -i "$bench_input" $COMMON_OPTS $extra --result-csv "$csv"
	case "$algo" in
	gearjump|jc)
		set -- "$@" --jump-mto "$JUMP_MASK_DELTA"
		;;
	esac
	set -- "$@" --gpu-device "$GPU_DEVICE"
	case "$label" in
	A-GPU-Naive)
		set -- "$@" --gpu-naive
		;;
	*)
		set -- "$@" --gpu --gpu-batch "$GPU_BATCH"
		;;
	esac
	if [ "$label" = "D-Full-ParallelScan" ] || [ "$label" = "GPU-Ours-Full" ]; then
		set -- "$@" --gpu-pipeline-tasks "$PIPELINE_FULL" --gpu-threads-per-block "$GPU_THREADS"
	fi
	if [ "$label" = "A-GPU-Naive" ] && [ -n "${GPU_NAIVE_FILE_THREADS:-}" ]; then
		export GPU_NAIVE_FILE_THREADS
	fi
	"$@"
	patch_result_row "$csv" "$label" "$dataset_name" "$algo" e2e
	if [ "$label" = "A-GPU-Naive" ]; then
		python3 "$SCRIPT_DIR/patch_csv.py" "$csv" naive_byte_cap "$GPU_NAIVE_BYTE_CAP"
	fi
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

ablation_tool_for() {
	algo="$1"
	stage="$2"
	case "$algo" in
	gear)
		printf '%s' "$TOOL"
		return 0
		;;
	esac
	case "$stage" in
	B)
		printf '%s' "$ABLATION_DIR/faad046_chunkingTool"
		;;
	C)
		printf '%s' "$ABLATION_DIR/a109ba0_chunkingTool"
		;;
	*)
		printf '%s' "$TOOL"
		;;
	esac
}

run_gpu_ablation() {
	algo="$1"
	ds_name="$2"
	full_input="$3"
	csv="$4"
	naive_input=$(prepare_naive_input "$ds_name" "$full_input")
	b_tool=$(ablation_tool_for "$algo" B)
	c_tool=$(ablation_tool_for "$algo" C)
	run_bench "$csv" "A-GPU-Naive" "$algo" "$naive_input" "$ds_name"
	run_bench "$csv" "B-Segment-Batch" "$algo" "$full_input" "$ds_name" "$b_tool"
	run_bench "$csv" "C-Pipeline" "$algo" "$full_input" "$ds_name" "$c_tool"
	run_bench "$csv" "D-Full-ParallelScan" "$algo" "$full_input" "$ds_name" "$TOOL"
}

run_fastcdc_ablation() {
	run_gpu_ablation fastcdc "$@"
}

run_dataset() {
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
		run_gpu_ablation "$algo" "$ds_name" "$input" "$csv"
	done
}

echo "============================================================"
printf '实验二：%s消融分析%s (Ablation Study)\n' "$C_KW" "$C_RST"
printf '  算法: '
print_colored_algorithms "$EXP_ALGORITHMS"
printf ' (fastcdc/gearjump: B/C=worktree; gear: 全程主仓库)\n'
printf '  fastcdc: %sA%s Naive → %sB%s Seg-Batch → %sC%s Pipeline → %sD%s Parallel Scan\n' \
	"$C_A" "$C_RST" "$C_B" "$C_RST" "$C_C" "$C_RST" "$C_D" "$C_RST"
printf '  A-GPU-Naive: 仅前 %.2f GiB (GPU_NAIVE_BYTE_CAP=%s)\n' \
	"$(awk "BEGIN {print $GPU_NAIVE_BYTE_CAP / (1024*1024*1024)}")" "$GPU_NAIVE_BYTE_CAP"
printf '  指标: %selapsed_ms%s 端到端墙钟 (H2D + Kernel + D2H)\n' "$C_KW" "$C_RST"
printf '  输出: %s%s%s\n' "$C_VAL" "$OUT_DIR" "$C_RST"
echo "============================================================"

build_tools
build_ablation_tool faad046 "$ABLATION_DIR/faad046_chunkingTool"
build_ablation_tool a109ba0 "$ABLATION_DIR/a109ba0_chunkingTool"

EXP2_CSV="$OUT_DIR/exp2_ablation.csv"
printf '%s\n' "$CSV_HEADER" > "$EXP2_CSV"

run_dataset "Wiki" "$DATASET_WIKI" "$EXP2_CSV"
run_dataset "Paper" "$DATASET_PAPER" "$EXP2_CSV"
run_dataset "LinuxDist" "$DATASET_LINUXDIST" "$EXP2_CSV"
run_dataset "GCC" "$DATASET_GCC" "$EXP2_CSV"

python3 - "$OUT_DIR" <<'PY'
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent if "__file__" in dir() else Path.cwd()))
from generate_report import write_exp2_ablation

out_dir = Path(sys.argv[1])
write_exp2_ablation(out_dir)
print(f"[report] {out_dir / 'exp2_ablation_report.md'}")
for p in sorted(out_dir.glob("exp2_ablation_*.svg")):
    print(f"[chart]  {p}")
if (out_dir / "exp2_ablation.svg").exists():
    print(f"[chart]  {out_dir / 'exp2_ablation.svg'}")
if (out_dir / "exp2_ablation_progress.svg").exists():
    print(f"[chart]  {out_dir / 'exp2_ablation_progress.svg'}")
PY

echo "[done] results in $OUT_DIR"
