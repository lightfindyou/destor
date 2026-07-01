#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <dirent.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <time.h>
#include <stdarg.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "../src/destor.h"
#include "../src/chunking/chunking.h"
#include "../src/chunking/gear_common.h"
#include "chunk_tool_timing.h"

struct destor destor;

typedef int (*chunk_fn_t)(unsigned char *p, int n);
typedef int (*chunk_batch_fn_t)(unsigned char **buffers,
		const int *sizes,
		int task_count,
		int *chunk_sizes);
typedef int (*chunk_segment_batch_fn_t)(unsigned char **buffers,
		const int *sizes,
		int task_count,
		int boundary_stride,
		int *boundary_counts,
		int *chunk_sizes);
typedef void (*chunk_close_fn_t)(void);

struct chunk_tool_options {
	const char *algorithm;
	const char *batch_algorithms;
	const char *input_path;
	const char *result_csv_path;
	int chunk_avg_size;
	int chunk_min_size;
	int chunk_max_size;
	int chunk_mask_bits;
	int chunk_warp_window;
	int jump_mask_delta;
	int leap_par_idx;
	int profile_chunking;
	int cpu_parallel;
	int gpu_enabled;
	int gpu_naive;
	int gpu_device_id;
	int gpu_batch_size;
	int gpu_pipeline_tasks;
	int gpu_threads_per_block;
	int print_chunks;
	int print_limit;
	int run_all;
};

struct chunk_tool_run {
	chunk_fn_t chunk_fn;
	chunk_batch_fn_t chunk_batch_fn;
	chunk_segment_batch_fn_t chunk_segment_batch_fn;
	chunk_close_fn_t close_fn;
	const char *display_name;
	int effective_avg_size;
	int uses_gpu;
};

struct chunk_tool_stats {
	const char *algorithm;
	int configured_min;
	int configured_avg;
	int configured_max;
	int configured_mask_bits;
	int configured_warp_window;
	size_t file_count;
	size_t total_bytes;
	size_t chunk_count;
	int observed_min;
	int observed_max;
	double observed_avg;
	double elapsed_ms;
	double actual_elapsed_ms;
	int uses_gpu;
	int profile_chunking;
	struct chunk_experiment_stats experiment_stats;
};

struct chunk_tool_path_list {
	char **items;
	int count;
	int capacity;
	size_t total_bytes;
};

struct chunk_tool_progress_state {
	const struct chunk_tool_options *options;
	const struct chunk_tool_run *run;
	const struct chunk_tool_path_list *paths;
	int current_file_index;
	size_t current_file_size;
	size_t completed_bytes;
	struct timespec last_update;
	int active;
};

static int select_algorithm(const struct chunk_tool_options *options, struct chunk_tool_run *run);
static int run_chunking(const struct chunk_tool_options *options,
		const struct chunk_tool_run *run,
		unsigned char *buffer,
		size_t buffer_size,
		struct chunk_tool_stats *stats);
static int append_stats_csv(const char *path,
		const char *input_label,
		const struct chunk_tool_stats *stats);
static int run_chunking_batch(const struct chunk_tool_options *options,
		const struct chunk_tool_run *run,
		const struct chunk_tool_path_list *paths,
		struct chunk_tool_stats *stats);
static void baseline_parallel_init(void);
static int baseline_parallel_chunk_data(unsigned char *p, int n);
static void reset_destor_for_run(const struct chunk_tool_options *options);
static int execute_path_run_gpu_naive_parallel(const struct chunk_tool_options *options,
		const struct chunk_tool_path_list *paths,
		struct chunk_tool_stats *stats);
static int execute_path_run_cpu_parallel(const struct chunk_tool_options *options,
		const struct chunk_tool_path_list *paths,
		struct chunk_tool_stats *stats);
static int chunk_tool_cpu_parallel_eligible(const struct chunk_tool_options *options);

static const char *k_all_algorithms[] = {
	"rabin",
	"normalized-rabin",
	"rabin-jump",
	"tttd",
	"ae",
	"sc",
	"baseline",
	"fastcdc",
	"gear",
	"jc",
	"jctttd",
	"normalized-gearjump",
	"tttdgear",
	"leap"
};

#define CHUNK_TOOL_ALGORITHM_COUNT ((int)(sizeof(k_all_algorithms) / sizeof(k_all_algorithms[0])))
#define CHUNK_TOOL_MAX_BATCH 1024

static uint64_t g_baseline_mask;
static struct chunk_tool_progress_state g_chunk_tool_progress;

static int chunk_tool_stdout_supports_color(void) {
	return isatty(STDOUT_FILENO);
}

static int chunk_tool_stderr_supports_progress(void) {
	return isatty(STDERR_FILENO);
}

static const char *chunk_tool_mode_style(int uses_gpu) {
	if (!chunk_tool_stdout_supports_color()) {
		return "";
	}
	return uses_gpu ? "\033[1;92m" : "\033[1;96m";
}

static const char *chunk_tool_stderr_mode_style(int uses_gpu) {
	if (!chunk_tool_stderr_supports_progress()) {
		return "";
	}
	return uses_gpu ? "\033[1;92m" : "\033[1;96m";
}

static const char *chunk_tool_highlight_style(void) {
	return chunk_tool_stdout_supports_color() ? "\033[1;93m" : "";
}

static const char *chunk_tool_metric_style(void) {
	return chunk_tool_stdout_supports_color() ? "\033[1;97m" : "";
}

static const char *chunk_tool_reset_style(void) {
	return chunk_tool_stdout_supports_color() ? "\033[0m" : "";
}

static const char *chunk_tool_stderr_reset_style(void) {
	return chunk_tool_stderr_supports_progress() ? "\033[0m" : "";
}

static int chunk_tool_invoke_len(size_t nbytes) {
	if (nbytes > (size_t)INT_MAX) {
		return INT_MAX;
	}
	return (int)nbytes;
}

static int chunk_tool_cpu_parallel_workers(int file_count) {
	const char *env = getenv("CPU_PARALLEL_FILE_THREADS");
	long value;
	long workers;

	if (env && *env) {
		value = strtol(env, NULL, 10);
		if (value > 0) {
			return value > file_count ? file_count : (int)value;
		}
	}
	workers = 8;
	return workers > file_count ? file_count : (int)workers;
}

static int chunk_tool_gpu_naive_workers(int file_count) {
	const char *env = getenv("GPU_NAIVE_FILE_THREADS");
	long value;

	if (env && *env) {
		value = strtol(env, NULL, 10);
		if (value > 0) {
			return value > file_count ? file_count : (int)value;
		}
	}
	return fastcdc_gpu_naive_probe_max_workers(file_count);
}

static double chunk_tool_throughput_mib_s(const struct chunk_tool_stats *stats) {
	if (!stats || stats->elapsed_ms <= 0.0) {
		return 0.0;
	}
	return ((double)stats->total_bytes * 1000.0)
			/ (stats->elapsed_ms * 1024.0 * 1024.0);
}

static double chunk_tool_actual_elapsed_ms(const struct chunk_tool_stats *stats) {
	if (!stats) {
		return 0.0;
	}
	if (stats->actual_elapsed_ms > 0.0) {
		return stats->actual_elapsed_ms;
	}
	return stats->elapsed_ms;
}

static double chunk_tool_actual_throughput_mib_s(const struct chunk_tool_stats *stats) {
	double actual_elapsed_ms;

	if (!stats) {
		return 0.0;
	}
	actual_elapsed_ms = chunk_tool_actual_elapsed_ms(stats);
	if (actual_elapsed_ms <= 0.0) {
		return 0.0;
	}
	return ((double)stats->total_bytes * 1000.0)
			/ (actual_elapsed_ms * 1024.0 * 1024.0);
}

static void chunk_tool_progress_clear(void) {
	if (!g_chunk_tool_progress.active || !chunk_tool_stderr_supports_progress()) {
		g_chunk_tool_progress.active = 0;
		return;
	}
	fprintf(stderr, "\r\033[2K");
	fflush(stderr);
	g_chunk_tool_progress.active = 0;
}

static int chunk_tool_progress_should_refresh(void) {
	struct timespec now;
	double elapsed_ms;

	if (!chunk_tool_stderr_supports_progress()) {
		return 0;
	}
	clock_gettime(CLOCK_MONOTONIC, &now);
	elapsed_ms = chunk_tool_timespec_diff_ms(&g_chunk_tool_progress.last_update, &now);
	if (elapsed_ms < 3000.0) {
		return 0;
	}
	g_chunk_tool_progress.last_update = now;
	return 1;
}

static void chunk_tool_progress_render(size_t current_file_offset) {
	double file_percent = 100.0;
	double total_percent = 100.0;
	size_t total_done;

	if (!g_chunk_tool_progress.active || !chunk_tool_stderr_supports_progress()) {
		return;
	}
	if (g_chunk_tool_progress.current_file_size > 0) {
		file_percent = (double)current_file_offset * 100.0
				/ (double)g_chunk_tool_progress.current_file_size;
		if (file_percent > 100.0) {
			file_percent = 100.0;
		}
	}
	total_done = g_chunk_tool_progress.completed_bytes + current_file_offset;
	if (g_chunk_tool_progress.paths && g_chunk_tool_progress.paths->total_bytes > 0) {
		total_percent = (double)total_done * 100.0
				/ (double)g_chunk_tool_progress.paths->total_bytes;
		if (total_percent > 100.0) {
			total_percent = 100.0;
		}
	}
	fprintf(stderr,
			"\r\033[2K%s[%s]%s %s file %d/%d  file %.1f%%  total %.1f%%  bytes %zu/%zu",
			chunk_tool_stderr_mode_style(g_chunk_tool_progress.run
					&& g_chunk_tool_progress.run->uses_gpu),
			g_chunk_tool_progress.run && g_chunk_tool_progress.run->uses_gpu ? "GPU" : "CPU",
			chunk_tool_stderr_reset_style(),
			g_chunk_tool_progress.run ? g_chunk_tool_progress.run->display_name : "chunking",
			g_chunk_tool_progress.current_file_index + 1,
			g_chunk_tool_progress.paths ? g_chunk_tool_progress.paths->count : 1,
			file_percent,
			total_percent,
			total_done,
			g_chunk_tool_progress.paths ? g_chunk_tool_progress.paths->total_bytes : g_chunk_tool_progress.current_file_size);
	fflush(stderr);
}

