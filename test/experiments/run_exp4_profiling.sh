#!/bin/sh
# 实验四：NCU 微观 Profiling（GPU-Naive vs Ours-Full）
#
# 按分块算法分别 profile（EXP_ALGORITHMS，默认 fastcdc + gearjump；gear 无 GPU 跳过）：
#   fastcdc   GPU-Naive vs Ours-Full
#   gear      GPU-Naive vs Ours-Full
#   gearjump  GPU-Naive vs Ours-Full
#
# 指标（由 summarize_ncu.py 从 NCU 报告提取）：
#   Warp Execution Efficiency、分支发散、Global/Shared 访存吞吐、Achieved Occupancy
#
# 为控制 NCU 开销，默认在 NCU_INPUT_BYTE_CAP（1GiB）子集上 profile；
# 若设置 EXP_INPUT_BYTE_CAP，则与实验 1–3 使用相同数据上限。
# 可通过 NCU_LAUNCH_COUNT 限制 profile 的 kernel launch 次数（Ours 默认 64，Naive 默认 16）。
# 目录输入文件过多时 NCU 自动改用子集中最大的单文件，避免 GPU-Naive 上万次 launch 撑爆显存。
# GPU_DEVICE=auto 选用当前显存占用最低的 GPU；若 GPU0 繁忙可设 GPU_DEVICE=1。
#
# 用法:
#   ./run_exp4_profiling.sh
#   SKIP_NCU=1 ./run_exp4_profiling.sh          # 仅生成空表与报告骨架
#   SKIP_DATASETS=Wiki ./run_exp4_profiling.sh
#   NCU_USE_SUDO=0 ./run_exp4_profiling.sh      # 已放开 RmProfilingAdminOnly 时禁用自动 sudo
#
# 脚本会自动设置 DESTOR_FASTCDC_GPU_PTX、探测 /usr/local/cuda/bin/ncu；
# 若 RmProfilingAdminOnly=1，会自动 sudo -E 重新执行（保留 OUT_DIR 等环境变量）。
#
# 若出现 ERR_NVGPUCTRPERM 且不想每次 sudo，可运行 enable_ncu_profiling.sh 并重启。
set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)

ensure_cuda_path() {
	if [ -d /usr/local/cuda/bin ]; then
		case ":$PATH:" in
		*:/usr/local/cuda/bin:*) ;;
		*) PATH="/usr/local/cuda/bin:$PATH"
			export PATH
			;;
		esac
	fi
}

maybe_reexec_for_ncu() {
	if [ -n "${EXP4_BOOTSTRAPPED:-}" ]; then
		return 0
	fi
	if [ -n "${SKIP_NCU:-}" ]; then
		return 0
	fi
	if [ "${NCU_USE_SUDO:-1}" = "0" ]; then
		return 0
	fi
	if [ "$(id -u)" -eq 0 ]; then
		return 0
	fi

	admin_only=1
	if [ -r /proc/driver/nvidia/params ]; then
		admin_only=$(grep -E '^RmProfilingAdminOnly:' /proc/driver/nvidia/params 2>/dev/null | awk '{print $2}')
	fi
	if [ "${admin_only:-1}" != "1" ]; then
		return 0
	fi

	if ! command -v sudo >/dev/null 2>&1; then
		echo "[warn] RmProfilingAdminOnly=1：需要 root 才能 profile；请 sudo 运行或执行 enable_ncu_profiling.sh" >&2
		return 0
	fi

	ptx="${DESTOR_FASTCDC_GPU_PTX:-$ROOT_DIR/src/chunking/fastcdc_gpu_kernel.ptx}"
	echo "[ncu] RmProfilingAdminOnly=1，自动 sudo -E 重新执行（输入密码一次即可）"
	exec sudo -E env EXP4_BOOTSTRAPPED=1 DESTOR_FASTCDC_GPU_PTX="$ptx" "$0" "$@"
}

