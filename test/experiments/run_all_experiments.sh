#!/bin/sh
# Paper experiments for GPU FastCDC chunking.
# End-to-End throughput uses elapsed_ms (wall clock): H2D copy + kernel + D2H copy.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
TEST_DIR="$ROOT_DIR/test"
TOOL="$TEST_DIR/chunkingTool"
DEDUP_TOOL="$SCRIPT_DIR/chunk_dedup_stats"
OUT_DIR="$SCRIPT_DIR/results/$(date +%Y%m%d_%H%M%S)"
DATA_DIR="$SCRIPT_DIR/data"
ABLATION_DIR="$ROOT_DIR/.worktrees/ablation"

GPU_DEVICE=${GPU_DEVICE:-0}
AVG_SIZE=${AVG_SIZE:-4096}
MIN_SIZE=${MIN_SIZE:-1024}
MAX_SIZE=${MAX_SIZE:-16384}
MASK_BITS=${MASK_BITS:-12}
WARP_WINDOW=${WARP_WINDOW:-32}
GPU_BATCH=${GPU_BATCH:-8388608}
GPU_THREADS=${GPU_THREADS:-128}
PIPELINE_FULL=${PIPELINE_FULL:-256}

mkdir -p "$OUT_DIR" "$DATA_DIR" "$ABLATION_DIR"

SILESIA_INPUT=${SILESIA_INPUT:-}
FSL_INPUT=${FSL_INPUT:-}
VM_INPUT=${VM_INPUT:-}

if [ -z "$SILESIA_INPUT" ]; then
	if [ -f "$DATA_DIR/silesia_repo/samba" ]; then
		SILESIA_INPUT="$DATA_DIR/silesia_repo/samba"
	else
		SILESIA_INPUT="/home/xzjin/data/gcc/gcc-4.4.4.tar"
	fi
fi
if [ -z "$FSL_INPUT" ]; then
	FSL_INPUT="/home/xzjin/data/gcc/gcc-11.2.0.tar"
fi
if [ -z "$VM_INPUT" ]; then
	VM_INPUT="/home/xzjin/data/linuxDist/ubuntu-22.04-server-cloudimg-amd64.img"
fi

COMMON_OPTS="-s $AVG_SIZE --min $MIN_SIZE --max $MAX_SIZE --mask-bits $MASK_BITS --warp-window $WARP_WINDOW"

build_tools() {
	make -C "$TEST_DIR" chunkingTool >/dev/null
	make -C "$ROOT_DIR/src/chunking" fastcdc_gpu_ptx >/dev/null
	gcc -O3 -Wall $(pkg-config --cflags glib-2.0) \
		-o "$DEDUP_TOOL" "$SCRIPT_DIR/chunk_dedup_stats.c" \
		"$ROOT_DIR/src/chunking/libchunk.a" -lm -lcrypto -lssl \
		$(pkg-config --libs glib-2.0) -ldl -pthread
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
	if [ ! -x "$out" ]; then
		mkdir -p "$(dirname "$out")"
		if [ ! -d "$worktree" ]; then
			git -C "$ROOT_DIR" worktree add "$worktree" "$commit" >/dev/null
		fi
		build_libchunk_manual "$worktree"
		python3 "$SCRIPT_DIR/patch_chunking_tool_largefile.py" "$worktree/test/chunkingTool.c" >/dev/null
		gcc -O3 -Wall $(pkg-config --cflags glib-2.0) \
			-o "$worktree/test/chunkingTool" "$worktree/test/chunkingTool.c" \
			"$worktree/src/chunking/libchunk.a" -lm -lcrypto -lssl \
			$(pkg-config --libs glib-2.0) -ldl -pthread
		cp "$worktree/test/chunkingTool" "$out"
	fi
}

run_bench() {
	csv="$1"
	label="$2"
	algo="$3"
	gpu_flag="$4"
	input="$5"
	shift 5
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

	echo "[bench] $label input=$(basename "$input") tool=$(basename "$tool")"
	set -- "$tool" -a "$algo" -i "$input" $COMMON_OPTS $extra --result-csv "$csv"
	if [ "$gpu_flag" = "gpu" ]; then
		set -- "$@" --gpu --gpu-device "$GPU_DEVICE" --gpu-batch "$GPU_BATCH"
	fi
	"$@"
	python3 "$SCRIPT_DIR/patch_csv.py" "$csv" \
		config_label "$label" \
		dataset "$(basename "$input")" \
		timing e2e
}

