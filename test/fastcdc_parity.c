#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/destor.h"
#include "../src/chunking/chunking.h"

struct destor destor;

static void usage(const char *prog) {
	fprintf(stderr,
			"Usage: %s [-n cases] [-s seed] [-a avg] [-m min] [-x max] [-l max_input_len]\n",
			prog);
}

void destor_log(int level, const char *fmt, ...) {
	(void)level;
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	fprintf(stdout, "\n");
	va_end(ap);
}

static size_t chunk_stream_cpu(const unsigned char *buf, int len, int *out, int out_cap) {
	int offset = 0;
	size_t num = 0;
	while (offset < len && (int)num < out_cap) {
		int csz = fastcdc_chunk_data((unsigned char *)buf + offset, len - offset);
		if (csz <= 0 || csz > (len - offset)) {
			return (size_t)-1;
		}
		offset += csz;
		out[num++] = offset;
	}
	if (offset != len) {
		return (size_t)-1;
	}
	return num;
}

static size_t chunk_stream_gpu(const unsigned char *buf, int len, int *out, int out_cap) {
	int offset = 0;
	size_t num = 0;
	while (offset < len && (int)num < out_cap) {
		int csz = fastcdc_gpu_chunk_data((unsigned char *)buf + offset, len - offset);
		if (csz <= 0 || csz > (len - offset)) {
			return (size_t)-1;
		}
		offset += csz;
		out[num++] = offset;
	}
	if (offset != len) {
		return (size_t)-1;
	}
	return num;
}

int main(int argc, char **argv) {
	int cases = 2000;
	unsigned int seed = 1;
	int avg = 4096;
	int min = 512;
	int max = 65536;
	int max_input_len = 2 * 1024 * 1024;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
			cases = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
			seed = (unsigned int)atoi(argv[++i]);
		} else if (strcmp(argv[i], "-a") == 0 && i + 1 < argc) {
			avg = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
			min = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-x") == 0 && i + 1 < argc) {
			max = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
			max_input_len = atoi(argv[++i]);
		} else {
			usage(argv[0]);
			return 2;
		}
	}

	if (min <= 0 || avg <= 0 || max <= 0 || max_input_len <= 0 || min > avg || avg > max) {
		fprintf(stderr, "Invalid size configuration: require min <= avg <= max and all > 0\n");
		return 2;
	}

	destor.chunk_min_size = min;
	destor.chunk_avg_size = avg;
	destor.chunk_max_size = max;
	destor.chunk_gpu_device_id = 0;
	destor.chunk_gpu_batch_size = 8 * 1024 * 1024;
	destor.chunk_gpu_enable = 1;

	fastcdc_init();
	if (fastcdc_gpu_init() != 0 || !fastcdc_gpu_is_ready()) {
		fprintf(stderr,
				"FastCDC GPU parity requires a loaded GPU kernel. Build PTX first with make -C src/chunking fastcdc_gpu_ptx\n");
		return 2;
	}

	srand(seed);

	unsigned char *buf = (unsigned char *)malloc((size_t)max_input_len);
	int *cpu_bounds = (int *)malloc((size_t)max_input_len * sizeof(int));
	int *gpu_bounds = (int *)malloc((size_t)max_input_len * sizeof(int));

	if (!buf || !cpu_bounds || !gpu_bounds) {
		fprintf(stderr, "Allocation failed\n");
		free(buf);
		free(cpu_bounds);
		free(gpu_bounds);
		return 2;
	}

	for (int c = 0; c < cases; c++) {
		int len = (rand() % max_input_len) + 1;
		for (int i = 0; i < len; i++) {
			buf[i] = (unsigned char)(rand() & 0xff);
		}

		size_t cpu_n = chunk_stream_cpu(buf, len, cpu_bounds, max_input_len);
		size_t gpu_n = chunk_stream_gpu(buf, len, gpu_bounds, max_input_len);

		if (cpu_n == (size_t)-1 || gpu_n == (size_t)-1) {
			fprintf(stderr, "Invalid chunk sequence at case %d, len=%d\n", c, len);
			free(buf);
			free(cpu_bounds);
			free(gpu_bounds);
			return 1;
		}

		if (cpu_n != gpu_n) {
			fprintf(stderr,
					"Mismatch chunk count at case %d, len=%d: cpu=%zu gpu=%zu\n",
					c, len, cpu_n, gpu_n);
			free(buf);
			free(cpu_bounds);
			free(gpu_bounds);
			return 1;
		}

		for (size_t i = 0; i < cpu_n; i++) {
			if (cpu_bounds[i] != gpu_bounds[i]) {
				fprintf(stderr,
						"Mismatch boundary at case %d, len=%d, idx=%zu: cpu=%d gpu=%d\n",
						c, len, i, cpu_bounds[i], gpu_bounds[i]);
				free(buf);
				free(cpu_bounds);
				free(gpu_bounds);
				return 1;
			}
		}
	}

	printf("FastCDC parity PASS: cases=%d seed=%u min=%d avg=%d max=%d max_input_len=%d\n",
			cases, seed, min, avg, max, max_input_len);

	fastcdc_gpu_close();
	free(buf);
	free(cpu_bounds);
	free(gpu_bounds);
	return 0;
}