maybe_reexec_for_ncu "$@"
ensure_cuda_path
. "$SCRIPT_DIR/exp_config.sh"
. "$SCRIPT_DIR/exp_common.sh"
. "$SCRIPT_DIR/exp_bench.sh"
export DESTOR_FASTCDC_GPU_PTX="${DESTOR_FASTCDC_GPU_PTX:-$ROOT_DIR/src/chunking/fastcdc_gpu_kernel.ptx}"

resolve_ncu_bin() {
	if [ -n "${NCU_BIN:-}" ] && [ "$NCU_BIN" != "ncu" ] && [ -x "$NCU_BIN" ]; then
		printf '%s' "$NCU_BIN"
		return 0
	fi
	if command -v ncu >/dev/null 2>&1; then
		command -v ncu
		return 0
	fi
	for candidate in \
		/usr/local/cuda/bin/ncu \
		/opt/cuda/bin/ncu \
		/usr/bin/ncu; do
		if [ -x "$candidate" ]; then
			printf '%s' "$candidate"
			return 0
		fi
	done
	printf '%s' "ncu"
}

if [ -t 1 ]; then
	C_RST=$(printf '\033[0m')
	C_GPU=$(printf '\033[1;92m')
	C_KW=$(printf '\033[1;93m')
	C_VAL=$(printf '\033[1;97m')
	C_NAIVE=$(printf '\033[1;95m')
	C_OURS=$(printf '\033[1;93m')
	C_WIKI=$(printf '\033[1;36m')
	C_PAPER=$(printf '\033[1;35m')
	C_LINUXDIST=$(printf '\033[1;32m')
	C_GCC=$(printf '\033[1;91m')
else
	C_RST='' C_GPU='' C_KW='' C_VAL='' C_NAIVE='' C_OURS=''
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

color_variant() {
	case "$1" in
	GPU-Naive) printf '%s' "$C_NAIVE" ;;
	Ours-Full) printf '%s' "$C_OURS" ;;
	*) printf '%s' "$C_RST" ;;
	esac
}

TEST_DIR="$ROOT_DIR/test"
OUT_DIR=${OUT_DIR:-"$SCRIPT_DIR/results/exp4_$(date +%Y%m%d_%H%M%S)"}
NCU_DIR="$OUT_DIR/ncu"

GPU_DEVICE=${GPU_DEVICE:-auto}
NCU_BIN=$(resolve_ncu_bin)

resolve_gpu_device() {
	if [ "${GPU_DEVICE:-0}" != "auto" ]; then
		printf '%s' "${GPU_DEVICE:-0}"
		return 0
	fi
	if ! command -v nvidia-smi >/dev/null 2>&1; then
		printf '%s' "0"
		return 0
	fi
	nvidia-smi --query-gpu=index,memory.used --format=csv,noheader,nounits 2>/dev/null \
		| sort -t, -k2 -n \
		| head -1 \
		| cut -d, -f1 \
		| tr -d ' '
}

DATASET_WIKI=${DATASET_WIKI:-/home/xzjin/data/wiki}
DATASET_PAPER=${DATASET_PAPER:-/home/xzjin/data/Paper}
DATASET_GCC=${DATASET_GCC:-/home/xzjin/data/gcc}
DATASET_LINUXDIST=${DATASET_LINUXDIST:-/home/xzjin/data/linuxDist}

SUMMARY_CSV="$OUT_DIR/exp4_profiling.csv"
SUMMARY_HEADER='dataset,algorithm,variant,input,kernel_name,status,ncu_exit,warp_exec_eff_pct,branch_targets_pct,dram_throughput_pct,l1tex_throughput_pct,occupancy_pct,dram_bytes,l1tex_bytes,gpu_time_ns,ncu_report,ncu_csv,ncu_log'

mkdir -p "$OUT_DIR" "$NCU_DIR"

GPU_DEVICE=$(resolve_gpu_device)

