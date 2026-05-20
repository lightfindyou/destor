#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <dirent.h>
#include <math.h>
#include <sys/stat.h>
#include <time.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/destor.h"
#include "../src/chunking/chunking.h"
#include "../src/chunking/gear_common.h"

struct destor destor;

typedef int (*chunk_fn_t)(unsigned char *p, int n);
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
	int gpu_enabled;
	int gpu_device_id;
	int gpu_batch_size;
	int print_chunks;
	int print_limit;
	int run_all;
};

struct chunk_tool_run {
	chunk_fn_t chunk_fn;
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
	int uses_gpu;
	int profile_chunking;
	struct chunk_experiment_stats experiment_stats;
};

struct chunk_tool_path_list {
	char **items;
	int count;
	int capacity;
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
static void baseline_parallel_init(void);
static int baseline_parallel_chunk_data(unsigned char *p, int n);

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
#define CHUNK_TOOL_MAX_BATCH 32

static uint64_t g_baseline_mask;

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
			"      --result-csv PATH  Append one CSV row per run to PATH\n"
			"      --gpu              Enable GPU wrapper when supported\n"
			"      --gpu-device ID    GPU device id, default 0\n"
			"      --gpu-batch SIZE   GPU batch size, default 8388608\n"
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

static double time_diff_ms(const struct timespec *start, const struct timespec *end) {
	return (double)(end->tv_sec - start->tv_sec) * 1000.0
			+ (double)(end->tv_nsec - start->tv_nsec) / 1000000.0;
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
		return append_path(paths, path);
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

static int read_input_file(const char *path, unsigned char **buffer, size_t *buffer_size) {
	struct stat st;
	FILE *fp = fopen(path, "rb");
	unsigned char *data;
	size_t read_len;
	size_t file_size;

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
	printf("algorithm: %s%s\n",
			stats->algorithm,
			stats->uses_gpu ? " (gpu)" : "");
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
		printf("checks per chunk min/max/avg: %llu/%llu/%.2f\n",
				(unsigned long long)stats->experiment_stats.min_checks_per_chunk,
				(unsigned long long)stats->experiment_stats.max_checks_per_chunk,
				avg_checks);
		printf("avg redundant checks per chunk: %.2f\n", avg_redundant_checks);
	}
	printf("elapsed: %.3f ms\n", stats->elapsed_ms);
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
			chunk_experiment_note_redundancy(group_end - first_cutoff, group_end - i);
			chunk_experiment_note_chunk_complete(first_cutoff, 1);
			return first_cutoff;
		}

		chunk_experiment_note_redundancy(0, group_end - i);
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

static int run_chunking(const struct chunk_tool_options *options,
		const struct chunk_tool_run *run,
		unsigned char *buffer,
		size_t buffer_size,
		struct chunk_tool_stats *stats) {
	size_t offset = 0;
	size_t chunk_count = 0;
	size_t printed = 0;
	unsigned long long total_chunk_bytes = 0;
	int observed_min = INT_MAX;
	int observed_max = 0;
	struct timespec start_time;
	struct timespec end_time;
	struct chunk_experiment_stats experiment_stats;

	chunk_experiment_reset_stats();

	clock_gettime(CLOCK_MONOTONIC, &start_time);