static void chunk_tool_progress_begin(const struct chunk_tool_options *options,
		const struct chunk_tool_run *run,
		const struct chunk_tool_path_list *paths,
		int current_file_index,
		size_t current_file_size,
		size_t completed_bytes) {
	if (!chunk_tool_stderr_supports_progress()) {
		return;
	}
	g_chunk_tool_progress.options = options;
	g_chunk_tool_progress.run = run;
	g_chunk_tool_progress.paths = paths;
	g_chunk_tool_progress.current_file_index = current_file_index;
	g_chunk_tool_progress.current_file_size = current_file_size;
	g_chunk_tool_progress.completed_bytes = completed_bytes;
	if (!g_chunk_tool_progress.active) {
		clock_gettime(CLOCK_MONOTONIC, &g_chunk_tool_progress.last_update);
	}
	g_chunk_tool_progress.active = 1;
}

static void chunk_tool_progress_finish_file(size_t current_file_size) {
	if (!g_chunk_tool_progress.active) {
		return;
	}
	if (chunk_tool_progress_should_refresh()) {
		chunk_tool_progress_render(current_file_size);
	}
	g_chunk_tool_progress.completed_bytes += current_file_size;
}

static void chunk_tool_error_at(const char *file,
		int line,
		const char *func,
		const char *fmt,
		...) {
	va_list ap;

	fprintf(stderr, "%s:%d:%s: ", file, line, func);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

#define CHUNK_TOOL_ERROR(...) chunk_tool_error_at(__FILE__, __LINE__, __func__, __VA_ARGS__)

static void usage(const char *prog) {
	fprintf(stderr,
			"Usage: %s -a algorithm -i input [options]\n"
			"\n"
			"Options:\n"
			"  --list                 List supported algorithms\n"
			"      --batch LIST       Comma-separated algorithms to run on one input\n"
			"      --all              Run all supported algorithms on one input\n"
			"  -a, --algorithm NAME   Algorithm name\n"
			"  -i, --input PATH       Input file or directory to chunk\n"
			"  -s, --avg SIZE         Average chunk size, default 4096\n"
			"      --min SIZE         Minimum chunk size, default avg/4\n"
			"      --max SIZE         Maximum chunk size, default avg*4\n"
			"      --mask-bits N      Override boundary mask bits for JC/FastCDC\n"
			"      --warp-window N    Record target warp window size, default 32\n"
			"      --profile-chunking Collect CPU-side chunking work counters\n"
			"      --cpu-parallel     Multi-threaded per-file CPU chunking (baseline/fastcdc/gear/gearjump)\n"
			"      --result-csv PATH  Append one CSV row per run to PATH\n"
			"      --gpu              Enable GPU wrapper when supported\n"
			"      --gpu-naive        Naive GPU chunking (per-chunk H2D/kernel/D2H): fastcdc, gear, gearjump\n"
			"      --gpu-device ID    GPU device id, default 0\n"
			"      --gpu-batch SIZE   GPU batch size, default 8388608\n"
			"      --gpu-pipeline-tasks N  GPU sub-batch tasks per launch, default 256\n"
			"      --gpu-threads-per-block N  GPU threads per block, default 128\n"
			"      --jump-mto N       GearJump mask delta, default 1\n"
			"      --leap-par-idx N   Leap parallelism index, default 0\n"
			"      --print-chunks     Print chunk boundaries\n"
			"      --print-limit N    Limit printed chunks, default 32\n"
			"\n"
			"Batch examples:\n"
			"  %s --batch rabin,fastcdc,jc -i input.bin\n"
			"  %s --all -i input.bin\n"
			"\n"
			"Algorithms:\n"
			"  rabin\n"
			"  normalized-rabin\n"
			"  rabin-jump\n"
			"  tttd\n"
			"  ae\n"
			"  sc\n"
			"  baseline\n"
			"  fastcdc\n"
			"  gear\n"
			"  jc\n"
			"  jctttd\n"
			"  normalized-gearjump\n"
			"  tttdgear\n"
			"  leap\n",
			prog,
			prog,
			prog);
}

static void list_algorithms(void) {
	int i;

	for (i = 0; i < CHUNK_TOOL_ALGORITHM_COUNT; i++) {
		puts(k_all_algorithms[i]);
	}
}

void destor_log(int level, const char *fmt, ...) {
	FILE *stream = level >= DESTOR_WARNING ? stderr : stdout;
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stream, fmt, ap);
	fputc('\n', stream);
	va_end(ap);
}

static int parse_int_arg(const char *label, const char *value) {
	char *end = NULL;
	long parsed = strtol(value, &end, 10);

	if (!value || *value == '\0' || (end && *end != '\0')) {
		CHUNK_TOOL_ERROR("Invalid %s: %s", label, value ? value : "<null>");
		return -1;
	}
	if (parsed <= 0 || parsed > INT_MAX) {
		CHUNK_TOOL_ERROR("%s out of range: %s", label, value);
		return -1;
	}
	return (int)parsed;
}

static int parse_nonnegative_int_arg(const char *label, const char *value) {
	char *end = NULL;
	long parsed = strtol(value, &end, 10);

	if (!value || *value == '\0' || (end && *end != '\0')) {
		CHUNK_TOOL_ERROR("Invalid %s: %s", label, value ? value : "<null>");
		return -1;
	}
	if (parsed < 0 || parsed > INT_MAX) {
		CHUNK_TOOL_ERROR("%s out of range: %s", label, value);
		return -1;
	}
	return (int)parsed;
}

static int normalize_power_of_two(int size) {
	int normalized = 1;

	while (normalized <= size / 2) {
		normalized <<= 1;
	}
	return normalized;
}

static int parse_algorithm_list(const char *text, const char **algorithms, int max_algorithms) {
	char *copy;
	char *token;
	char *saveptr = NULL;
	int count = 0;

	if (!text || !*text) {
		CHUNK_TOOL_ERROR("Empty batch algorithm list");
		return -1;
	}

	copy = strdup(text);
	if (!copy) {
		CHUNK_TOOL_ERROR("Allocation failed while parsing algorithm list");
		return -1;
	}

	for (token = strtok_r(copy, ",", &saveptr);
			token;
			token = strtok_r(NULL, ",", &saveptr)) {
		while (*token == ' ' || *token == '\t') {
			token++;
		}
		if (*token == '\0') {
			continue;
		}
		if (count >= max_algorithms) {
			CHUNK_TOOL_ERROR("Too many algorithms in batch list, max=%d", max_algorithms);
			free(copy);
			return -1;
		}
		algorithms[count++] = strdup(token);
	}

	free(copy);
	if (count == 0) {
		CHUNK_TOOL_ERROR("No usable algorithms found in batch list");
		return -1;
	}
	return count;
}

static void free_algorithm_list(const char **algorithms, int count) {
	int i;

	for (i = 0; i < count; i++) {
		free((void *)algorithms[i]);
	}
}

static int append_path(struct chunk_tool_path_list *paths, const char *path) {
	char **new_items;
	char *copy;
	int new_capacity;

	if (paths->count == paths->capacity) {
		new_capacity = paths->capacity == 0 ? 32 : paths->capacity * 2;
		new_items = (char **)realloc(paths->items, (size_t)new_capacity * sizeof(char *));
		if (!new_items) {
			CHUNK_TOOL_ERROR("Allocation failed while growing input path list");
			return -1;
		}
		paths->items = new_items;
		paths->capacity = new_capacity;
	}

	copy = strdup(path);
	if (!copy) {
		CHUNK_TOOL_ERROR("Allocation failed while copying input path %s", path);
		return -1;
	}
	paths->items[paths->count++] = copy;
	return 0;
}

static void free_path_list(struct chunk_tool_path_list *paths) {
	int i;

	for (i = 0; i < paths->count; i++) {
		free(paths->items[i]);
	}
	free(paths->items);
	paths->items = NULL;
	paths->count = 0;
	paths->capacity = 0;
	paths->total_bytes = 0;
}

static int collect_input_paths_recursive(const char *path, struct chunk_tool_path_list *paths) {
	struct stat st;
	DIR *dir;
	struct dirent *entry;

	if (lstat(path, &st) != 0) {
		CHUNK_TOOL_ERROR("Failed to stat %s: %s", path, strerror(errno));
		return -1;
	}

	if (S_ISREG(st.st_mode)) {
		if (append_path(paths, path) != 0) {
			return -1;
		}
		paths->total_bytes += (size_t)st.st_size;
		return 0;
	}
	if (!S_ISDIR(st.st_mode)) {
		return 0;
	}

	dir = opendir(path);
	if (!dir) {
		CHUNK_TOOL_ERROR("Failed to open directory %s: %s", path, strerror(errno));
		return -1;
	}

	while ((entry = readdir(dir)) != NULL) {
		char *child_path;
		size_t path_len;
		size_t name_len;
		int rc;

		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}

		path_len = strlen(path);
		name_len = strlen(entry->d_name);
		child_path = (char *)malloc(path_len + 1 + name_len + 1);
		if (!child_path) {
			closedir(dir);
			CHUNK_TOOL_ERROR("Allocation failed while descending into %s", path);
			return -1;
		}

		memcpy(child_path, path, path_len);
		if (path_len > 0 && path[path_len - 1] == '/') {
			memcpy(child_path + path_len, entry->d_name, name_len + 1);
		} else {
			child_path[path_len] = '/';
			memcpy(child_path + path_len + 1, entry->d_name, name_len + 1);
		}

		rc = collect_input_paths_recursive(child_path, paths);
		free(child_path);
		if (rc != 0) {
			closedir(dir);
			return -1;
		}
	}

	closedir(dir);
	return 0;
}