warn_busy_gpu() {
	if ! command -v nvidia-smi >/dev/null 2>&1; then
		return 0
	fi
	used=$(nvidia-smi --query-gpu=memory.used,utilization.gpu --format=csv,noheader,nounits -i "$GPU_DEVICE" 2>/dev/null | tr -d ' ')
	if [ -z "$used" ]; then
		return 0
	fi
	mem_used=$(echo "$used" | cut -d, -f1)
	util=$(echo "$used" | cut -d, -f2)
	if [ "$mem_used" -gt 2048 ] 2>/dev/null || [ "$util" -gt 30 ] 2>/dev/null; then
		echo "[warn] GPU $GPU_DEVICE 繁忙 (mem=${mem_used}MiB util=${util}%)；NCU 可能 OOM，建议 GPU_DEVICE=auto 或空闲卡号" >&2
	fi
}

pick_ncu_input() {
	input="$1"
	variant="$2"
	if [ -f "$input" ]; then
		printf '%s' "$input"
		return 0
	fi
	if [ ! -d "$input" ]; then
		printf '%s' "$input"
		return 0
	fi
	file_count=$(find "$input" -type f ! -name .source 2>/dev/null | wc -l)
	total_bytes=$(find "$input" -type f ! -name .source -printf '%s\n' 2>/dev/null \
		| awk '{s+=$1} END {print s+0}')
	use_single=0
	if [ "$file_count" -gt "$NCU_MAX_DIR_FILES" ]; then
		use_single=1
	fi
	if [ "$variant" = "GPU-Naive" ]; then
		use_single=1
	fi
	if [ "$total_bytes" -gt "$NCU_PROFILE_BYTE_CAP" ]; then
		use_single=1
	fi
	if [ "$use_single" -eq 0 ]; then
		printf '%s' "$input"
		return 0
	fi
	largest=$(find "$input" -type f ! -name .source -printf '%s %p\n' 2>/dev/null \
		| sort -rn | head -1 | cut -d' ' -f2-)
	if [ -z "$largest" ]; then
		printf '%s' "$input"
		return 0
	fi
	size_mb=$(awk "BEGIN {printf \"%.1f\", $(stat -c%s "$largest" 2>/dev/null || echo 0) / (1024*1024)}")
	reason="files=${file_count}"
	if [ "$total_bytes" -gt "$NCU_PROFILE_BYTE_CAP" ]; then
		total_mb=$(awk "BEGIN {printf \"%.0f\", $total_bytes / (1024*1024)}")
		reason="${reason} total=${total_mb}MiB"
	fi
	echo "[ncu-input] $variant: NCU 改用最大单文件 (${size_mb}MiB, ${reason}): $largest" >&2
	printf '%s' "$largest"
}

prepare_profile_input() {
	ds_name="$1"
	src="$2"
	profile_input=$(resolve_profile_input "$ds_name" "$src")
	cap="${EXP_INPUT_BYTE_CAP:-$NCU_INPUT_BYTE_CAP}"
	cap_gib=$(awk "BEGIN {printf \"%.2f\", $cap / (1024*1024*1024)}")
	ds_color=$(color_dataset "$ds_name")
	echo "[subset] dataset=${ds_color}${ds_name}${C_RST} cap=${cap_gib}GiB input=$profile_input (tmp, auto-clean)" >&2
	printf '%s' "$profile_input"
}

