# Shared helpers for experiment scripts (source, do not execute).
# Algorithms: fastcdc, gear, gearjump (CLI name for jump chunk / jc GPU path).
#
#   EXP_ALGORITHMS   default "fastcdc gear gearjump"
#   SKIP_ALGORITHMS  comma list to skip, e.g. SKIP_ALGORITHMS=gear
#   JUMP_MASK_DELTA  --jump-mto for gearjump, default 1

EXP_ALGORITHMS=${EXP_ALGORITHMS:-fastcdc gear gearjump}
JUMP_MASK_DELTA=${JUMP_MASK_DELTA:-1}

# Per-algorithm terminal colors (no-op when stdout is not a TTY).
exp_common_init_algo_colors() {
	if [ -t 1 ]; then
		C_ALGO_FASTCDC=$(printf '\033[1;33m')
		C_ALGO_GEAR=$(printf '\033[1;96m')
		C_ALGO_GEARJUMP=$(printf '\033[1;95m')
		C_ALGO_JC=$(printf '\033[1;95m')
		C_ALGO_DEF=$(printf '\033[1;97m')
		C_ALGO_RST=$(printf '\033[0m')
	else
		C_ALGO_FASTCDC=''
		C_ALGO_GEAR=''
		C_ALGO_GEARJUMP=''
		C_ALGO_JC=''
		C_ALGO_DEF=''
		C_ALGO_RST=''
	fi
}

color_algorithm() {
	case "$1" in
	fastcdc)
		printf '%s' "$C_ALGO_FASTCDC"
		;;
	gear)
		printf '%s' "$C_ALGO_GEAR"
		;;
	gearjump|jc)
		printf '%s' "$C_ALGO_GEARJUMP"
		;;
	*)
		printf '%s' "$C_ALGO_DEF"
		;;
	esac
}

# Print a space-separated algorithm list with per-algorithm highlight.
print_colored_algorithms() {
	list="$1"
	first=1
	for algo in $list; do
		if [ "$first" -eq 0 ]; then
			printf ' '
		fi
		first=0
		printf '%s%s%s' "$(color_algorithm "$algo")" "$algo" "$C_ALGO_RST"
	done
}

algorithm_skipped() {
	algo="$1"
	case ",${SKIP_ALGORITHMS:-}," in
	*,$algo,*)
		return 0
		;;
	esac
	return 1
}

algo_supports_gpu() {
	case "$1" in
	fastcdc|gear|gearjump|jc)
		return 0
		;;
	*)
		return 1
		;;
	esac
}

algo_supports_gpu_naive() {
	case "$1" in
	fastcdc|gear|gearjump|jc)
		return 0
		;;
	*)
		return 1
		;;
	esac
}

algo_supports_gpu_sensitivity() {
	algo_supports_gpu "$1"
}

algo_supports_ncu() {
	case "$1" in
	fastcdc|gear|gearjump|jc)
		return 0
		;;
	*)
		return 1
		;;
	esac
}

ncu_kernel_ours() {
	case "$1" in
	fastcdc)
		printf '%s' "fastcdc_chunk_kernel"
		;;
	gear)
		printf '%s' "gear_chunk_kernel"
		;;
	gearjump|jc)
		printf '%s' "jc_chunk_kernel"
		;;
	*)
		printf '%s' ""
		;;
	esac
}

ncu_kernel_naive() {
	case "$1" in
	fastcdc)
		printf '%s' "fastcdc_naive_chunk_kernel"
		;;
	gear)
		printf '%s' "gear_naive_chunk_kernel"
		;;
	gearjump|jc)
		printf '%s' "jc_naive_chunk_kernel"
		;;
	*)
		printf '%s' ""
		;;
	esac
}

patch_result_row() {
	csv="$1"
	label="$2"
	dataset_name="$3"
	algo="$4"
	timing="${5:-e2e}"
	shift 5 || true
	python3 "$SCRIPT_DIR/patch_csv.py" "$csv" \
		config_label "$label" \
		dataset "$dataset_name" \
		chunk_algo "$algo" \
		timing "$timing" \
		"$@"
}

# Temporary byte-capped dataset views (symlinks; removed on script exit).
DESTOR_SUBSET_TMP=""
DESTOR_SUBSET_CLEANUP_REGISTERED=0

subset_tmp_cleanup() {
	if [ -n "$DESTOR_SUBSET_TMP" ] && [ -d "$DESTOR_SUBSET_TMP" ]; then
		rm -rf "$DESTOR_SUBSET_TMP"
	fi
	DESTOR_SUBSET_TMP=""
}

subset_tmp_init() {
	if [ -n "$DESTOR_SUBSET_TMP" ]; then
		return 0
	fi
	DESTOR_SUBSET_TMP=$(mktemp -d "${TMPDIR:-/tmp}/destor-subset.XXXXXX")
	if [ "${KEEP_SUBSET:-0}" != "1" ] && [ "$DESTOR_SUBSET_CLEANUP_REGISTERED" -eq 0 ]; then
		DESTOR_SUBSET_CLEANUP_REGISTERED=1
		trap subset_tmp_cleanup EXIT INT TERM
	fi
}

prepare_capped_input() {
	ds_name="$1"
	src="$2"
	cap_bytes="$3"
	subset_tmp_init
	python3 "$SCRIPT_DIR/prepare_naive_subset.py" "$src" "$DESTOR_SUBSET_TMP/$ds_name" "$cap_bytes"
}

exp_common_init_algo_colors