static int read_input_file(const char *path,
		int prefer_mmap,
		unsigned char **buffer,
		size_t *buffer_size,
		int *is_mmap) {
	struct stat st;
	FILE *fp;
	unsigned char *data;
	size_t read_len;
	size_t file_size;
	int fd;

	if (buffer) {
		*buffer = NULL;
	}
	if (buffer_size) {
		*buffer_size = 0;
	}
	if (is_mmap) {
		*is_mmap = 0;
	}

	if (stat(path, &st) != 0) {
		CHUNK_TOOL_ERROR("Failed to stat %s: %s", path, strerror(errno));
		return -1;
	}
	if (!S_ISREG(st.st_mode)) {
		CHUNK_TOOL_ERROR("Input path is not a regular file: %s", path);
		return -1;
	}
	if (st.st_size < 0) {
		CHUNK_TOOL_ERROR("Negative file size reported for %s", path);
		return -1;
	}
	if ((uintmax_t)st.st_size > (uintmax_t)SIZE_MAX) {
		CHUNK_TOOL_ERROR("File is too large to map into memory on this build: %s (%ju bytes)",
				path,
				(uintmax_t)st.st_size);
		return -1;
	}
	file_size = (size_t)st.st_size;
	if (prefer_mmap && file_size > 0) {
		fd = open(path, O_RDONLY);
		if (fd >= 0) {
			void *mapped = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
			close(fd);
			if (mapped != MAP_FAILED) {
				*buffer = (unsigned char *)mapped;
				*buffer_size = file_size;
				if (is_mmap) {
					*is_mmap = 1;
				}
				return 0;
			}
		}
	}

	fp = fopen(path, "rb");

	if (!fp) {
		CHUNK_TOOL_ERROR("Failed to open %s: %s", path, strerror(errno));
		return -1;
	}

	data = (unsigned char *)malloc(file_size);
	if (!data && file_size > 0) {
		CHUNK_TOOL_ERROR("Allocation failed for %s (%zu bytes)", path, file_size);
		fclose(fp);
		return -1;
	}

	read_len = fread(data, 1, file_size, fp);
	if (read_len != file_size) {
		CHUNK_TOOL_ERROR("Failed to read %s: expected %zu, got %zu", path, file_size, read_len);
		free(data);
		fclose(fp);
		return -1;
	}

	fclose(fp);
	*buffer = data;
	*buffer_size = (size_t)file_size;
	return 0;
}

static void release_input_file(unsigned char *buffer, size_t buffer_size, int is_mmap) {
	if (!buffer) {
		return;
	}
	if (is_mmap) {
		munmap(buffer, buffer_size);
		return;
	}
	free(buffer);
}

static void init_stats(struct chunk_tool_stats *stats) {
	memset(stats, 0, sizeof(*stats));
	stats->observed_min = INT_MAX;
}

static void finalize_stats(struct chunk_tool_stats *stats) {
	if (stats->chunk_count == 0) {
		stats->observed_min = 0;
		stats->observed_max = 0;
		stats->observed_avg = 0.0;
		return;
	}
	stats->observed_avg = (double)stats->total_bytes / (double)stats->chunk_count;
}

static void print_stats_summary(const struct chunk_tool_stats *stats, const char *input_label) {
	const char *hi = chunk_tool_highlight_style();
	const char *metric = chunk_tool_metric_style();
	const char *rst = chunk_tool_reset_style();

	printf("%s[%s]%s algorithm=%s %sthroughput%s(total)=%s%.2f MiB/s%s %sactual%s=%s%.2f MiB/s%s\n",
			chunk_tool_mode_style(stats->uses_gpu),
			stats->uses_gpu ? "GPU" : "CPU",
			rst,
			stats->algorithm,
			hi, rst,
			metric, chunk_tool_throughput_mib_s(stats), rst,
			hi, rst,
			metric, chunk_tool_actual_throughput_mib_s(stats), rst);
	printf("input: %s\n", input_label);
	printf("files: %zu\n", stats->file_count);
	printf("bytes: %zu\n", stats->total_bytes);
	printf("configured min/avg/max: %d/%d/%d\n",
			stats->configured_min,
			stats->configured_avg,
			stats->configured_max);
	if (stats->configured_mask_bits > 0) {
		printf("configured mask bits: %d\n", stats->configured_mask_bits);
	}
	printf("configured warp window: %d\n", stats->configured_warp_window);
	printf("chunks: %zu\n", stats->chunk_count);
	if (stats->chunk_count > 0) {
		printf("observed min/max/avg: %d/%d/%.2f\n",
				stats->observed_min,
				stats->observed_max,
				stats->observed_avg);
	}
	if (stats->profile_chunking) {
		double avg_checks = stats->experiment_stats.chunk_count > 0
				? (double)stats->experiment_stats.total_checks_per_chunk
				/ (double)stats->experiment_stats.chunk_count
				: 0.0;
		double avg_redundant_checks = stats->experiment_stats.chunk_count > 0
				? (double)stats->experiment_stats.redundant_checks
				/ (double)stats->experiment_stats.chunk_count
				: 0.0;
		double avg_cutoff_lane = stats->experiment_stats.cutoff_hits > 0
				? (double)stats->experiment_stats.cutoff_lane_sum
				/ (double)stats->experiment_stats.cutoff_hits
				: 0.0;
		double avg_tail_idle_lanes = stats->experiment_stats.cutoff_hits > 0
				? (double)stats->experiment_stats.tail_idle_lane_sum
				/ (double)stats->experiment_stats.cutoff_hits
				: 0.0;
		printf("fingerprint updates: %llu\n",
				(unsigned long long)stats->experiment_stats.fingerprint_updates);
		printf("cutoff hits: %llu\n",
				(unsigned long long)stats->experiment_stats.cutoff_hits);
		printf("jump hits/bytes skipped: %llu/%llu\n",
				(unsigned long long)stats->experiment_stats.jump_hits,
				(unsigned long long)stats->experiment_stats.jump_bytes_skipped);
		printf("redundant checks/warp groups: %llu/%llu\n",
				(unsigned long long)stats->experiment_stats.redundant_checks,
				(unsigned long long)stats->experiment_stats.simulated_warp_groups);
		printf("avg cutoff lane / tail idle lanes: %.2f / %.2f\n",
				avg_cutoff_lane,
				avg_tail_idle_lanes);
		printf("checks per chunk min/max/avg: %llu/%llu/%.2f\n",
				(unsigned long long)stats->experiment_stats.min_checks_per_chunk,
				(unsigned long long)stats->experiment_stats.max_checks_per_chunk,
				avg_checks);
		printf("avg redundant checks per chunk: %.2f\n", avg_redundant_checks);
	}
	printf("%selapsed%s: %s%.3f ms%s\n",
			hi, rst, metric, stats->elapsed_ms, rst);
	printf("%sactual%s %selapsed%s: %s%.3f ms%s\n",
			hi, rst, hi, rst, metric, chunk_tool_actual_elapsed_ms(stats), rst);
}

static void baseline_parallel_init(void) {
	int index;
	int mask_bits;

	gear_matrix_init();
	index = log2(destor.chunk_avg_size);
	assert(index > 6);
	assert(index < 17);
	mask_bits = destor.chunk_mask_bits > 0 ? destor.chunk_mask_bits : index - 1;
	assert(mask_bits > 1);
	assert(mask_bits < 17);
	g_baseline_mask = g_condition_mask[mask_bits];
}

static int baseline_parallel_chunk_data(unsigned char *p, int n) {
	uint64_t fingerprint = 0;
	int i = 0;
	int min_size = destor.chunk_min_size;
	int end;
	int warp_window = destor.chunk_warp_window;

	if (n <= 0) {
		return -1;
	}
	if (n <= min_size) {
		chunk_experiment_note_chunk_complete(n, 0);
		return n;
	}

	i = min_size;
	end = n < destor.chunk_max_size ? n : destor.chunk_max_size;
	while (i < end) {
		int pos;
		int group_end = i + warp_window < end ? i + warp_window : end;
		int first_cutoff = -1;

		for (pos = i; pos < group_end; pos++) {
			fingerprint = (fingerprint << 1) + g_gear_matrix[p[pos]];
			chunk_experiment_note_fingerprint_update();
			if (first_cutoff < 0 && !(fingerprint & g_baseline_mask)) {
				first_cutoff = pos + 1;
			}
		}

		if (first_cutoff >= 0) {
			chunk_experiment_note_redundancy(group_end - first_cutoff, 1);
			chunk_experiment_note_chunk_complete(first_cutoff, 1);
			return first_cutoff;
		}

		chunk_experiment_note_redundancy(0, 1);
		i = group_end;
	}

	chunk_experiment_note_chunk_complete(end, 0);
	return end;
}

static void merge_experiment_stats(struct chunk_experiment_stats *total,
		const struct chunk_experiment_stats *part) {
	total->fingerprint_updates += part->fingerprint_updates;
	total->chunk_count += part->chunk_count;
	total->cutoff_hits += part->cutoff_hits;
	total->jump_hits += part->jump_hits;
	total->jump_bytes_skipped += part->jump_bytes_skipped;
	total->redundant_checks += part->redundant_checks;
	total->simulated_warp_groups += part->simulated_warp_groups;
	total->cutoff_lane_sum += part->cutoff_lane_sum;
	total->tail_idle_lane_sum += part->tail_idle_lane_sum;
	total->total_chunk_bytes += part->total_chunk_bytes;
	total->total_checks_per_chunk += part->total_checks_per_chunk;
	if (part->chunk_count > 0) {
		if (total->min_checks_per_chunk == 0 || part->min_checks_per_chunk < total->min_checks_per_chunk) {
			total->min_checks_per_chunk = part->min_checks_per_chunk;
		}
		if (part->max_checks_per_chunk > total->max_checks_per_chunk) {
			total->max_checks_per_chunk = part->max_checks_per_chunk;
		}
	}
}

static void merge_stats(struct chunk_tool_stats *total, const struct chunk_tool_stats *part) {
	if (total->algorithm == NULL) {
		total->algorithm = part->algorithm;
		total->configured_min = part->configured_min;
		total->configured_avg = part->configured_avg;
		total->configured_max = part->configured_max;
		total->configured_mask_bits = part->configured_mask_bits;
		total->configured_warp_window = part->configured_warp_window;
		total->uses_gpu = part->uses_gpu;
		total->profile_chunking = part->profile_chunking;
	}

	total->file_count += part->file_count;
	total->total_bytes += part->total_bytes;
	total->chunk_count += part->chunk_count;
	total->elapsed_ms += part->elapsed_ms;
	total->actual_elapsed_ms += part->actual_elapsed_ms;
	if (part->chunk_count > 0) {
		if (total->observed_min == INT_MAX || part->observed_min < total->observed_min) {
			total->observed_min = part->observed_min;
		}
		if (part->observed_max > total->observed_max) {
			total->observed_max = part->observed_max;
		}
	}
	if (part->profile_chunking) {
		merge_experiment_stats(&total->experiment_stats, &part->experiment_stats);
	}
	finalize_stats(total);
}