ncu_progress_filter() {
	# 终端只显示 NCU 进度行（单行刷新）和真实告警；chunkingTool 常规输出仅写入 ncu_log。
	if [ -t 1 ]; then
		awk '
			/^==PROF== Profiling / {
				printf "\r  %-96s", $0
				fflush()
				progress = 1
				next
			}
			/^==WARNING== Note: Running with uncontrolled GPU caches/ {
				if (!warned_cache) {
					if (progress) { printf "\n"; progress = 0 }
					printf "%s\n", $0
					warned_cache = 1
				}
				next
			}
			/^==ERROR==/ {
				if (progress) { printf "\n"; progress = 0 }
				printf "%s\n", $0
				next
			}
			/^==WARNING==/ {
				if (progress) { printf "\n"; progress = 0 }
				printf "%s\n", $0
				next
			}
			/^\[GPU\] fastcdc file / {
				printf "\r  %-96s", $0
				fflush()
				file_progress = 1
				next
			}
			/^==PROF== Connected|^==PROF== Disconnected/ { next }
			/^Chunk GPU:|^\[GPU\]|^elapsed:|^actual elapsed:|^input:|^files:|^bytes:|^chunks:|^configured |^observed / { next }
			END {
				if (progress || file_progress) printf "\n"
			}
		'
	else
		grep -E '^==PROF== Profiling |^==ERROR==|^==WARNING==' || true
	fi
}

append_exp4_row() {
	ds_name="$1"
	algo="$2"
	variant="$3"
	profile_input="$4"
	kernel_hint="$5"
	status="$6"
	ncu_exit="$7"
	ncu_report="$8"
	ncu_csv="$9"
	ncu_log="$10"
	bench_csv="$11"
	python3 "$SCRIPT_DIR/summarize_ncu.py" --append-row "$SUMMARY_CSV" \
		--dataset "$ds_name" --algorithm "$algo" --variant "$variant" --input "$profile_input" \
		--kernel "$kernel_hint" --status "$status" --ncu-exit "$ncu_exit" \
		--ncu-report "$ncu_report" --ncu-csv "$ncu_csv" --ncu-log "$ncu_log" \
		--bench-csv "$bench_csv" --print
}

print_running_summary() {
	ds_name="${1:-}"
	if [ -n "$ds_name" ]; then
		python3 "$SCRIPT_DIR/summarize_ncu.py" --print-summary "$SUMMARY_CSV" --dataset "$ds_name"
	else
		python3 "$SCRIPT_DIR/summarize_ncu.py" --print-summary "$SUMMARY_CSV"
	fi
}