build_tools
build_ablation_tool ee34179 "$ABLATION_DIR/ee34179_chunkingTool"
build_ablation_tool faad046 "$ABLATION_DIR/faad046_chunkingTool"
build_ablation_tool a109ba0 "$ABLATION_DIR/a109ba0_chunkingTool"

EXP1_CSV="$OUT_DIR/exp1_throughput.csv"
printf 'algorithm,input,mode,files,bytes,cfg_min,cfg_avg,cfg_max,mask_bits,warp_window,chunks,obs_min,obs_max,obs_avg,elapsed_ms,actual_elapsed_ms,actual_throughput_mib_s,fingerprint_updates,cutoff_hits,jump_hits,jump_bytes_skipped,redundant_checks,warp_groups,cutoff_lane_sum,tail_idle_lane_sum,min_checks,max_checks,avg_checks,config_label,dataset,timing\n' > "$EXP1_CSV"

for ds_name in Silesia FSL VM; do
	case "$ds_name" in
	Silesia) input="$SILESIA_INPUT" ;;
	FSL) input="$FSL_INPUT" ;;
	VM) input="$VM_INPUT" ;;
	esac
	if [ ! -f "$input" ]; then
		echo "[warn] missing $ds_name input: $input" >&2
		continue
	fi
	run_bench "$EXP1_CSV" "CPU-Serial" fastcdc cpu "$input"
	run_bench "$EXP1_CSV" "CPU-Parallel" fastcdc cpu "$input" --cpu-parallel
	run_bench "$EXP1_CSV" "GPU-Naive" fastcdc gpu "$input" \
		"$ABLATION_DIR/ee34179_chunkingTool"
	run_bench "$EXP1_CSV" "Ours-Full" fastcdc gpu "$input" "$TOOL" \
		--gpu-pipeline-tasks "$PIPELINE_FULL" --gpu-threads-per-block "$GPU_THREADS" --profile-chunking
done

ABLATION_INPUT=${ABLATION_INPUT:-$FSL_INPUT}
EXP3_CSV="$OUT_DIR/exp3_ablation.csv"
printf 'algorithm,input,mode,files,bytes,cfg_min,cfg_avg,cfg_max,mask_bits,warp_window,chunks,obs_min,obs_max,obs_avg,elapsed_ms,actual_elapsed_ms,actual_throughput_mib_s,fingerprint_updates,cutoff_hits,jump_hits,jump_bytes_skipped,redundant_checks,warp_groups,cutoff_lane_sum,tail_idle_lane_sum,min_checks,max_checks,avg_checks,config_label,dataset,timing\n' > "$EXP3_CSV"

run_bench "$EXP3_CSV" "A-GPU-Naive" fastcdc gpu "$ABLATION_INPUT" \
	"$ABLATION_DIR/ee34179_chunkingTool"
run_bench "$EXP3_CSV" "B-Segment-Batch" fastcdc gpu "$ABLATION_INPUT" \
	"$ABLATION_DIR/faad046_chunkingTool"
run_bench "$EXP3_CSV" "C-Pipeline" fastcdc gpu "$ABLATION_INPUT" \
	"$ABLATION_DIR/a109ba0_chunkingTool"
run_bench "$EXP3_CSV" "D-Full-ParallelScan" fastcdc gpu "$ABLATION_INPUT" "$TOOL" \
	--gpu-pipeline-tasks "$PIPELINE_FULL" --gpu-threads-per-block "$GPU_THREADS" --profile-chunking

SWEEP_INPUT=${SWEEP_INPUT:-$FSL_INPUT}
EXP41_CSV="$OUT_DIR/exp4_1_pipeline_tasks.csv"
for tasks in 16 32 64 128 256 512; do
	run_bench "$EXP41_CSV" "pipeline_tasks=$tasks" fastcdc gpu "$SWEEP_INPUT" "$TOOL" \
		--gpu-pipeline-tasks "$tasks" --gpu-threads-per-block "$GPU_THREADS"
done