static void merge_stats_counts(struct chunk_tool_stats *total, const struct chunk_tool_stats *part) {
	if (total->algorithm == NULL) {
		total->algorithm = part->algorithm;
		total->configured_min = part->configured_min;
		total->configured_avg = part->configured_avg;
		total->configured_max = part->configured_max;
		total->configured_mask_bits = part->configured_mask_bits;
		total->configured_warp_window = part->configured_warp_window;
		total->uses_gpu = part->uses_gpu;
		total->profile_chunking = part->profile_chunking;
	}

	total->file_count += part->file_count;
	total->total_bytes += part->total_bytes;
	total->chunk_count += part->chunk_count;
	if (part->chunk_count > 0) {
		if (total->observed_min == INT_MAX || part->observed_min < total->observed_min) {
			total->observed_min = part->observed_min;
		}
		if (part->observed_max > total->observed_max) {
			total->observed_max = part->observed_max;
		}
	}
	if (part->profile_chunking) {
		merge_experiment_stats(&total->experiment_stats, &part->experiment_stats);
	}
}

struct file_parallel_pool {
	const struct chunk_tool_options *options;
	const struct chunk_tool_path_list *paths;
	struct chunk_tool_run run;
	int uses_gpu;
	pthread_mutex_t lock;
	int next_file;
	int completed_files;
	int rc;
	struct chunk_tool_parallel_wall compute_wall;
	struct chunk_tool_stats aggregate;
};

static int chunk_tool_gpu_naive_supported(const char *algorithm) {
	return algorithm
			&& (strcmp(algorithm, "fastcdc") == 0
					|| strcmp(algorithm, "gear") == 0
					|| strcmp(algorithm, "gearjump") == 0
					|| strcmp(algorithm, "jc") == 0);
}

static int gpu_naive_worker_init_cuda(struct chunk_tool_run *run,
		const struct chunk_tool_options *options) {
	fastcdc_gpu_set_naive_mode(1);
	if (strcmp(options->algorithm, "gear") == 0) {
		fastcdc_gpu_set_naive_algorithm(1);
		gear_init();
		if (gear_gpu_init() != 0) {
			return -1;
		}
		run->chunk_fn = gear_gpu_chunk_data;
		run->close_fn = gear_gpu_close;
	} else if (strcmp(options->algorithm, "gearjump") == 0
			|| strcmp(options->algorithm, "jc") == 0) {
		fastcdc_gpu_set_naive_algorithm(2);
		gearjump_init(options->jump_mask_delta);
		if (jc_gpu_init() != 0) {
			return -1;
		}
		run->chunk_fn = jc_gpu_chunk_data;
		run->close_fn = jc_gpu_close;
	} else {
		fastcdc_gpu_set_naive_algorithm(0);
		fastcdc_init();
		if (fastcdc_gpu_init() != 0) {
			return -1;
		}
		run->chunk_fn = fastcdc_gpu_chunk_data;
		run->close_fn = fastcdc_gpu_close;
	}
	run->uses_gpu = 1;
	return 0;
}

static void *gpu_naive_file_worker_main(void *arg) {
	struct file_parallel_pool *pool = arg;
	struct chunk_tool_run run = pool->run;

	if (gpu_naive_worker_init_cuda(&run, pool->options) != 0) {
		pthread_mutex_lock(&pool->lock);
		pool->rc = 2;
		pthread_mutex_unlock(&pool->lock);
		return NULL;
	}

	for (;;) {
		int file_index;
		unsigned char *buffer = NULL;
		size_t buffer_size = 0;
		int buffer_is_mmap = 0;
		struct chunk_tool_stats file_stats;
		int chunk_rc;

		pthread_mutex_lock(&pool->lock);
		file_index = pool->next_file++;
		pthread_mutex_unlock(&pool->lock);
		if (file_index >= pool->paths->count) {
			break;
		}

		if (read_input_file(pool->paths->items[file_index],
					1,
					&buffer,
					&buffer_size,
					&buffer_is_mmap) != 0) {
			pthread_mutex_lock(&pool->lock);
			pool->rc = 2;
			pthread_mutex_unlock(&pool->lock);
			break;
		}

		init_stats(&file_stats);
		{
			struct timespec compute_start;
			struct timespec compute_end;

			clock_gettime(CLOCK_MONOTONIC, &compute_start);
			chunk_rc = run_chunking(pool->options, &run, buffer, buffer_size, &file_stats);
			clock_gettime(CLOCK_MONOTONIC, &compute_end);
			if (chunk_rc == 0) {
				chunk_tool_parallel_wall_merge(&pool->compute_wall,
						&compute_start,
						&compute_end);
			}
		}
		release_input_file(buffer, buffer_size, buffer_is_mmap);
		if (chunk_rc != 0) {
			pthread_mutex_lock(&pool->lock);
			pool->rc = 2;
			pthread_mutex_unlock(&pool->lock);
			break;
		}

		pthread_mutex_lock(&pool->lock);
		merge_stats_counts(&pool->aggregate, &file_stats);
		pool->completed_files++;
		if (pool->paths->count > 1
				&& (pool->completed_files == 1
						|| pool->completed_files == pool->paths->count
						|| pool->completed_files % 50 == 0)) {
			fprintf(stderr, "\r%s[%s]%s %s file %d/%d (%.1f%%)  ",
					chunk_tool_stderr_mode_style(1),
					"GPU",
					chunk_tool_stderr_reset_style(),
					pool->options->algorithm,
					pool->completed_files,
					pool->paths->count,
					100.0 * (double)pool->completed_files
							/ (double)pool->paths->count);
			fflush(stderr);
		}
		pthread_mutex_unlock(&pool->lock);
	}

	if (run.close_fn) {
		run.close_fn();
	}
	return NULL;
}

static void *cpu_parallel_file_worker_main(void *arg) {
	struct file_parallel_pool *pool = arg;
	struct chunk_tool_run run = pool->run;

	for (;;) {
		int file_index;
		unsigned char *buffer = NULL;
		size_t buffer_size = 0;
		int buffer_is_mmap = 0;
		struct chunk_tool_stats file_stats;
		int chunk_rc;

		pthread_mutex_lock(&pool->lock);
		file_index = pool->next_file++;
		pthread_mutex_unlock(&pool->lock);
		if (file_index >= pool->paths->count) {
			break;
		}

		if (read_input_file(pool->paths->items[file_index],
					0,
					&buffer,
					&buffer_size,
					&buffer_is_mmap) != 0) {
			pthread_mutex_lock(&pool->lock);
			pool->rc = 2;
			pthread_mutex_unlock(&pool->lock);
			break;
		}

		init_stats(&file_stats);
		{
			struct timespec compute_start;
			struct timespec compute_end;

			clock_gettime(CLOCK_MONOTONIC, &compute_start);
			chunk_rc = run_chunking(pool->options, &run, buffer, buffer_size, &file_stats);
			clock_gettime(CLOCK_MONOTONIC, &compute_end);
			if (chunk_rc == 0) {
				chunk_tool_parallel_wall_merge(&pool->compute_wall, &compute_start, &compute_end);
			}
		}
		release_input_file(buffer, buffer_size, buffer_is_mmap);
		if (chunk_rc != 0) {
			pthread_mutex_lock(&pool->lock);
			pool->rc = 2;
			pthread_mutex_unlock(&pool->lock);
			break;
		}

		pthread_mutex_lock(&pool->lock);
		merge_stats_counts(&pool->aggregate, &file_stats);
		pool->completed_files++;
		if (pool->paths->count > 1
				&& (pool->completed_files == 1
						|| pool->completed_files == pool->paths->count
						|| pool->completed_files % 50 == 0)) {
			fprintf(stderr, "\r%s[%s]%s %s file %d/%d (%.1f%%)  ",
					chunk_tool_stderr_mode_style(0),
					"CPU",
					chunk_tool_stderr_reset_style(),
					pool->options->algorithm,
					pool->completed_files,
					pool->paths->count,
					100.0 * (double)pool->completed_files
							/ (double)pool->paths->count);
			fflush(stderr);
		}
		pthread_mutex_unlock(&pool->lock);
	}

	return NULL;
}

static int execute_path_run_gpu_naive_parallel(const struct chunk_tool_options *options,
		const struct chunk_tool_path_list *paths,
		struct chunk_tool_stats *stats) {
	struct file_parallel_pool pool;
	struct timespec start_time;
	struct timespec end_time;
	pthread_t *threads = NULL;
	int worker_count;
	int i;

	memset(&pool, 0, sizeof(pool));
	pool.options = options;
	pool.paths = paths;
	pool.uses_gpu = 1;
	init_stats(&pool.aggregate);
	pthread_mutex_init(&pool.lock, NULL);
	chunk_tool_parallel_wall_init(&pool.compute_wall);
	reset_destor_for_run(options);
	if (select_algorithm(options, &pool.run) != 0) {
		chunk_tool_parallel_wall_destroy(&pool.compute_wall);
		pthread_mutex_destroy(&pool.lock);
		return 2;
	}
	if (!pool.run.uses_gpu) {
		chunk_tool_parallel_wall_destroy(&pool.compute_wall);
		pthread_mutex_destroy(&pool.lock);
		return 2;
	}
	if (pool.run.close_fn) {
		pool.run.close_fn();
	}

	worker_count = chunk_tool_gpu_naive_workers(paths->count);
	threads = calloc((size_t)worker_count, sizeof(*threads));
	if (!threads) {
		chunk_tool_parallel_wall_destroy(&pool.compute_wall);
		pthread_mutex_destroy(&pool.lock);
		return 2;
	}

	fprintf(stderr,
			"[GPU-Naive] one thread per active file, workers=%d files=%d (per-thread CUDA context, serial FastCDC kernel)\n",
			worker_count,
			paths->count);

	chunk_tool_compute_reset(CHUNK_TOOL_COMPUTE_GPU_NAIVE);
	clock_gettime(CLOCK_MONOTONIC, &start_time);
	for (i = 0; i < worker_count; i++) {
		if (pthread_create(&threads[i], NULL, gpu_naive_file_worker_main, &pool) != 0) {
			pool.rc = 2;
			break;
		}
	}
	for (i = 0; i < worker_count; i++) {
		pthread_join(threads[i], NULL);
	}
	clock_gettime(CLOCK_MONOTONIC, &end_time);

	if (paths->count > 1) {
		fprintf(stderr, "\n");
		fflush(stderr);
	}

	free(threads);
	chunk_tool_parallel_wall_destroy(&pool.compute_wall);
	pthread_mutex_destroy(&pool.lock);

	if (pool.rc != 0) {
		return pool.rc;
	}

	*stats = pool.aggregate;
	{
		double kernel_ms = chunk_tool_compute_read_ms(CHUNK_TOOL_COMPUTE_GPU_NAIVE);

		(void)start_time;
		(void)end_time;
		if (kernel_ms <= 0.0) {
			kernel_ms = chunk_tool_parallel_wall_ms(&pool.compute_wall);
		}
		chunk_tool_timing_assign(&stats->elapsed_ms,
				&stats->actual_elapsed_ms,
				kernel_ms,
				CHUNK_TOOL_COMPUTE_GPU_NAIVE,
				0.0);
	}
	finalize_stats(stats);
	return 0;
}