	while (offset < buffer_size) {
		int remaining = (int)(buffer_size - offset);
		int chunk_size = run->chunk_fn(buffer + offset, remaining);

		if (chunk_size <= 0 || chunk_size > remaining) {
			CHUNK_TOOL_ERROR("Invalid chunk size %d at offset %zu, remaining=%d",
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

	clock_gettime(CLOCK_MONOTONIC, &end_time);
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
	stats->elapsed_ms = time_diff_ms(&start_time, &end_time);
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

static void print_batch_summary(const struct chunk_tool_stats *stats, int count) {
	int i;

	printf("\nsummary:\n");
	printf("%-20s %-8s %-8s %-12s %-16s %-12s %-10s\n",
			"algorithm",
			"files",
			"chunks",
			"cfg(avg)",
			"obs(min/max/avg)",
			"elapsed(ms)",
			"mode");
	for (i = 0; i < count; i++) {
		printf("%-20s %-8zu %-8zu %-12d %d/%d/%.2f %12.3f %-10s\n",
				stats[i].algorithm,
				stats[i].file_count,
				stats[i].chunk_count,
				stats[i].configured_avg,
				stats[i].observed_min,
				stats[i].observed_max,
				stats[i].observed_avg,
				stats[i].elapsed_ms,
				stats[i].uses_gpu ? "gpu" : "cpu");
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
				"algorithm,input,mode,files,bytes,cfg_min,cfg_avg,cfg_max,mask_bits,warp_window,chunks,obs_min,obs_max,obs_avg,elapsed_ms,fingerprint_updates,cutoff_hits,jump_hits,jump_bytes_skipped,redundant_checks,warp_groups,min_checks,max_checks,avg_checks\n");
	}
	if (stats->experiment_stats.chunk_count > 0) {
		avg_checks = (double)stats->experiment_stats.total_checks_per_chunk
				/ (double)stats->experiment_stats.chunk_count;
	}
	fprintf(fp,
			"%s,%s,%s,%zu,%zu,%d,%d,%d,%d,%d,%zu,%d,%d,%.2f,%.3f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%.2f\n",
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
			(unsigned long long)stats->experiment_stats.fingerprint_updates,
			(unsigned long long)stats->experiment_stats.cutoff_hits,
			(unsigned long long)stats->experiment_stats.jump_hits,
			(unsigned long long)stats->experiment_stats.jump_bytes_skipped,
			(unsigned long long)stats->experiment_stats.redundant_checks,
			(unsigned long long)stats->experiment_stats.simulated_warp_groups,
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
}

static int execute_single_run(const struct chunk_tool_options *options,
		unsigned char *buffer,
		size_t buffer_size,
		struct chunk_tool_stats *stats,
		int show_separator,
		int print_summary) {
	struct chunk_tool_run run;
	int rc;

	reset_destor_for_run(options);
	init_stats(stats);
	if (show_separator) {
		printf("\n==== %s ====\n", options->algorithm);
	}
	if (select_algorithm(options, &run) != 0) {
		return 2;
	}

	if (buffer_size == 0) {
		stats->algorithm = run.display_name;
		stats->configured_min = destor.chunk_min_size;
		stats->configured_avg = run.effective_avg_size;
		stats->configured_max = destor.chunk_max_size;
		stats->file_count = 1;
		stats->total_bytes = 0;
		stats->chunk_count = 0;
		stats->observed_min = 0;
		stats->observed_max = 0;
		stats->observed_avg = 0.0;
		stats->elapsed_ms = 0.0;
		stats->uses_gpu = run.uses_gpu;
		rc = 0;
	} else {
		rc = run_chunking(options, &run, buffer, buffer_size, stats) == 0 ? 0 : 1;
	}

	if (print_summary) {
		print_stats_summary(stats, options->input_path);
	}

	if (run.close_fn) {
		run.close_fn();
	}
	return rc;
}

static int execute_path_run(const struct chunk_tool_options *options,
		const struct chunk_tool_path_list *paths,
		struct chunk_tool_stats *stats,
		int show_separator) {
	int i;

	init_stats(stats);
	for (i = 0; i < paths->count; i++) {
		unsigned char *buffer = NULL;
		size_t buffer_size = 0;
		struct chunk_tool_options file_options = *options;
		struct chunk_tool_stats file_stats;
		int rc;

		file_options.input_path = paths->items[i];
		if (read_input_file(paths->items[i], &buffer, &buffer_size) != 0) {
			free(buffer);
			return 2;
		}
		rc = execute_single_run(&file_options,
				buffer,
				buffer_size,
				&file_stats,
				show_separator && i == 0,
				0);
		free(buffer);
		if (rc != 0) {
			return rc;
		}
		merge_stats(stats, &file_stats);
	}

	if (paths->count == 0) {
		struct chunk_tool_run run;

		reset_destor_for_run(options);
		if (show_separator) {
			printf("\n==== %s ====\n", options->algorithm);
		}
		if (select_algorithm(options, &run) != 0) {
			return 2;
		}
		stats->algorithm = run.display_name;
		stats->configured_min = destor.chunk_min_size;
		stats->configured_avg = run.effective_avg_size;
		stats->configured_max = destor.chunk_max_size;
		stats->uses_gpu = run.uses_gpu;
		stats->observed_min = 0;
		if (run.close_fn) {
			run.close_fn();
		}
	}

	finalize_stats(stats);
	return 0;
}

static int select_algorithm(const struct chunk_tool_options *options, struct chunk_tool_run *run) {
	memset(run, 0, sizeof(*run));
	run->effective_avg_size = destor.chunk_avg_size;

	if (options->gpu_enabled
			&& strcmp(options->algorithm, "fastcdc") != 0
			&& strcmp(options->algorithm, "jc") != 0
			&& strcmp(options->algorithm, "gearjump") != 0) {
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
				CHUNK_TOOL_ERROR("FastCDC GPU init failed");
				return -1;
			}
			run->chunk_fn = fastcdc_gpu_chunk_data;
			run->close_fn = fastcdc_gpu_close;
			run->uses_gpu = 1;
		}
	} else if (strcmp(options->algorithm, "gear") == 0) {
		gear_init();
		run->chunk_fn = gear_chunk_data;
		run->display_name = "gear";
	} else if (strcmp(options->algorithm, "jc") == 0 || strcmp(options->algorithm, "gearjump") == 0) {
		gearjump_init(options->jump_mask_delta);
		run->chunk_fn = gearjump_chunk_data;
		run->display_name = "jc";
		if (options->gpu_enabled) {
			if (jc_gpu_init() != 0) {
				CHUNK_TOOL_ERROR("JC GPU init failed");
				return -1;
			}
			run->chunk_fn = jc_gpu_chunk_data;
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
		if (strcmp(argv[i], "--gpu") == 0) {
			options->gpu_enabled = 1;
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