EXP42_CSV="$OUT_DIR/exp4_2_thread_blocks.csv"
for threads in 128 256 512; do
	echo "[exp4.2] threads_per_block=$threads"
	"$TOOL" -a fastcdc -i "$SWEEP_INPUT" $COMMON_OPTS \
		--gpu --gpu-device "$GPU_DEVICE" --gpu-batch "$GPU_BATCH" \
		--gpu-pipeline-tasks "$PIPELINE_FULL" --gpu-threads-per-block "$threads" \
		--profile-chunking --result-csv "$EXP42_CSV"
	python3 "$SCRIPT_DIR/patch_csv.py" "$EXP42_CSV" \
		config_label "threads=$threads" \
		dataset "$(basename "$SWEEP_INPUT")" \
		timing kernel
done

EXP2_CSV="$OUT_DIR/exp2_correctness.csv"
printf 'dataset,input,mode,raw_bytes,chunk_count,avg_chunk_size,obs_min,obs_max,unique_bytes,dedup_ratio\n' > "$EXP2_CSV"
for ds_name in Silesia FSL VM; do
	case "$ds_name" in
	Silesia) input="$SILESIA_INPUT" ;;
	FSL) input="$FSL_INPUT" ;;
	VM) input="$VM_INPUT" ;;
	esac
	if [ ! -f "$input" ]; then
		continue
	fi
	for mode in cpu gpu; do
		echo "[exp2] dataset=$ds_name mode=$mode"
		stats=$("$DEDUP_TOOL" --mode "$mode" -i "$input" -s "$AVG_SIZE" --min "$MIN_SIZE" --max "$MAX_SIZE" --mask-bits "$MASK_BITS")
		raw=$(echo "$stats" | awk -F, '/^raw_bytes,/{print $2}')
		chunks=$(echo "$stats" | awk -F, '/^chunk_count,/{print $2}')
		avg=$(echo "$stats" | awk -F, '/^avg_chunk_size,/{print $2}')
		omin=$(echo "$stats" | awk -F, '/^obs_min,/{print $2}')
		omax=$(echo "$stats" | awk -F, '/^obs_max,/{print $2}')
		uniq=$(echo "$stats" | awk -F, '/^unique_bytes,/{print $2}')
		ratio=$(echo "$stats" | awk -F, '/^dedup_ratio,/{print $2}')
		printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
			"$ds_name" "$input" "$mode" "$raw" "$chunks" "$avg" "$omin" "$omax" "$uniq" "$ratio" >> "$EXP2_CSV"
	done
done

if [ -z "${SKIP_NCU:-}" ] && command -v ncu >/dev/null 2>&1; then
	NCU_DIR="$OUT_DIR/ncu"
	mkdir -p "$NCU_DIR"
	NCU_METRICS="dram__bytes.sum,gpu__time_duration.sum,smsp__warps_active.avg.pct_of_peak_sustained_active,smsp__sass_average_branch_targets_threads_per_instruction.pct,l1tex__t_bytes.sum"
	for variant in naive full; do
		if [ "$variant" = "naive" ]; then
			NCU_TOOL="$ABLATION_DIR/a109ba0_chunkingTool"
			tag="gpu_pipeline_noscan"
		else
			NCU_TOOL="$TOOL"
			tag="gpu_full_parallel_scan"
		fi
		rep="$NCU_DIR/${tag}.ncu-rep"
		csv="$NCU_DIR/${tag}.csv"
		echo "[exp5] ncu variant=$variant"
		ncu --target-processes all --metrics "$NCU_METRICS" --export "$rep" \
			"$NCU_TOOL" -a fastcdc -i "$SWEEP_INPUT" $COMMON_OPTS \
			--gpu --gpu-device "$GPU_DEVICE" --gpu-batch "$GPU_BATCH" \
			--gpu-pipeline-tasks "$PIPELINE_FULL" --gpu-threads-per-block "$GPU_THREADS" \
			--profile-chunking --result-csv "$NCU_DIR/${tag}_bench.csv" \
			>"$NCU_DIR/${tag}.log" 2>&1 || true
		if [ -f "$rep" ]; then
			ncu --import "$rep" --csv >"$csv" 2>>"$NCU_DIR/${tag}.log" || true
		fi
	done
	python3 "$SCRIPT_DIR/summarize_ncu.py" "$NCU_DIR" >"$OUT_DIR/exp5_ncu_summary.md" 2>/dev/null || true
fi

python3 "$SCRIPT_DIR/generate_report.py" "$OUT_DIR"
echo "[done] results in $OUT_DIR"