run_ncu_variant() {
	ds_name="$1"
	algo="$2"
	variant="$3"
	profile_input="$4"
	tag="${ds_name}_${algo}_${variant}"
	tag=$(echo "$tag" | tr ' ' '_')
	ncu_report="$NCU_DIR/${tag}.ncu-rep"
	ncu_csv="$NCU_DIR/${tag}.csv"
	ncu_log="$NCU_DIR/${tag}.log"
	bench_csv="$NCU_DIR/${tag}_bench.csv"
	status="ok"
	ncu_exit=0

	rm -f "$ncu_report" "$ncu_csv" "$ncu_log" "$bench_csv"

	ncu_input=$(pick_ncu_input "$profile_input" "$variant")
	launch_count="$NCU_LAUNCH_COUNT"
	case "$variant" in
	GPU-Naive)
		launch_count="$NCU_NAIVE_LAUNCH_COUNT"
		;;
	esac

	set -- "$TOOL" -a "$algo" -i "$ncu_input" $COMMON_OPTS \
		--gpu-device "$GPU_DEVICE" --result-csv "$bench_csv"
	case "$algo" in
	gearjump|jc)
		set -- "$@" --jump-mto "$JUMP_MASK_DELTA"
		;;
	esac
	case "$variant" in
	GPU-Naive)
		set -- "$@" --gpu-naive
		kernel_hint=$(ncu_kernel_naive "$algo")
		ncu_kernel_name="$kernel_hint"
		;;
	Ours-Full)
		set -- "$@" --gpu --gpu-batch "$GPU_BATCH" \
			--gpu-pipeline-tasks "$PIPELINE_FULL" --gpu-threads-per-block "$GPU_THREADS"
		kernel_hint=$(ncu_kernel_ours "$algo")
		ncu_kernel_name="$kernel_hint"
		;;
	*)
		echo "[error] unknown variant: $variant" >&2
		return 1
		;;
	esac

	if [ -z "$ncu_kernel_name" ]; then
		printf '[skip] %s%s%s / %s: no NCU kernel mapping\n' \
			"$(color_algorithm "$algo")" "$algo" "$C_ALGO_RST" "$variant" >&2
		append_exp4_row "$ds_name" "$algo" "$variant" "$profile_input" "" \
			"skipped_no_kernel" 0 "" "" "" "$bench_csv"
		return 0
	fi

	var_color=$(color_variant "$variant")
	ds_color=$(color_dataset "$ds_name")
	algo_color=$(color_algorithm "$algo")
	printf '\n[ncu] ▶ algo=%s%s%s dataset=%s%s%s variant=%s%s%s\n' \
		"$algo_color" "$algo" "$C_ALGO_RST" \
		"$ds_color" "$ds_name" "$C_RST" \
		"$var_color" "$variant" "$C_RST"

	if [ -n "${SKIP_NCU:-}" ]; then
		status="skipped"
		append_exp4_row "$ds_name" "$algo" "$variant" "$profile_input" "$kernel_hint" \
			"$status" 0 "" "" "" "$bench_csv"
		return 0
	fi

	if [ ! -x "$NCU_BIN" ]; then
		status="ncu_missing"
		append_exp4_row "$ds_name" "$algo" "$variant" "$profile_input" "$kernel_hint" \
			"$status" 127 "" "" "" "$bench_csv"
		return 0
	fi

	ncu_replay_mode="$NCU_REPLAY_MODE"
	case "$variant" in
	GPU-Naive)
		ncu_replay_mode="$NCU_NAIVE_REPLAY_MODE"
		;;
	esac

	ncu_exit_file="$ncu_log.exit"
	rm -f "$ncu_exit_file"
	set +e
	(
		"$NCU_BIN" \
			--target-processes all \
			--replay-mode "$ncu_replay_mode" \
			--cache-control "$NCU_CACHE_CONTROL" \
			--kernel-name "$ncu_kernel_name" \
			--launch-count "$launch_count" \
			--metrics "$NCU_METRICS" \
			--export "$ncu_report" \
			"$@"
		echo $? >"$ncu_exit_file"
	) 2>&1 | tee "$ncu_log" | ncu_progress_filter
	ncu_exit=0
	if [ -f "$ncu_exit_file" ]; then
		ncu_exit=$(cat "$ncu_exit_file")
		rm -f "$ncu_exit_file"
	fi
	set -e

	if [ "$ncu_exit" -ne 0 ]; then
		status="ncu_failed"
	fi
	if grep -q "ERR_NVGPUCTRPERM" "$ncu_log" 2>/dev/null; then
		status="ncu_perm_denied"
	fi
	if grep -q "Insufficient free memory" "$ncu_log" 2>/dev/null; then
		status="ncu_oom"
	fi
	if grep -q "No kernels were profiled" "$ncu_log" 2>/dev/null; then
		status="no_kernels"
	fi

	if [ -f "$ncu_report" ]; then
		if ! "$NCU_BIN" --import "$ncu_report" --csv >"$ncu_csv" 2>>"$ncu_log"; then
			status="import_failed"
		fi
	else
		status="no_report"
	fi

	if [ -f "$bench_csv" ] && [ -s "$bench_csv" ]; then
		patch_result_row "$bench_csv" "$variant" "$ds_name" "$algo" "${EXP_TIMING_MODE:-kernel}"
		cap="${EXP_INPUT_BYTE_CAP:-$NCU_INPUT_BYTE_CAP}"
		if [ -n "$cap" ]; then
			python3 "$SCRIPT_DIR/patch_csv.py" "$bench_csv" input_byte_cap "$cap"
		fi
	fi

	append_exp4_row "$ds_name" "$algo" "$variant" "$profile_input" "$kernel_hint" \
		"$status" "$ncu_exit" "$ncu_report" "$ncu_csv" "$ncu_log" "$bench_csv"
}