static int chunk_tool_cpu_parallel_eligible(const struct chunk_tool_options *options) {
	if (!options || options->gpu_enabled) {
		return 0;
	}
	if (strcmp(options->algorithm, "baseline") == 0) {
		return 1;
	}
	if (!options->cpu_parallel) {
		return 0;
	}
	return strcmp(options->algorithm, "fastcdc") == 0
			|| strcmp(options->algorithm, "gear") == 0
			|| strcmp(options->algorithm, "gearjump") == 0
			|| strcmp(options->algorithm, "jc") == 0;
}

static int execute_path_run_cpu_parallel(const struct chunk_tool_options *options,
		const struct chunk_tool_path_list *paths,
		struct chunk_tool_stats *stats) {
	struct file_parallel_pool pool;
	struct timespec start_time;
	struct timespec end_time;
	pthread_t *threads = NULL;
	int worker_count;
	int i;

	memset(&pool, 0, sizeof(pool));
	pool.options = options;
	pool.paths = paths;
	init_stats(&pool.aggregate);
	pthread_mutex_init(&pool.lock, NULL);
	chunk_tool_parallel_wall_init(&pool.compute_wall);
	reset_destor_for_run(options);
	if (select_algorithm(options, &pool.run) != 0) {
		chunk_tool_parallel_wall_destroy(&pool.compute_wall);
		pthread_mutex_destroy(&pool.lock);
		return 2;
	}

	worker_count = chunk_tool_cpu_parallel_workers(paths->count);
	threads = calloc((size_t)worker_count, sizeof(*threads));
	if (!threads) {
		if (pool.run.close_fn) {
			pool.run.close_fn();
		}
		chunk_tool_parallel_wall_destroy(&pool.compute_wall);
		pthread_mutex_destroy(&pool.lock);
		return 2;
	}

	fprintf(stderr,
			"[CPU-Parallel] file workers=%d files=%d (%s)\n",
			worker_count,
			paths->count,
			pool.run.display_name ? pool.run.display_name : options->algorithm);

	clock_gettime(CLOCK_MONOTONIC, &start_time);
	for (i = 0; i < worker_count; i++) {
		if (pthread_create(&threads[i], NULL, cpu_parallel_file_worker_main, &pool) != 0) {
			pool.rc = 2;
			break;
		}
	}
	for (i = 0; i < worker_count; i++) {
		pthread_join(threads[i], NULL);
	}
	clock_gettime(CLOCK_MONOTONIC, &end_time);

	if (paths->count > 1) {
		fprintf(stderr, "\n");
		fflush(stderr);
	}

	free(threads);
	chunk_tool_parallel_wall_destroy(&pool.compute_wall);
	pthread_mutex_destroy(&pool.lock);
	if (pool.run.close_fn) {
		pool.run.close_fn();
	}

	if (pool.rc != 0) {
		return pool.rc;
	}

	*stats = pool.aggregate;
	chunk_tool_timing_assign(&stats->elapsed_ms,
			&stats->actual_elapsed_ms,
			chunk_tool_parallel_wall_ms(&pool.compute_wall),
			CHUNK_TOOL_COMPUTE_CPU,
			chunk_tool_parallel_wall_ms(&pool.compute_wall));
	finalize_stats(stats);
	return 0;
}

static int run_chunking(const struct chunk_tool_options *options,
		const struct chunk_tool_run *run,
		unsigned char *buffer,
		size_t buffer_size,
		struct chunk_tool_stats *stats) {
	int segment_bytes = fastcdc_gpu_segment_bytes();
	int boundary_stride = fastcdc_gpu_segment_boundary_limit(segment_bytes);
	size_t offset = 0;
	size_t chunk_count = 0;
	size_t printed = 0;
	unsigned long long total_chunk_bytes = 0;
	int observed_min = INT_MAX;
	int observed_max = 0;
	struct chunk_experiment_stats experiment_stats;
	chunk_tool_compute_kind compute_kind;

	compute_kind = chunk_tool_compute_kind_for(run->uses_gpu, options->gpu_naive);
	chunk_tool_compute_reset(compute_kind);
	chunk_experiment_reset_stats();

        while (offset < buffer_size) {
        	size_t remaining = buffer_size - offset;

		if (run->chunk_segment_batch_fn) {
			unsigned char *segment_buffers[1];
			int segment_sizes[1];
			int boundary_counts[1];
			int segment_chunk_sizes[boundary_stride];
			int j;

			segment_buffers[0] = buffer + offset;
			segment_sizes[0] = chunk_tool_invoke_len(remaining);
			if (run->chunk_segment_batch_fn(segment_buffers,
						segment_sizes,
						1,
						boundary_stride,
						boundary_counts,
						segment_chunk_sizes) != 0) {
				return -1;
			}
			for (j = 0; j < boundary_counts[0]; j++) {
				int chunk_size = segment_chunk_sizes[j];

				if (chunk_size <= 0 || (size_t)chunk_size > remaining) {
					CHUNK_TOOL_ERROR("Invalid chunk size %d at offset %zu, remaining=%zu",
							chunk_size,
							offset,
							remaining);
					return -1;
				}
				if (chunk_size < observed_min) {
					observed_min = chunk_size;
				}
				if (chunk_size > observed_max) {
					observed_max = chunk_size;
				}
				total_chunk_bytes += (unsigned long long)chunk_size;
				chunk_count++;
				if (options->print_chunks && printed < (size_t)options->print_limit) {
					printf("chunk[%zu] start=%zu end=%zu size=%d\n",
							chunk_count - 1,
							offset,
							offset + (size_t)chunk_size,
							chunk_size);
					printed++;
				}
				offset += (size_t)chunk_size;
				remaining -= chunk_size;
			}
            } else {
            	int chunk_size = run->chunk_fn(buffer + offset, chunk_tool_invoke_len(remaining));

            	if (chunk_size <= 0 || (size_t)chunk_size > remaining) {
                	CHUNK_TOOL_ERROR("Invalid chunk size %d at offset %zu, remaining=%zu",
                        	chunk_size,
                        	offset,
                        	remaining);
                	return -1;
            	}

			if (chunk_size < observed_min) {
				observed_min = chunk_size;
			}
			if (chunk_size > observed_max) {
				observed_max = chunk_size;
			}

			total_chunk_bytes += (unsigned long long)chunk_size;
			chunk_count++;

			if (options->print_chunks && printed < (size_t)options->print_limit) {
				printf("chunk[%zu] start=%zu end=%zu size=%d\n",
						chunk_count - 1,
						offset,
						offset + (size_t)chunk_size,
						chunk_size);
				printed++;
			}

			offset += (size_t)chunk_size;
		}
		if (g_chunk_tool_progress.active && chunk_tool_progress_should_refresh()) {
			chunk_tool_progress_render(offset);
		}
	}

	chunk_experiment_snapshot(&experiment_stats);
	stats->algorithm = run->display_name;
	stats->configured_min = destor.chunk_min_size;
	stats->configured_avg = run->effective_avg_size;
	stats->configured_max = destor.chunk_max_size;
	stats->configured_mask_bits = destor.chunk_mask_bits;
	stats->configured_warp_window = destor.chunk_warp_window;
	stats->file_count = 1;
	stats->total_bytes = buffer_size;
	stats->chunk_count = chunk_count;
	stats->observed_min = chunk_count > 0 ? observed_min : 0;
	stats->observed_max = chunk_count > 0 ? observed_max : 0;
	stats->observed_avg = chunk_count > 0
			? (double)total_chunk_bytes / (double)chunk_count
			: 0.0;
	chunk_tool_timing_assign(&stats->elapsed_ms,
			&stats->actual_elapsed_ms,
			0.0,
			compute_kind,
			0.0);
	stats->uses_gpu = run->uses_gpu;
	stats->profile_chunking = options->profile_chunking;
	if (options->profile_chunking) {
		stats->experiment_stats = experiment_stats;
	}
	if (options->print_chunks && chunk_count > (size_t)options->print_limit) {
		printf("printed first %d of %zu chunks\n", options->print_limit, chunk_count);
	}

	return 0;
}

