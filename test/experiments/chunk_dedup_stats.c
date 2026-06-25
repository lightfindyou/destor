#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../src/destor.h"
#include "../../src/chunking/chunking.h"

struct destor destor;

void destor_log(int level, const char *fmt, ...) {
	(void)level;
	(void)fmt;
}

static void usage(const char *prog) {
	fprintf(stderr,
			"Usage: %s --mode cpu|gpu -i INPUT [-s avg] [--min N] [--max N]\n",
			prog);
}

static int hash_chunk(const unsigned char *data, int len, unsigned char out[SHA256_DIGEST_LENGTH]) {
	SHA256_CTX ctx;

	if (!SHA256_Init(&ctx)) {
		return -1;
	}
	if (!SHA256_Update(&ctx, data, (size_t)len)) {
		return -1;
	}
	if (!SHA256_Final(out, &ctx)) {
		return -1;
	}
	return 0;
}

static int hash_equal(const unsigned char *a, const unsigned char *b) {
	return memcmp(a, b, SHA256_DIGEST_LENGTH) == 0;
}

static int find_hash(const unsigned char (*hashes)[SHA256_DIGEST_LENGTH], int count,
		const unsigned char candidate[SHA256_DIGEST_LENGTH]) {
	int i;

	for (i = 0; i < count; i++) {
		if (hash_equal(hashes[i], candidate)) {
			return i;
		}
	}
	return -1;
}

static int chunk_stream(int use_gpu,
		unsigned char *buf,
		int len,
		uint64_t *unique_bytes,
		size_t *chunk_count,
		uint64_t *total_chunk_bytes,
		int *observed_min,
		int *observed_max) {
	unsigned char (*hashes)[SHA256_DIGEST_LENGTH] = NULL;
	int hash_cap = 1024;
	int hash_count = 0;
	int offset = 0;
	int min_size = INT_MAX;
	int max_size = 0;

	hashes = calloc((size_t)hash_cap, SHA256_DIGEST_LENGTH);
	if (!hashes) {
		return -1;
	}

	if (use_gpu) {
		if (fastcdc_gpu_init() != 0) {
			free(hashes);
			return -1;
		}
	}

	while (offset < len) {
		int chunk_size;
		unsigned char digest[SHA256_DIGEST_LENGTH];
		int existing;

		if (use_gpu) {
			chunk_size = fastcdc_gpu_chunk_data(buf + offset, len - offset);
		} else {
			chunk_size = fastcdc_chunk_data(buf + offset, len - offset);
		}
		if (chunk_size <= 0 || chunk_size > (len - offset)) {
			free(hashes);
			if (use_gpu) {
				fastcdc_gpu_close();
			}
			return -1;
		}
		if (hash_chunk(buf + offset, chunk_size, digest) != 0) {
			free(hashes);
			if (use_gpu) {
				fastcdc_gpu_close();
			}
			return -1;
		}
		existing = find_hash(hashes, hash_count, digest);
		if (existing < 0) {
			if (hash_count >= hash_cap) {
				int new_cap = hash_cap * 2;
				unsigned char (*grown)[SHA256_DIGEST_LENGTH];

				grown = realloc(hashes, (size_t)new_cap * SHA256_DIGEST_LENGTH);
				if (!grown) {
					free(hashes);
					if (use_gpu) {
						fastcdc_gpu_close();
					}
					return -1;
				}
				memset(grown + hash_count, 0,
						(size_t)(new_cap - hash_count) * SHA256_DIGEST_LENGTH);
				hashes = grown;
				hash_cap = new_cap;
			}
			memcpy(hashes[hash_count], digest, SHA256_DIGEST_LENGTH);
			*unique_bytes += (uint64_t)chunk_size;
			hash_count++;
		}
		if (chunk_size < min_size) {
			min_size = chunk_size;
		}
		if (chunk_size > max_size) {
			max_size = chunk_size;
		}
		*total_chunk_bytes += (uint64_t)chunk_size;
		(*chunk_count)++;
		offset += chunk_size;
	}

	if (use_gpu) {
		fastcdc_gpu_close();
	}
	free(hashes);
	*observed_min = (*chunk_count > 0) ? min_size : 0;
	*observed_max = (*chunk_count > 0) ? max_size : 0;
	return 0;
}

int main(int argc, char **argv) {
	const char *input = NULL;
	const char *mode = NULL;
	int avg = 4096;
	int min_size = 0;
	int max_size = 0;
	int mask_bits = 12;
	int use_gpu = 0;
	int fd = -1;
	struct stat st;
	unsigned char *buf = NULL;
	size_t chunk_count = 0;
	uint64_t unique_bytes = 0;
	uint64_t total_chunk_bytes = 0;
	int observed_min = 0;
	int observed_max = 0;
	double dedup_ratio;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
			mode = argv[++i];
		} else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
			input = argv[++i];
		} else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
			avg = atoi(argv[++i]);
		} else if (strcmp(argv[i], "--min") == 0 && i + 1 < argc) {
			min_size = atoi(argv[++i]);
		} else if (strcmp(argv[i], "--max") == 0 && i + 1 < argc) {
			max_size = atoi(argv[++i]);
		} else if (strcmp(argv[i], "--mask-bits") == 0 && i + 1 < argc) {
			mask_bits = atoi(argv[++i]);
		} else {
			usage(argv[0]);
			return 2;
		}
	}

	if (!input || !mode) {
		usage(argv[0]);
		return 2;
	}
	if (strcmp(mode, "gpu") == 0) {
		use_gpu = 1;
	} else if (strcmp(mode, "cpu") != 0) {
		usage(argv[0]);
		return 2;
	}

	if (min_size <= 0) {
		min_size = avg / 4;
	}
	if (max_size <= 0) {
		max_size = avg * 4;
	}

	destor.chunk_min_size = min_size;
	destor.chunk_avg_size = avg;
	destor.chunk_max_size = max_size;
	destor.chunk_mask_bits = mask_bits;
	destor.chunk_warp_window = 32;
	fastcdc_init();

	fd = open(input, O_RDONLY);
	if (fd < 0) {
		perror("open");
		return 1;
	}
	if (fstat(fd, &st) != 0) {
		perror("fstat");
		close(fd);
		return 1;
	}
	if (st.st_size <= 0) {
		fprintf(stderr, "empty input\n");
		close(fd);
		return 1;
	}
	buf = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (buf == MAP_FAILED) {
		perror("mmap");
		return 1;
	}

	if (chunk_stream(use_gpu,
				buf,
				(int)st.st_size,
				&unique_bytes,
				&chunk_count,
				&total_chunk_bytes,
				&observed_min,
				&observed_max) != 0) {
		munmap(buf, (size_t)st.st_size);
		return 1;
	}
	munmap(buf, (size_t)st.st_size);

	dedup_ratio = unique_bytes > 0
			? (double)st.st_size / (double)unique_bytes
			: 0.0;

	printf("mode,%s\n", mode);
	printf("input,%s\n", input);
	printf("raw_bytes,%lld\n", (long long)st.st_size);
	printf("chunk_count,%zu\n", chunk_count);
	printf("avg_chunk_size,%.2f\n",
			chunk_count > 0 ? (double)total_chunk_bytes / (double)chunk_count : 0.0);
	printf("obs_min,%d\n", observed_min);
	printf("obs_max,%d\n", observed_max);
	printf("unique_bytes,%" PRIu64 "\n", unique_bytes);
	printf("dedup_ratio,%.6f\n", dedup_ratio);
	return 0;
}