run_dataset() {
	ds_name="$1"
	full_input="$2"
	if dataset_skipped "$ds_name"; then
		echo "[skip] dataset=$ds_name"
		return 0
	fi
	ds_color=$(color_dataset "$ds_name")
	printf '\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n'
	printf '  数据集: %s%s%s\n' "$ds_color" "$ds_name" "$C_RST"
	printf '━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n'
	profile_input=$(prepare_profile_input "$ds_name" "$full_input")
	for algo in $EXP_ALGORITHMS; do
		if algorithm_skipped "$algo"; then
			printf '[skip] algorithm=%s%s%s (SKIP_ALGORITHMS)\n' \
				"$(color_algorithm "$algo")" "$algo" "$C_ALGO_RST" >&2
			continue
		fi
		if ! algo_supports_ncu "$algo"; then
			printf '[skip] algorithm=%s%s%s (no GPU NCU)\n' \
				"$(color_algorithm "$algo")" "$algo" "$C_ALGO_RST" >&2
			continue
		fi
		printf '\n--- algorithm: %s%s%s ---\n' \
			"$(color_algorithm "$algo")" "$algo" "$C_ALGO_RST"
		if algo_supports_gpu_naive "$algo"; then
			run_ncu_variant "$ds_name" "$algo" "GPU-Naive" "$profile_input"
		fi
		run_ncu_variant "$ds_name" "$algo" "Ours-Full" "$profile_input"
	done
	print_running_summary "$ds_name"
}

build_tools
printf '%s\n' "$SUMMARY_HEADER" > "$SUMMARY_CSV"

echo "============================================================"
printf '实验四：%sNCU 微观 Profiling%s (GPU-Naive vs Ours-Full)\n' "$C_KW" "$C_RST"
printf '  算法: '
print_colored_algorithms "$EXP_ALGORITHMS"
printf ' (Naive + Ours 均 profile)\n'
printf '  子集: cap=%.2f GiB (EXP_INPUT_BYTE_CAP 优先) | NCU launches: Ours=%s Naive=%s | GPU=%s | timing=%s\n' \
	"$(awk "BEGIN {cap=${EXP_INPUT_BYTE_CAP:-$NCU_INPUT_BYTE_CAP}; print cap / (1024*1024*1024)}")" \
	"$NCU_LAUNCH_COUNT" "$NCU_NAIVE_LAUNCH_COUNT" "$GPU_DEVICE" "${EXP_TIMING_MODE:-kernel}"
printf '  NCU: Ours replay=%s | Naive replay=%s | cache=%s\n' \
	"$NCU_REPLAY_MODE" "$NCU_NAIVE_REPLAY_MODE" "$NCU_CACHE_CONTROL"
printf '  输出: %s%s%s\n' "$C_VAL" "$OUT_DIR" "$C_RST"
echo "============================================================"
warn_busy_gpu

if [ -z "${SKIP_NCU:-}" ] && ! command -v "$NCU_BIN" >/dev/null 2>&1 && [ ! -x "$NCU_BIN" ]; then
	echo "[warn] ncu not found ($NCU_BIN); set NCU_BIN=/usr/local/cuda/bin/ncu or SKIP_NCU=1" >&2
elif [ -z "${SKIP_NCU:-}" ]; then
	echo "[ncu] using $NCU_BIN"
fi

run_dataset "Wiki" "$DATASET_WIKI"
run_dataset "Paper" "$DATASET_PAPER"
run_dataset "LinuxDist" "$DATASET_LINUXDIST"
run_dataset "GCC" "$DATASET_GCC"

print_running_summary

python3 - "$OUT_DIR" <<'PY'
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent if "__file__" in dir() else Path.cwd()))
from generate_report import write_exp4_profiling

out_dir = Path(sys.argv[1])
write_exp4_profiling(out_dir)
print(f"[report] {out_dir / 'exp4_profiling_report.md'}")
PY

echo "[done] results in $OUT_DIR"