static int run_chunking_batch(const struct chunk_tool_options *options,
		const struct chunk_tool_run *run,
		const struct chunk_tool_path_list *paths,
		struct chunk_tool_stats *stats) {
	int segment_bytes = fastcdc_gpu_segment_bytes();
	int boundary_stride = fastcdc_gpu_segment_boundary_limit(segment_bytes);
	int batch_start;
	size_t printed = 0;

	init_stats(stats);
	stats->algorithm = run->display_name;
	stats->configured_min = destor.chunk_min_size;
	stats->configured_avg = run->effective_avg_size;
	stats->configured_max = destor.chunk_max_size;
	stats->configured_mask_bits = destor.chunk_mask_bits;
	stats->configured_warp_window = destor.chunk_warp_window;
	stats->uses_gpu = run->uses_gpu;
	stats->profile_chunking = options->profile_chunking;
	if (run->uses_gpu) {
		chunk_tool_compute_reset(CHUNK_TOOL_COMPUTE_GPU_BATCH);
	}

	for (batch_start = 0; batch_start < paths->count; batch_start += CHUNK_TOOL_MAX_BATCH) {
		int batch_count = paths->count - batch_start;
		unsigned char *buffers[CHUNK_TOOL_MAX_BATCH];
		size_t buffer_sizes[CHUNK_TOOL_MAX_BATCH];
		int buffer_is_mmap[CHUNK_TOOL_MAX_BATCH];
		size_t offsets[CHUNK_TOOL_MAX_BATCH];
		int done[CHUNK_TOOL_MAX_BATCH];
		size_t batch_total_bytes = 0;
		unsigned long long total_chunk_bytes = 0;
		size_t chunk_count = 0;
		int observed_min = INT_MAX;
		int observed_max = 0;
		struct chunk_experiment_stats experiment_stats;
		int i;

		if (batch_count > CHUNK_TOOL_MAX_BATCH) {
			batch_count = CHUNK_TOOL_MAX_BATCH;
		}
		memset(buffers, 0, sizeof(buffers));
		memset(buffer_sizes, 0, sizeof(buffer_sizes));
		memset(buffer_is_mmap, 0, sizeof(buffer_is_mmap));
		memset(offsets, 0, sizeof(offsets));
		memset(done, 0, sizeof(done));

		for (i = 0; i < batch_count; i++) {
			if (read_input_file(paths->items[batch_start + i],
						run->uses_gpu,
						&buffers[i],
						&buffer_sizes[i],
						&buffer_is_mmap[i]) != 0) {
				int j;
				for (j = 0; j < batch_count; j++) {
					release_input_file(buffers[j], buffer_sizes[j], buffer_is_mmap[j]);
				}
				return -1;
			}
			batch_total_bytes += buffer_sizes[i];
		}

		chunk_experiment_reset_stats();
		chunk_tool_progress_begin(options,
				run,
				paths,
				batch_start,
				batch_total_bytes,
				g_chunk_tool_progress.completed_bytes);

		while (1) {
			unsigned char *active_buffers[CHUNK_TOOL_MAX_BATCH];
			int active_sizes[CHUNK_TOOL_MAX_BATCH];
			int chunk_sizes[CHUNK_TOOL_MAX_BATCH * boundary_stride];
			int boundary_counts[CHUNK_TOOL_MAX_BATCH];
			int active_index[CHUNK_TOOL_MAX_BATCH];
			size_t current_batch_offset = 0;
			int active_count = 0;

			for (i = 0; i < batch_count; i++) {
				if (done[i]) {
					current_batch_offset += buffer_sizes[i];
					continue;
				}
				current_batch_offset += offsets[i];
				if (offsets[i] < buffer_sizes[i]) {
					active_buffers[active_count] = buffers[i] + offsets[i];
					active_sizes[active_count] = chunk_tool_invoke_len(buffer_sizes[i] - offsets[i]);
					active_index[active_count] = i;
					active_count++;
				}
			}

			if (active_count == 0) {
				break;
			}

			if (run->chunk_segment_batch_fn) {
				if (run->chunk_segment_batch_fn(active_buffers,
							active_sizes,
							active_count,
							boundary_stride,
							boundary_counts,
							chunk_sizes) != 0) {
					for (i = 0; i < batch_count; i++) {
						release_input_file(buffers[i], buffer_sizes[i], buffer_is_mmap[i]);
					}
					chunk_tool_progress_clear();
					return -1;
				}
			} else if (run->chunk_batch_fn(active_buffers, active_sizes, active_count, chunk_sizes) != 0) {
				for (i = 0; i < batch_count; i++) {
					release_input_file(buffers[i], buffer_sizes[i], buffer_is_mmap[i]);
				}
				chunk_tool_progress_clear();
				return -1;
			}

			for (i = 0; i < active_count; i++) {
				int file_index = active_index[i];
				int local_count = run->chunk_segment_batch_fn ? boundary_counts[i] : 1;
				int j;

				for (j = 0; j < local_count; j++) {
					int chunk_size = chunk_sizes[i * boundary_stride + j];
					size_t remaining = buffer_sizes[file_index] - offsets[file_index];

					if (chunk_size <= 0 || (size_t)chunk_size > remaining) {
						CHUNK_TOOL_ERROR("Invalid chunk size %d for file %s, remaining=%zu",
								chunk_size,
								paths->items[batch_start + file_index],
								remaining);
						for (file_index = 0; file_index < batch_count; file_index++) {
							release_input_file(buffers[file_index],
									buffer_sizes[file_index],
									buffer_is_mmap[file_index]);
						}
						chunk_tool_progress_clear();
						return -1;
					}

					if (chunk_size < observed_min) {
						observed_min = chunk_size;
					}
					if (chunk_size > observed_max) {
						observed_max = chunk_size;
					}
					total_chunk_bytes += (unsigned long long)chunk_size;
					chunk_count++;
					if (options->print_chunks && printed < (size_t)options->print_limit) {
						printf("%s chunk[%zu] start=%zu end=%zu size=%d\n",
								paths->items[batch_start + active_index[i]],
								printed,
								offsets[active_index[i]],
								offsets[active_index[i]] + (size_t)chunk_size,
								chunk_size);
						printed++;
					}
					offsets[active_index[i]] += (size_t)chunk_size;
				}
				if (offsets[active_index[i]] >= buffer_sizes[active_index[i]]) {
					done[active_index[i]] = 1;
				}
			}

			if (g_chunk_tool_progress.active && chunk_tool_progress_should_refresh()) {
				size_t total_done = 0;
				for (i = 0; i < batch_count; i++) {
					total_done += offsets[i];
				}
				chunk_tool_progress_render(total_done);
			}
		}

		chunk_experiment_snapshot(&experiment_stats);
		stats->file_count += (size_t)batch_count;
		stats->total_bytes += batch_total_bytes;
		stats->chunk_count += chunk_count;
		if (chunk_count > 0) {
			if (stats->observed_min == INT_MAX || observed_min < stats->observed_min) {
				stats->observed_min = observed_min;
			}
			if (observed_max > stats->observed_max) {
				stats->observed_max = observed_max;
			}
			stats->observed_avg = (double)(stats->observed_avg * (double)(stats->chunk_count - chunk_count)
					+ (double)total_chunk_bytes) / (double)stats->chunk_count;
		}
		if (options->profile_chunking) {
			merge_experiment_stats(&stats->experiment_stats, &experiment_stats);
		}
		chunk_tool_progress_finish_file(batch_total_bytes);

		for (i = 0; i < batch_count; i++) {
			release_input_file(buffers[i], buffer_sizes[i], buffer_is_mmap[i]);
		}
	}

	chunk_tool_progress_clear();
	if (run->uses_gpu) {
		chunk_tool_timing_assign(&stats->elapsed_ms,
				&stats->actual_elapsed_ms,
				0.0,
				CHUNK_TOOL_COMPUTE_GPU_BATCH,
				0.0);
	}
	finalize_stats(stats);
	if (options->print_chunks && stats->chunk_count > (size_t)options->print_limit) {
		printf("printed first %d of %zu chunks\n", options->print_limit, stats->chunk_count);
	}
	return 0;
}

static void print_batch_summary(const struct chunk_tool_stats *stats, int count) {
	int i;

	printf("\nsummary:\n");
	printf("%s%-10s%s %-20s %s%-14s%s %s%-14s%s %-8s %-8s %-12s %-16s %-12s %s%-12s%s\n",
			chunk_tool_highlight_style(), "mode", chunk_tool_reset_style(),
			"algorithm",
			chunk_tool_highlight_style(), "MiB/s(total)", chunk_tool_reset_style(),
			chunk_tool_highlight_style(), "MiB/s(actual)", chunk_tool_reset_style(),
			"files",
			"chunks",
			"cfg(avg)",
			"obs(min/max/avg)",
			"elapsed(ms)",
			chunk_tool_highlight_style(), "actual(ms)", chunk_tool_reset_style());
	for (i = 0; i < count; i++) {
		printf("%s%-10s%s %-20s %s%-14.2f%s %s%-14.2f%s %-8zu %-8zu %-12d %d/%d/%.2f %12.3f %s%12.3f%s\n",
				chunk_tool_mode_style(stats[i].uses_gpu),
				stats[i].uses_gpu ? "GPU" : "CPU",
				chunk_tool_reset_style(),
				stats[i].algorithm,
				chunk_tool_metric_style(),
				chunk_tool_throughput_mib_s(&stats[i]),
				chunk_tool_reset_style(),
				chunk_tool_metric_style(),
				chunk_tool_actual_throughput_mib_s(&stats[i]),
				chunk_tool_reset_style(),
				stats[i].file_count,
				stats[i].chunk_count,
				stats[i].configured_avg,
				stats[i].observed_min,
				stats[i].observed_max,
				stats[i].observed_avg,
				stats[i].elapsed_ms,
				chunk_tool_metric_style(),
				chunk_tool_actual_elapsed_ms(&stats[i]),
				chunk_tool_reset_style());
	}
}

static int append_stats_csv(const char *path,
		const char *input_label,
		const struct chunk_tool_stats *stats) {
	FILE *fp;
	long file_size;
	double avg_checks = 0.0;

	if (!path || !stats) {
		return 0;
	}

	fp = fopen(path, "a+");
	if (!fp) {
		CHUNK_TOOL_ERROR("Failed to open CSV output %s: %s", path, strerror(errno));
		return -1;
	}
	if (fseek(fp, 0, SEEK_END) != 0) {
		CHUNK_TOOL_ERROR("Failed to seek CSV output %s: %s", path, strerror(errno));
		fclose(fp);
		return -1;
	}
	file_size = ftell(fp);
	if (file_size < 0) {
		CHUNK_TOOL_ERROR("Failed to inspect CSV output %s: %s", path, strerror(errno));
		fclose(fp);
		return -1;
	}
	if (file_size == 0) {
		fprintf(fp,
				"algorithm,input,mode,files,bytes,cfg_min,cfg_avg,cfg_max,mask_bits,warp_window,chunks,obs_min,obs_max,obs_avg,elapsed_ms,actual_elapsed_ms,actual_throughput_mib_s,fingerprint_updates,cutoff_hits,jump_hits,jump_bytes_skipped,redundant_checks,warp_groups,cutoff_lane_sum,tail_idle_lane_sum,min_checks,max_checks,avg_checks\n");
	}
	if (stats->experiment_stats.chunk_count > 0) {
		avg_checks = (double)stats->experiment_stats.total_checks_per_chunk
				/ (double)stats->experiment_stats.chunk_count;
	}
	fprintf(fp,
			"%s,%s,%s,%zu,%zu,%d,%d,%d,%d,%d,%zu,%d,%d,%.2f,%.3f,%.3f,%.2f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%.2f\n",
			stats->algorithm,
			input_label,
			stats->uses_gpu ? "gpu" : "cpu",
			stats->file_count,
			stats->total_bytes,
			stats->configured_min,
			stats->configured_avg,
			stats->configured_max,
			stats->configured_mask_bits,
			stats->configured_warp_window,
			stats->chunk_count,
			stats->observed_min,
			stats->observed_max,
			stats->observed_avg,
			stats->elapsed_ms,
			chunk_tool_actual_elapsed_ms(stats),
			chunk_tool_actual_throughput_mib_s(stats),
			(unsigned long long)stats->experiment_stats.fingerprint_updates,
			(unsigned long long)stats->experiment_stats.cutoff_hits,
			(unsigned long long)stats->experiment_stats.jump_hits,
			(unsigned long long)stats->experiment_stats.jump_bytes_skipped,
			(unsigned long long)stats->experiment_stats.redundant_checks,
			(unsigned long long)stats->experiment_stats.simulated_warp_groups,
			(unsigned long long)stats->experiment_stats.cutoff_lane_sum,
			(unsigned long long)stats->experiment_stats.tail_idle_lane_sum,
			(unsigned long long)stats->experiment_stats.min_checks_per_chunk,
			(unsigned long long)stats->experiment_stats.max_checks_per_chunk,
			avg_checks);
	if (fclose(fp) != 0) {
		CHUNK_TOOL_ERROR("Failed to close CSV output %s: %s", path, strerror(errno));
		return -1;
	}
	return 0;
}

static void reset_destor_for_run(const struct chunk_tool_options *options) {
	destor.chunk_algorithm = -1;
	destor.chunk_min_size = options->chunk_min_size;
	destor.chunk_avg_size = options->chunk_avg_size;
	destor.chunk_max_size = options->chunk_max_size;
	destor.chunk_mask_bits = options->chunk_mask_bits;
	destor.chunk_warp_window = options->chunk_warp_window;
	destor.chunk_profile_enabled = options->profile_chunking;
	destor.jumpOnes = options->jump_mask_delta;
	destor.chunk_gpu_enable = options->gpu_enabled;
	destor.chunk_gpu_device_id = options->gpu_device_id;
	destor.chunk_gpu_batch_size = options->gpu_batch_size;
	destor.chunk_gpu_is_active = 0;
	fastcdc_gpu_set_naive_mode(options->gpu_naive);
	if (!options->gpu_naive) {
		fastcdc_gpu_set_threads_per_block(options->gpu_threads_per_block);
		fastcdc_gpu_set_pipeline_tasks(options->gpu_pipeline_tasks);
	}
}

static int execute_path_run(const struct chunk_tool_options *options,
		const struct chunk_tool_path_list *paths,
		struct chunk_tool_stats *stats,
		int show_separator) {
	int i;
	struct chunk_tool_run run;

	init_stats(stats);
	reset_destor_for_run(options);
	if (show_separator) {
		printf("\n==== %s ====\n", options->algorithm);
	}

	if (paths->count > 1 && chunk_tool_cpu_parallel_eligible(options)) {
		return execute_path_run_cpu_parallel(options, paths, stats);
	}

	if (options->gpu_naive
			&& options->gpu_enabled
			&& chunk_tool_gpu_naive_supported(options->algorithm)) {
		return execute_path_run_gpu_naive_parallel(options, paths, stats);
	}

	if (select_algorithm(options, &run) != 0) {
		return 2;
	}
	if (paths->count == 0) {
		stats->algorithm = run.display_name;
		stats->configured_min = destor.chunk_min_size;
		stats->configured_avg = run.effective_avg_size;
		stats->configured_max = destor.chunk_max_size;
		stats->configured_mask_bits = destor.chunk_mask_bits;
		stats->configured_warp_window = destor.chunk_warp_window;
		stats->uses_gpu = run.uses_gpu;
		stats->observed_min = 0;
		if (run.close_fn) {
			run.close_fn();
		}
		finalize_stats(stats);
		return 0;
	}
	if (run.uses_gpu && run.chunk_batch_fn) {
		int rc = run_chunking_batch(options, &run, paths, stats);
		if (run.close_fn) {
			run.close_fn();
		}
		return rc;
	}

	{
		struct chunk_tool_parallel_wall compute_wall;

		chunk_tool_parallel_wall_init(&compute_wall);
		for (i = 0; i < paths->count; i++) {
			unsigned char *buffer = NULL;
			size_t buffer_size = 0;
			int buffer_is_mmap = 0;
			struct chunk_tool_stats file_stats;
			struct timespec compute_start;
			struct timespec compute_end;
			int rc;

			if (read_input_file(paths->items[i], run.uses_gpu, &buffer, &buffer_size, &buffer_is_mmap) != 0) {
				release_input_file(buffer, buffer_size, buffer_is_mmap);
				chunk_tool_parallel_wall_destroy(&compute_wall);
				if (run.close_fn) {
					run.close_fn();
				}
				chunk_tool_progress_clear();
				return 2;
			}
			chunk_tool_progress_begin(options,
					&run,
					paths,
					i,
					buffer_size,
					g_chunk_tool_progress.completed_bytes);
			init_stats(&file_stats);
			clock_gettime(CLOCK_MONOTONIC, &compute_start);
			rc = run_chunking(options, &run, buffer, buffer_size, &file_stats) == 0 ? 0 : 1;
			clock_gettime(CLOCK_MONOTONIC, &compute_end);
			if (rc == 0) {
				chunk_tool_parallel_wall_merge(&compute_wall, &compute_start, &compute_end);
			}
			chunk_tool_progress_finish_file(buffer_size);
			release_input_file(buffer, buffer_size, buffer_is_mmap);
			if (rc != 0) {
				chunk_tool_parallel_wall_destroy(&compute_wall);
				if (run.close_fn) {
					run.close_fn();
				}
				chunk_tool_progress_clear();
				return rc;
			}
			merge_stats_counts(stats, &file_stats);
		}
		{
			double cpu_wall_ms;

			cpu_wall_ms = chunk_tool_parallel_wall_ms(&compute_wall);
			chunk_tool_parallel_wall_destroy(&compute_wall);
			chunk_tool_timing_assign(&stats->elapsed_ms,
					&stats->actual_elapsed_ms,
					cpu_wall_ms,
					CHUNK_TOOL_COMPUTE_CPU,
					cpu_wall_ms);
		}
	}
	if (run.close_fn) {
		run.close_fn();
	}
	chunk_tool_progress_clear();

	finalize_stats(stats);
	return 0;
}

static int select_algorithm(const struct chunk_tool_options *options, struct chunk_tool_run *run) {
	memset(run, 0, sizeof(*run));
	run->effective_avg_size = destor.chunk_avg_size;

	if (options->gpu_naive && !chunk_tool_gpu_naive_supported(options->algorithm)) {
		CHUNK_TOOL_ERROR("--gpu-naive is only supported with fastcdc, gear, and gearjump");
		return -1;
	}

	if (options->gpu_enabled
			&& strcmp(options->algorithm, "fastcdc") != 0
			&& strcmp(options->algorithm, "jc") != 0
			&& strcmp(options->algorithm, "gearjump") != 0
			&& strcmp(options->algorithm, "gear") != 0) {
		CHUNK_TOOL_ERROR("Algorithm %s does not support --gpu", options->algorithm);
		return -1;
	}

	if (strcmp(options->algorithm, "rabin") == 0) {
		run->effective_avg_size = normalize_power_of_two(destor.chunk_avg_size);
		destor.chunk_avg_size = run->effective_avg_size;
		chunkAlg_init();
		run->chunk_fn = rabin_chunk_data;
		run->display_name = "rabin";
	} else if (strcmp(options->algorithm, "normalized-rabin") == 0 || strcmp(options->algorithm, "nr-rabin") == 0) {
		run->effective_avg_size = normalize_power_of_two(destor.chunk_avg_size);
		destor.chunk_avg_size = run->effective_avg_size;
		chunkAlg_init();
		run->chunk_fn = normalized_rabin_chunk_data;
		run->display_name = "normalized-rabin";
	} else if (strcmp(options->algorithm, "rabin-jump") == 0 || strcmp(options->algorithm, "rabinjump") == 0) {
		run->effective_avg_size = normalize_power_of_two(destor.chunk_avg_size);
		destor.chunk_avg_size = run->effective_avg_size;
		rabinJump_init(destor.chunk_avg_size);
		run->chunk_fn = rabinjump_chunk_data;
		run->display_name = "rabin-jump";
	} else if (strcmp(options->algorithm, "tttd") == 0) {
		run->effective_avg_size = normalize_power_of_two(destor.chunk_avg_size);
		destor.chunk_avg_size = run->effective_avg_size;
		chunkAlg_init();
		run->chunk_fn = tttd_chunk_data;
		run->display_name = "tttd";
	} else if (strcmp(options->algorithm, "ae") == 0) {
		ae_init();
		run->chunk_fn = ae_chunk_data;
		run->display_name = "ae";
	} else if (strcmp(options->algorithm, "sc") == 0) {
		sc_init();
		run->chunk_fn = sc_chunk_data;
		run->display_name = "sc";
	} else if (strcmp(options->algorithm, "baseline") == 0) {
		baseline_parallel_init();
		run->chunk_fn = baseline_parallel_chunk_data;
		run->display_name = "baseline";
	} else if (strcmp(options->algorithm, "fastcdc") == 0) {
		fastcdc_init();
		run->chunk_fn = fastcdc_chunk_data;
		run->display_name = "fastcdc";
		if (options->gpu_enabled) {
			if (fastcdc_gpu_init() != 0) {
				CHUNK_TOOL_ERROR("FastCDC --gpu requested, but GPU kernel is unavailable");
				return -1;
			}
			run->chunk_fn = fastcdc_gpu_chunk_data;
			if (!options->gpu_naive) {
				run->chunk_batch_fn = fastcdc_gpu_chunk_batch;
				run->chunk_segment_batch_fn = fastcdc_gpu_chunk_segments_batch;
			}
			run->close_fn = fastcdc_gpu_close;
			run->uses_gpu = 1;
		}
	} else if (strcmp(options->algorithm, "gear") == 0) {
		gear_init();
		run->chunk_fn = gear_chunk_data;
		run->display_name = "gear";
		if (options->gpu_enabled) {
			if (gear_gpu_init() != 0) {
				CHUNK_TOOL_ERROR("Gear --gpu requested, but GPU kernel is unavailable");
				return -1;
			}
			run->chunk_fn = gear_gpu_chunk_data;
			if (!options->gpu_naive) {
				run->chunk_batch_fn = gear_gpu_chunk_batch;
				run->chunk_segment_batch_fn = gear_gpu_chunk_segments_batch;
			}
			run->close_fn = gear_gpu_close;
			run->uses_gpu = 1;
		}
	} else if (strcmp(options->algorithm, "jc") == 0 || strcmp(options->algorithm, "gearjump") == 0) {
		gearjump_init(options->jump_mask_delta);
		run->chunk_fn = gearjump_chunk_data;
		run->display_name = "jc";
		if (options->gpu_enabled) {
			if (jc_gpu_init() != 0) {
				CHUNK_TOOL_ERROR("JC --gpu requested, but GPU kernel is unavailable");
				return -1;
			}
			run->chunk_fn = jc_gpu_chunk_data;
			if (!options->gpu_naive) {
				run->chunk_batch_fn = jc_gpu_chunk_batch;
				run->chunk_segment_batch_fn = jc_gpu_chunk_segments_batch;
			}
			run->close_fn = jc_gpu_close;
			run->uses_gpu = 1;
		}
	} else if (strcmp(options->algorithm, "jctttd") == 0) {
		gearjump_init(options->jump_mask_delta);
		run->chunk_fn = gearjumpTTTD_chunk_data;
		run->display_name = "jctttd";
	} else if (strcmp(options->algorithm, "normalized-gearjump") == 0) {
		normalized_gearjump_init(options->jump_mask_delta);
		run->chunk_fn = normalized_gearjump_chunk_data;
		run->display_name = "normalized-gearjump";
	} else if (strcmp(options->algorithm, "tttdgear") == 0) {
		gear_init();
		run->chunk_fn = TTTD_gear_chunk_data;
		run->display_name = "tttdgear";
	} else if (strcmp(options->algorithm, "leap") == 0) {
		leap_init(destor.chunk_avg_size, options->leap_par_idx);
		run->chunk_fn = leap_chunk_data;
		run->display_name = "leap";
	} else {
		CHUNK_TOOL_ERROR("Unsupported algorithm: %s", options->algorithm);
		return -1;
	}

	return 0;
}

static int parse_args(int argc, char **argv, struct chunk_tool_options *options) {
	int i;

	memset(options, 0, sizeof(*options));
	options->chunk_avg_size = 4096;
	options->chunk_warp_window = 32;
	options->jump_mask_delta = 1;
	options->gpu_device_id = 0;
	options->gpu_batch_size = 8 * 1024 * 1024;
	options->gpu_pipeline_tasks = 256;
	options->gpu_threads_per_block = 128;
	options->print_limit = 32;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--list") == 0) {
			list_algorithms();
			return 1;
		}
		if (strcmp(argv[i], "--all") == 0) {
			options->run_all = 1;
			continue;
		}
		if (strcmp(argv[i], "--batch") == 0 && i + 1 < argc) {
			options->batch_algorithms = argv[++i];
			continue;
		}
		if ((strcmp(argv[i], "-a") == 0 || strcmp(argv[i], "--algorithm") == 0) && i + 1 < argc) {
			options->algorithm = argv[++i];
			continue;
		}
		if ((strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--input") == 0) && i + 1 < argc) {
			options->input_path = argv[++i];
			continue;
		}
		if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--avg") == 0) && i + 1 < argc) {
			options->chunk_avg_size = parse_int_arg("avg size", argv[++i]);
			if (options->chunk_avg_size < 0) {
				return -1;
			}
			continue;
		}
		if (strcmp(argv[i], "--min") == 0 && i + 1 < argc) {
			options->chunk_min_size = parse_int_arg("min size", argv[++i]);
			if (options->chunk_min_size < 0) {
				return -1;
			}
			continue;
		}
		if (strcmp(argv[i], "--max") == 0 && i + 1 < argc) {
			options->chunk_max_size = parse_int_arg("max size", argv[++i]);
			if (options->chunk_max_size < 0) {
				return -1;
			}
			continue;
		}
		if (strcmp(argv[i], "--mask-bits") == 0 && i + 1 < argc) {
			options->chunk_mask_bits = parse_int_arg("mask bits", argv[++i]);
			if (options->chunk_mask_bits < 0) {
				return -1;
			}
			continue;
		}
		if (strcmp(argv[i], "--warp-window") == 0 && i + 1 < argc) {
			options->chunk_warp_window = parse_int_arg("warp window", argv[++i]);
			if (options->chunk_warp_window < 0) {
				return -1;
			}
			continue;
		}
		if (strcmp(argv[i], "--profile-chunking") == 0) {
			options->profile_chunking = 1;
			continue;
		}
		if (strcmp(argv[i], "--cpu-parallel") == 0) {
			options->cpu_parallel = 1;
			continue;
		}
		if (strcmp(argv[i], "--gpu") == 0) {
			options->gpu_enabled = 1;
			continue;
		}
		if (strcmp(argv[i], "--gpu-naive") == 0) {
			options->gpu_enabled = 1;
			options->gpu_naive = 1;
			continue;
		}
		if (strcmp(argv[i], "--gpu-device") == 0 && i + 1 < argc) {
			options->gpu_device_id = parse_nonnegative_int_arg("gpu device id", argv[++i]);
			if (options->gpu_device_id < 0) {
				return -1;
			}
			continue;
		}
		if (strcmp(argv[i], "--gpu-batch") == 0 && i + 1 < argc) {
			options->gpu_batch_size = parse_int_arg("gpu batch size", argv[++i]);
			if (options->gpu_batch_size < 0) {
				return -1;
			}
			continue;
		}
		if (strcmp(argv[i], "--gpu-pipeline-tasks") == 0 && i + 1 < argc) {
			options->gpu_pipeline_tasks = parse_int_arg("gpu pipeline tasks", argv[++i]);
			if (options->gpu_pipeline_tasks < 0) {
				return -1;
			}
			continue;
		}
		if (strcmp(argv[i], "--gpu-threads-per-block") == 0 && i + 1 < argc) {
			options->gpu_threads_per_block = parse_int_arg("gpu threads per block", argv[++i]);
			if (options->gpu_threads_per_block < 0) {
				return -1;
			}
			continue;
		}
		if (strcmp(argv[i], "--jump-mto") == 0 && i + 1 < argc) {
			options->jump_mask_delta = parse_int_arg("jump mask delta", argv[++i]);
			if (options->jump_mask_delta < 0) {
				return -1;
			}
			continue;
		}
		if (strcmp(argv[i], "--leap-par-idx") == 0 && i + 1 < argc) {
			options->leap_par_idx = parse_nonnegative_int_arg("leap par idx", argv[++i]);
			if (options->leap_par_idx < 0) {
				return -1;
			}
			continue;
		}
		if (strcmp(argv[i], "--print-chunks") == 0) {
			options->print_chunks = 1;
			continue;
		}
		if (strcmp(argv[i], "--print-limit") == 0 && i + 1 < argc) {
			options->print_limit = parse_int_arg("print limit", argv[++i]);
			if (options->print_limit < 0) {
				return -1;
			}
			continue;
		}
		if (strcmp(argv[i], "--result-csv") == 0 && i + 1 < argc) {
			options->result_csv_path = argv[++i];
			continue;
		}

		usage(argv[0]);
		CHUNK_TOOL_ERROR("Unknown or incomplete argument: %s", argv[i]);
		return -1;
	}

	if ((!options->algorithm && !options->batch_algorithms && !options->run_all) || !options->input_path) {
		usage(argv[0]);
		return -1;
	}
	if ((options->algorithm && options->batch_algorithms)
			|| (options->algorithm && options->run_all)
			|| (options->batch_algorithms && options->run_all)) {
		CHUNK_TOOL_ERROR("Use only one of --algorithm, --batch, or --all");
		usage(argv[0]);
		return -1;
	}

	if (options->chunk_min_size == 0) {
		options->chunk_min_size = options->chunk_avg_size / 4;
	}
	if (options->chunk_max_size == 0) {
		options->chunk_max_size = options->chunk_avg_size * 4;
	}

	if (options->chunk_min_size <= 0 || options->chunk_avg_size <= 0 || options->chunk_max_size <= 0) {
		CHUNK_TOOL_ERROR("Chunk sizes must be positive");
		return -1;
	}
	if (options->chunk_min_size > options->chunk_avg_size || options->chunk_avg_size > options->chunk_max_size) {
		CHUNK_TOOL_ERROR("Require min <= avg <= max");
		return -1;
	}
	if (options->chunk_mask_bits != 0
			&& (options->chunk_mask_bits <= 1 || options->chunk_mask_bits >= 17)) {
		CHUNK_TOOL_ERROR("Require mask bits in [2, 16]");
		return -1;
	}
	if (options->chunk_warp_window <= 0) {
		CHUNK_TOOL_ERROR("Warp window must be positive");
		return -1;
	}

	return 0;
}

int main(int argc, char **argv) {
	struct chunk_tool_options options;
	struct chunk_tool_options run_options;
	struct chunk_tool_path_list paths;
	struct chunk_tool_stats stats[CHUNK_TOOL_MAX_BATCH];
	const char *algorithms[CHUNK_TOOL_MAX_BATCH];
	int algorithm_count = 0;
	int i;
	int parse_result = parse_args(argc, argv, &options);
	int rc = 0;

	memset(&paths, 0, sizeof(paths));

	if (parse_result > 0) {
		return 0;
	}
	if (parse_result < 0) {
		return 2;
	}

	if (collect_input_paths_recursive(options.input_path, &paths) != 0) {
		return 2;
	}

	if (options.run_all) {
		algorithm_count = CHUNK_TOOL_ALGORITHM_COUNT;
		for (i = 0; i < algorithm_count; i++) {
			algorithms[i] = k_all_algorithms[i];
		}
	} else if (options.batch_algorithms) {
		algorithm_count = parse_algorithm_list(options.batch_algorithms, algorithms, CHUNK_TOOL_MAX_BATCH);
		if (algorithm_count < 0) {
			free_path_list(&paths);
			return 2;
		}
	} else {
		algorithm_count = 1;
		algorithms[0] = options.algorithm;
	}

	for (i = 0; i < algorithm_count; i++) {
		run_options = options;
		run_options.algorithm = algorithms[i];
		if (execute_path_run(&run_options, &paths, &stats[i], algorithm_count > 1) != 0) {
			rc = 2;
			break;
		}
		print_stats_summary(&stats[i], options.input_path);
		if (append_stats_csv(options.result_csv_path, options.input_path, &stats[i]) != 0) {
			rc = 2;
			break;
		}
	}

	if (algorithm_count > 1 && rc == 0) {
		print_batch_summary(stats, algorithm_count);
	}

	if (options.batch_algorithms) {
		free_algorithm_list(algorithms, algorithm_count);
	}
	free_path_list(&paths);
	return rc;
}