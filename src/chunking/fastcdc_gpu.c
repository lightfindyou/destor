#include "chunking.h"
#include "../destor.h"
#include "gear_common.h"

#include <dlfcn.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef int CUresult;
typedef int CUdevice;
typedef struct CUctx_st *CUcontext;
typedef struct CUmod_st *CUmodule;
typedef struct CUfunc_st *CUfunction;
typedef struct CUstream_st *CUstream;
typedef struct CUevent_st *CUevent;
typedef unsigned long long CUdeviceptr;

#define CUDA_SUCCESS 0
#define FASTCDC_GPU_KERNEL_SYMBOL "fastcdc_chunk_kernel"
#define JC_GPU_KERNEL_SYMBOL "jc_chunk_kernel"
#define FASTCDC_GPU_GEAR_SYMBOL_COUNT 256

typedef CUresult (*cuInit_t)(unsigned int flags);
typedef CUresult (*cuDeviceGetCount_t)(int *count);
typedef CUresult (*cuDeviceGet_t)(CUdevice *device, int ordinal);
typedef CUresult (*cuCtxCreate_t)(CUcontext *pctx, unsigned int flags, CUdevice dev);
typedef CUresult (*cuCtxDestroy_t)(CUcontext ctx);
typedef CUresult (*cuCtxSetCurrent_t)(CUcontext ctx);
typedef CUresult (*cuGetErrorString_t)(CUresult error, const char **pStr);
typedef CUresult (*cuModuleLoad_t)(CUmodule *module, const char *fname);
typedef CUresult (*cuModuleUnload_t)(CUmodule module);
typedef CUresult (*cuModuleGetFunction_t)(CUfunction *hfunc, CUmodule module, const char *name);
typedef CUresult (*cuMemAlloc_t)(CUdeviceptr *dptr, size_t bytesize);
typedef CUresult (*cuMemFree_t)(CUdeviceptr dptr);
typedef CUresult (*cuMemHostAlloc_t)(void **pp, size_t bytesize, unsigned int Flags);
typedef CUresult (*cuMemFreeHost_t)(void *p);
typedef CUresult (*cuMemcpyHtoD_t)(CUdeviceptr dstDevice, const void *srcHost, size_t ByteCount);
typedef CUresult (*cuMemcpyDtoH_t)(void *dstHost, CUdeviceptr srcDevice, size_t ByteCount);
typedef CUresult (*cuMemcpyHtoDAsync_t)(CUdeviceptr dstDevice, const void *srcHost, size_t ByteCount, CUstream hStream);
typedef CUresult (*cuMemcpyDtoHAsync_t)(void *dstHost, CUdeviceptr srcDevice, size_t ByteCount, CUstream hStream);
typedef CUresult (*cuLaunchKernel_t)(CUfunction f,
		unsigned int gridDimX,
		unsigned int gridDimY,
		unsigned int gridDimZ,
		unsigned int blockDimX,
		unsigned int blockDimY,
		unsigned int blockDimZ,
		unsigned int sharedMemBytes,
		void *hStream,
		void **kernelParams,
		void **extra);
typedef CUresult (*cuStreamCreate_t)(CUstream *phStream, unsigned int Flags);
typedef CUresult (*cuStreamDestroy_t)(CUstream hStream);
typedef CUresult (*cuStreamSynchronize_t)(CUstream hStream);
typedef CUresult (*cuEventCreate_t)(CUevent *phEvent, unsigned int Flags);
typedef CUresult (*cuEventDestroy_t)(CUevent hEvent);
typedef CUresult (*cuEventRecord_t)(CUevent hEvent, CUstream hStream);
typedef CUresult (*cuEventSynchronize_t)(CUevent hEvent);
typedef CUresult (*cuEventElapsedTime_t)(float *pMilliseconds, CUevent hStart, CUevent hEnd);
typedef CUresult (*cuCtxSynchronize_t)(void);

struct fastcdc_gpu_kernel_result {
	int chunk_size;
	int cutoff_hit;
	int cutoff_lane;
	int tail_idle_lanes;
	uint64_t fingerprint_updates;
	uint64_t redundant_checks;
	uint64_t warp_groups;
	uint64_t jump_hits;
	uint64_t jump_bytes_skipped;
};

struct cuda_driver_state {
	void *handle;
	CUcontext ctx;
	CUmodule module;
	CUfunction fastcdc_kernel;
	CUfunction jc_kernel;
	CUdeviceptr gear_matrix_device;
	CUdeviceptr batch_input_device;
	CUdeviceptr batch_input_device_alt;
	CUdeviceptr batch_offsets_device;
	CUdeviceptr batch_offsets_device_alt;
	CUdeviceptr batch_lengths_device;
	CUdeviceptr batch_lengths_device_alt;
	CUdeviceptr batch_results_device;
	CUdeviceptr batch_results_device_alt;
	CUstream batch_stream;
	CUstream batch_stream_alt;
	size_t batch_input_capacity;
	int batch_task_capacity;
	unsigned char *batch_input_host;
	int *batch_offsets_host;
	int *batch_offsets_host_alt;
	int *batch_lengths_host;
	int *batch_lengths_host_alt;
	struct fastcdc_gpu_kernel_result *batch_results_host;
	struct fastcdc_gpu_kernel_result *batch_results_host_alt;
	int initialized;
	int kernel_ready;
	int jc_kernel_ready;
	char ptx_path[PATH_MAX];
	cuInit_t cuInit;
	cuDeviceGetCount_t cuDeviceGetCount;
	cuDeviceGet_t cuDeviceGet;
	cuCtxCreate_t cuCtxCreate;
	cuCtxDestroy_t cuCtxDestroy;
	cuCtxSetCurrent_t cuCtxSetCurrent;
	cuGetErrorString_t cuGetErrorString;
	cuModuleLoad_t cuModuleLoad;
	cuModuleUnload_t cuModuleUnload;
	cuModuleGetFunction_t cuModuleGetFunction;
	cuMemAlloc_t cuMemAlloc;
	cuMemFree_t cuMemFree;
	cuMemHostAlloc_t cuMemHostAlloc;
	cuMemFreeHost_t cuMemFreeHost;
	cuMemcpyHtoD_t cuMemcpyHtoD;
	cuMemcpyDtoH_t cuMemcpyDtoH;
	cuMemcpyHtoDAsync_t cuMemcpyHtoDAsync;
	cuMemcpyDtoHAsync_t cuMemcpyDtoHAsync;
	cuLaunchKernel_t cuLaunchKernel;
	cuStreamCreate_t cuStreamCreate;
	cuStreamDestroy_t cuStreamDestroy;
	cuStreamSynchronize_t cuStreamSynchronize;
    cuEventCreate_t cuEventCreate;
    cuEventDestroy_t cuEventDestroy;
    cuEventRecord_t cuEventRecord;
    cuEventSynchronize_t cuEventSynchronize;
    cuEventElapsedTime_t cuEventElapsedTime;
	cuCtxSynchronize_t cuCtxSynchronize;
	double batch_compute_ms_accum;
};

static struct cuda_driver_state g_cuda;
static struct chunk_experiment_stats g_chunk_experiment_stats;
static uint64_t g_chunk_experiment_current_checks;

static int cuda_driver_init_context();

static int fastcdc_gpu_ensure_batch_capacity(size_t total_bytes, int task_count);
void fastcdc_gpu_reset_batch_timing(void) {
	g_cuda.batch_compute_ms_accum = 0.0;
}

double fastcdc_gpu_get_batch_compute_ms(void) {
	return g_cuda.batch_compute_ms_accum;
}

static int fastcdc_gpu_floor_log2(unsigned int value) {
	int index = 0;

	while (value > 1U) {
		value >>= 1U;
		index++;
	}
	return index;
}

static void fastcdc_gpu_compute_masks(uint64_t *mask_s,
		uint64_t *mask_l,
		int *expect_chunk_size) {
	int index = fastcdc_gpu_floor_log2((unsigned int)destor.chunk_avg_size);
	int mask_bits = destor.chunk_mask_bits > 0 ? destor.chunk_mask_bits : index - 1;

	if (expect_chunk_size) {
		*expect_chunk_size = destor.chunk_mask_bits > 0
				? (1 << (mask_bits + 1))
				: destor.chunk_avg_size;
	}
	if (mask_s) {
		*mask_s = (uint64_t)g_condition_mask[mask_bits + 1];
	}
	if (mask_l) {
		*mask_l = (uint64_t)g_condition_mask[mask_bits - 1];
	}
}

static int jc_gpu_compute_params(uint64_t *mask,
		uint64_t *jump_mask,
		int *jump_len,
		int *expect_chunk_size) {
	int index = fastcdc_gpu_floor_log2((unsigned int)destor.chunk_avg_size);
	int c_ones = destor.chunk_mask_bits > 0 ? destor.chunk_mask_bits : index - 1;
	int jump_delta = destor.jumpOnes > 0 ? destor.jumpOnes : 1;
	int j_ones = c_ones - jump_delta;
	uint64_t numerator;
	uint64_t denominator;

	if (c_ones <= 1 || c_ones >= 17 || j_ones <= 0 || j_ones >= c_ones) {
		return -1;
	}
	if (expect_chunk_size) {
		*expect_chunk_size = destor.chunk_mask_bits > 0
				? (1 << (c_ones + 1))
				: destor.chunk_avg_size;
	}
	if (mask) {
		*mask = (uint64_t)g_condition_mask[c_ones];
	}
	if (jump_mask) {
		*jump_mask = (uint64_t)g_condition_mask[j_ones];
	}
	numerator = 1ULL << (c_ones + j_ones);
	denominator = (1ULL << c_ones) - (1ULL << j_ones);
	if (jump_len) {
		*jump_len = denominator == 0 ? 0 : (int)(numerator / denominator);
	}
	return 0;
}

static int fastcdc_gpu_resolve_ptx_path(char *path, size_t path_size) {
	static const char *k_candidates[] = {
		"src/chunking/fastcdc_gpu_kernel.ptx",
		"./src/chunking/fastcdc_gpu_kernel.ptx",
		"../src/chunking/fastcdc_gpu_kernel.ptx",
		"fastcdc_gpu_kernel.ptx"
	};
	const char *env_path;
	size_t i;

	if (!path || path_size == 0) {
		return -1;
	}

	env_path = getenv("DESTOR_FASTCDC_GPU_PTX");
	if (env_path && *env_path) {
		if (access(env_path, R_OK) == 0) {
			snprintf(path, path_size, "%s", env_path);
			return 0;
		}
		return -1;
	}

	for (i = 0; i < sizeof(k_candidates) / sizeof(k_candidates[0]); i++) {
		if (access(k_candidates[i], R_OK) == 0) {
			snprintf(path, path_size, "%s", k_candidates[i]);
			return 0;
		}
	}

	return -1;
}

static void fastcdc_gpu_note_kernel_result(const struct fastcdc_gpu_kernel_result *result) {
	if (!result || !destor.chunk_profile_enabled) {
		return;
	}
	g_chunk_experiment_stats.fingerprint_updates += result->fingerprint_updates;
	g_chunk_experiment_stats.jump_hits += result->jump_hits;
	g_chunk_experiment_stats.jump_bytes_skipped += result->jump_bytes_skipped;
	if (result->cutoff_hit) {
		g_chunk_experiment_stats.cutoff_lane_sum += (uint64_t)result->cutoff_lane;
		g_chunk_experiment_stats.tail_idle_lane_sum += (uint64_t)result->tail_idle_lanes;
	}
	g_chunk_experiment_current_checks = result->fingerprint_updates;
	chunk_experiment_note_redundancy((int)result->redundant_checks, (int)result->warp_groups);
	chunk_experiment_note_chunk_complete(result->chunk_size, result->cutoff_hit);
}

void chunk_experiment_reset_stats() {
	memset(&g_chunk_experiment_stats, 0, sizeof(g_chunk_experiment_stats));
	g_chunk_experiment_stats.min_checks_per_chunk = UINT64_MAX;
	g_chunk_experiment_current_checks = 0;
}

void chunk_experiment_snapshot(struct chunk_experiment_stats *stats) {
	if (!stats) {
		return;
	}
	*stats = g_chunk_experiment_stats;
	if (stats->min_checks_per_chunk == UINT64_MAX) {
		stats->min_checks_per_chunk = 0;
	}
}

void chunk_experiment_note_fingerprint_update() {
	if (!destor.chunk_profile_enabled) {
		return;
	}
	g_chunk_experiment_stats.fingerprint_updates++;
	g_chunk_experiment_current_checks++;
}

void chunk_experiment_note_jump(int jump_bytes) {
	if (!destor.chunk_profile_enabled) {
		return;
	}
	g_chunk_experiment_stats.jump_hits++;
	if (jump_bytes > 0) {
		g_chunk_experiment_stats.jump_bytes_skipped += (uint64_t)jump_bytes;
	}
}

void chunk_experiment_note_redundancy(int redundant_checks, int warp_group_count) {
	if (!destor.chunk_profile_enabled) {
		return;
	}
	if (redundant_checks > 0) {
		g_chunk_experiment_stats.redundant_checks += (uint64_t)redundant_checks;
	}
	if (warp_group_count > 0) {
		g_chunk_experiment_stats.simulated_warp_groups += (uint64_t)warp_group_count;
	}
}

void chunk_experiment_note_chunk_complete(int chunk_size, int cutoff_hit) {
	if (!destor.chunk_profile_enabled) {
		return;
	}
	g_chunk_experiment_stats.chunk_count++;
	if (cutoff_hit) {
		g_chunk_experiment_stats.cutoff_hits++;
	}
	if (chunk_size > 0) {
		g_chunk_experiment_stats.total_chunk_bytes += (uint64_t)chunk_size;
	}
	g_chunk_experiment_stats.total_checks_per_chunk += g_chunk_experiment_current_checks;
	if (g_chunk_experiment_current_checks < g_chunk_experiment_stats.min_checks_per_chunk) {
		g_chunk_experiment_stats.min_checks_per_chunk = g_chunk_experiment_current_checks;
	}
	if (g_chunk_experiment_current_checks > g_chunk_experiment_stats.max_checks_per_chunk) {
		g_chunk_experiment_stats.max_checks_per_chunk = g_chunk_experiment_current_checks;
	}
	g_chunk_experiment_current_checks = 0;
}

static void fastcdc_gpu_reset_state() {
	memset(&g_cuda, 0, sizeof(g_cuda));
}

static void *fastcdc_gpu_load_symbol_any(const char *primary, const char *secondary) {
	void *symbol = NULL;

	if (primary) {
		symbol = dlsym(g_cuda.handle, primary);
	}
	if (!symbol && secondary) {
		symbol = dlsym(g_cuda.handle, secondary);
	}
	return symbol;
}

static const char *fastcdc_cuda_error_string(CUresult code) {
	const char *msg = NULL;
	if (g_cuda.cuGetErrorString && g_cuda.cuGetErrorString(code, &msg) == CUDA_SUCCESS && msg) {
		return msg;
	}
	return "unknown CUDA error";
}

static int fastcdc_gpu_activate_context() {
	CUresult rc;

	if (!g_cuda.initialized || !g_cuda.ctx) {
		return -1;
	}
	if (!g_cuda.cuCtxSetCurrent) {
		return 0;
	}
	rc = g_cuda.cuCtxSetCurrent(g_cuda.ctx);
	if (rc != CUDA_SUCCESS) {
		WARNING("Chunk GPU: cuCtxSetCurrent failed: %s", fastcdc_cuda_error_string(rc));
		return -1;
	}
	return 0;
}

static void fastcdc_gpu_release_driver() {
	if (g_cuda.batch_stream_alt && g_cuda.cuStreamDestroy) {
		g_cuda.cuStreamDestroy(g_cuda.batch_stream_alt);
	}
	if (g_cuda.batch_stream && g_cuda.cuStreamDestroy) {
		g_cuda.cuStreamDestroy(g_cuda.batch_stream);
	}
	if (g_cuda.batch_results_host_alt) {
		if (g_cuda.cuMemFreeHost) {
			g_cuda.cuMemFreeHost(g_cuda.batch_results_host_alt);
		} else {
			free(g_cuda.batch_results_host_alt);
		}
	}
	if (g_cuda.batch_results_host) {
		if (g_cuda.cuMemFreeHost) {
			g_cuda.cuMemFreeHost(g_cuda.batch_results_host);
		} else {
			free(g_cuda.batch_results_host);
		}
	}
	if (g_cuda.batch_lengths_host_alt) {
		if (g_cuda.cuMemFreeHost) {
			g_cuda.cuMemFreeHost(g_cuda.batch_lengths_host_alt);
		} else {
			free(g_cuda.batch_lengths_host_alt);
		}
	}
	if (g_cuda.batch_lengths_host) {
		if (g_cuda.cuMemFreeHost) {
			g_cuda.cuMemFreeHost(g_cuda.batch_lengths_host);
		} else {
			free(g_cuda.batch_lengths_host);
		}
	}
	if (g_cuda.batch_offsets_host_alt) {
		if (g_cuda.cuMemFreeHost) {
			g_cuda.cuMemFreeHost(g_cuda.batch_offsets_host_alt);
		} else {
			free(g_cuda.batch_offsets_host_alt);
		}
	}
	if (g_cuda.batch_offsets_host) {
		if (g_cuda.cuMemFreeHost) {
			g_cuda.cuMemFreeHost(g_cuda.batch_offsets_host);
		} else {
			free(g_cuda.batch_offsets_host);
		}
	}
	if (g_cuda.batch_input_host) {
		if (g_cuda.cuMemFreeHost) {
			g_cuda.cuMemFreeHost(g_cuda.batch_input_host);
		} else {
			free(g_cuda.batch_input_host);
		}
	}
	if (g_cuda.batch_results_device && g_cuda.cuMemFree) {
		g_cuda.cuMemFree(g_cuda.batch_results_device);
	}
	if (g_cuda.batch_results_device_alt && g_cuda.cuMemFree) {
		g_cuda.cuMemFree(g_cuda.batch_results_device_alt);
	}
	if (g_cuda.batch_lengths_device && g_cuda.cuMemFree) {
		g_cuda.cuMemFree(g_cuda.batch_lengths_device);
	}
	if (g_cuda.batch_lengths_device_alt && g_cuda.cuMemFree) {
		g_cuda.cuMemFree(g_cuda.batch_lengths_device_alt);
	}
	if (g_cuda.batch_offsets_device && g_cuda.cuMemFree) {
		g_cuda.cuMemFree(g_cuda.batch_offsets_device);
	}
	if (g_cuda.batch_offsets_device_alt && g_cuda.cuMemFree) {
		g_cuda.cuMemFree(g_cuda.batch_offsets_device_alt);
	}
	if (g_cuda.batch_input_device && g_cuda.cuMemFree) {
		g_cuda.cuMemFree(g_cuda.batch_input_device);
	}
	if (g_cuda.batch_input_device_alt && g_cuda.cuMemFree) {
		g_cuda.cuMemFree(g_cuda.batch_input_device_alt);
	}
	if (g_cuda.gear_matrix_device && g_cuda.cuMemFree) {
		g_cuda.cuMemFree(g_cuda.gear_matrix_device);
	}
	if (g_cuda.module && g_cuda.cuModuleUnload) {
		g_cuda.cuModuleUnload(g_cuda.module);
	}
	if (g_cuda.ctx && g_cuda.cuCtxDestroy) {
		g_cuda.cuCtxDestroy(g_cuda.ctx);
	}
	if (g_cuda.handle) {
		dlclose(g_cuda.handle);
	}
	fastcdc_gpu_reset_state();
}

static int fastcdc_gpu_alloc_host_buffer(void **ptr, size_t bytes) {
	void *buffer = NULL;

	if (!ptr) {
		return -1;
	}
	if (bytes == 0) {
		*ptr = NULL;
		return 0;
	}
	if (g_cuda.cuMemHostAlloc) {
		if (g_cuda.cuMemHostAlloc(&buffer, bytes, 0) != CUDA_SUCCESS) {
			return -1;
		}
	} else {
		buffer = malloc(bytes);
		if (!buffer) {
			return -1;
		}
	}
	*ptr = buffer;
	return 0;
}

static void fastcdc_gpu_free_host_buffer(void *ptr) {
	if (!ptr) {
		return;
	}
	if (g_cuda.cuMemFreeHost) {
		g_cuda.cuMemFreeHost(ptr);
	} else {
		free(ptr);
	}
}

static int fastcdc_gpu_ensure_batch_capacity(size_t total_bytes, int task_count) {
	CUdeviceptr new_device_ptr = 0;
	CUdeviceptr new_device_ptr_alt = 0;
	unsigned char *new_input_host = NULL;
	int *new_offsets_host = NULL;
	int *new_offsets_host_alt = NULL;
	int *new_lengths_host = NULL;
	int *new_lengths_host_alt = NULL;
	struct fastcdc_gpu_kernel_result *new_results_host = NULL;
	struct fastcdc_gpu_kernel_result *new_results_host_alt = NULL;

	if (task_count <= 0) {
		return -1;
	}
	if (fastcdc_gpu_activate_context() != 0) {
		return -1;
	}
	if (total_bytes > g_cuda.batch_input_capacity) {
		if (fastcdc_gpu_alloc_host_buffer((void **)&new_input_host, total_bytes) != 0) {
			return -1;
		}
		if (g_cuda.batch_input_device_alt) {
			g_cuda.cuMemFree(g_cuda.batch_input_device_alt);
			g_cuda.batch_input_device_alt = 0;
		}
		if (g_cuda.batch_input_device) {
			g_cuda.cuMemFree(g_cuda.batch_input_device);
			g_cuda.batch_input_device = 0;
		}
		if (total_bytes > 0) {
			if (g_cuda.cuMemAlloc(&new_device_ptr, total_bytes) != CUDA_SUCCESS) {
				fastcdc_gpu_free_host_buffer(new_input_host);
				return -1;
			}
			if (g_cuda.cuMemAlloc(&new_device_ptr_alt, total_bytes) != CUDA_SUCCESS) {
				g_cuda.cuMemFree(new_device_ptr);
				fastcdc_gpu_free_host_buffer(new_input_host);
				return -1;
			}
			g_cuda.batch_input_device = new_device_ptr;
			g_cuda.batch_input_device_alt = new_device_ptr_alt;
		}
		fastcdc_gpu_free_host_buffer(g_cuda.batch_input_host);
		g_cuda.batch_input_host = new_input_host;
		g_cuda.batch_input_capacity = total_bytes;
	}
	if (task_count > g_cuda.batch_task_capacity) {
		if (fastcdc_gpu_alloc_host_buffer((void **)&new_offsets_host, sizeof(int) * (size_t)task_count) != 0) {
			return -1;
		}
		if (fastcdc_gpu_alloc_host_buffer((void **)&new_offsets_host_alt, sizeof(int) * (size_t)task_count) != 0) {
			fastcdc_gpu_free_host_buffer(new_offsets_host);
			return -1;
		}

		if (fastcdc_gpu_alloc_host_buffer((void **)&new_lengths_host, sizeof(int) * (size_t)task_count) != 0) {
			fastcdc_gpu_free_host_buffer(new_offsets_host_alt);
			fastcdc_gpu_free_host_buffer(new_offsets_host);
			return -1;
		}
		if (fastcdc_gpu_alloc_host_buffer((void **)&new_lengths_host_alt, sizeof(int) * (size_t)task_count) != 0) {
			fastcdc_gpu_free_host_buffer(new_lengths_host);
			fastcdc_gpu_free_host_buffer(new_offsets_host_alt);
			fastcdc_gpu_free_host_buffer(new_offsets_host);
			return -1;
		}

		if (fastcdc_gpu_alloc_host_buffer((void **)&new_results_host,
				sizeof(struct fastcdc_gpu_kernel_result) * (size_t)task_count) != 0) {
			fastcdc_gpu_free_host_buffer(new_lengths_host_alt);
			fastcdc_gpu_free_host_buffer(new_lengths_host);
			fastcdc_gpu_free_host_buffer(new_offsets_host_alt);
			fastcdc_gpu_free_host_buffer(new_offsets_host);
			return -1;
		}
		if (fastcdc_gpu_alloc_host_buffer((void **)&new_results_host_alt,
				sizeof(struct fastcdc_gpu_kernel_result) * (size_t)task_count) != 0) {
			fastcdc_gpu_free_host_buffer(new_results_host);
			fastcdc_gpu_free_host_buffer(new_lengths_host_alt);
			fastcdc_gpu_free_host_buffer(new_lengths_host);
			fastcdc_gpu_free_host_buffer(new_offsets_host_alt);
			fastcdc_gpu_free_host_buffer(new_offsets_host);
			return -1;
		}

		fastcdc_gpu_free_host_buffer(g_cuda.batch_offsets_host_alt);
		fastcdc_gpu_free_host_buffer(g_cuda.batch_offsets_host);
		fastcdc_gpu_free_host_buffer(g_cuda.batch_lengths_host_alt);
		fastcdc_gpu_free_host_buffer(g_cuda.batch_lengths_host);
		fastcdc_gpu_free_host_buffer(g_cuda.batch_results_host_alt);
		fastcdc_gpu_free_host_buffer(g_cuda.batch_results_host);
		g_cuda.batch_offsets_host = new_offsets_host;
		g_cuda.batch_offsets_host_alt = new_offsets_host_alt;
		g_cuda.batch_lengths_host = new_lengths_host;
		g_cuda.batch_lengths_host_alt = new_lengths_host_alt;
		g_cuda.batch_results_host = new_results_host;
		g_cuda.batch_results_host_alt = new_results_host_alt;

		if (g_cuda.batch_offsets_device_alt) {
			g_cuda.cuMemFree(g_cuda.batch_offsets_device_alt);
			g_cuda.batch_offsets_device_alt = 0;
		}
		if (g_cuda.batch_offsets_device) {
			g_cuda.cuMemFree(g_cuda.batch_offsets_device);
			g_cuda.batch_offsets_device = 0;
		}
		if (g_cuda.batch_lengths_device_alt) {
			g_cuda.cuMemFree(g_cuda.batch_lengths_device_alt);
			g_cuda.batch_lengths_device_alt = 0;
		}
		if (g_cuda.batch_lengths_device) {
			g_cuda.cuMemFree(g_cuda.batch_lengths_device);
			g_cuda.batch_lengths_device = 0;
		}
		if (g_cuda.batch_results_device_alt) {
			g_cuda.cuMemFree(g_cuda.batch_results_device_alt);
			g_cuda.batch_results_device_alt = 0;
		}
		if (g_cuda.batch_results_device) {
			g_cuda.cuMemFree(g_cuda.batch_results_device);
			g_cuda.batch_results_device = 0;
		}

		if (g_cuda.cuMemAlloc(&g_cuda.batch_offsets_device,
				sizeof(int) * (size_t)task_count) != CUDA_SUCCESS) {
			g_cuda.batch_task_capacity = 0;
			return -1;
		}
		if (g_cuda.cuMemAlloc(&g_cuda.batch_offsets_device_alt,
				sizeof(int) * (size_t)task_count) != CUDA_SUCCESS) {
			g_cuda.cuMemFree(g_cuda.batch_offsets_device);
			g_cuda.batch_offsets_device = 0;
			g_cuda.batch_task_capacity = 0;
			return -1;
		}
		if (g_cuda.cuMemAlloc(&g_cuda.batch_lengths_device,
				sizeof(int) * (size_t)task_count) != CUDA_SUCCESS) {
			g_cuda.cuMemFree(g_cuda.batch_offsets_device_alt);
			g_cuda.batch_offsets_device_alt = 0;
			g_cuda.cuMemFree(g_cuda.batch_offsets_device);
			g_cuda.batch_offsets_device = 0;
			g_cuda.batch_task_capacity = 0;
			return -1;
		}
		if (g_cuda.cuMemAlloc(&g_cuda.batch_lengths_device_alt,
				sizeof(int) * (size_t)task_count) != CUDA_SUCCESS) {
			g_cuda.cuMemFree(g_cuda.batch_lengths_device);
			g_cuda.batch_lengths_device = 0;
			g_cuda.cuMemFree(g_cuda.batch_offsets_device_alt);
			g_cuda.batch_offsets_device_alt = 0;
			g_cuda.cuMemFree(g_cuda.batch_offsets_device);
			g_cuda.batch_offsets_device = 0;
			g_cuda.batch_task_capacity = 0;
			return -1;
		}
		if (g_cuda.cuMemAlloc(&g_cuda.batch_results_device,
				sizeof(struct fastcdc_gpu_kernel_result) * (size_t)task_count) != CUDA_SUCCESS) {
			g_cuda.cuMemFree(g_cuda.batch_lengths_device_alt);
			g_cuda.batch_lengths_device_alt = 0;
			g_cuda.cuMemFree(g_cuda.batch_lengths_device);
			g_cuda.batch_lengths_device = 0;
			g_cuda.cuMemFree(g_cuda.batch_offsets_device_alt);
			g_cuda.batch_offsets_device_alt = 0;
			g_cuda.cuMemFree(g_cuda.batch_offsets_device);
			g_cuda.batch_offsets_device = 0;
			g_cuda.batch_task_capacity = 0;
			return -1;
		}
		if (g_cuda.cuMemAlloc(&g_cuda.batch_results_device_alt,
				sizeof(struct fastcdc_gpu_kernel_result) * (size_t)task_count) != CUDA_SUCCESS) {
			g_cuda.cuMemFree(g_cuda.batch_results_device);
			g_cuda.batch_results_device = 0;
			g_cuda.cuMemFree(g_cuda.batch_lengths_device_alt);
			g_cuda.batch_lengths_device_alt = 0;
			g_cuda.cuMemFree(g_cuda.batch_lengths_device);
			g_cuda.batch_lengths_device = 0;
			g_cuda.cuMemFree(g_cuda.batch_offsets_device_alt);
			g_cuda.batch_offsets_device_alt = 0;
			g_cuda.cuMemFree(g_cuda.batch_offsets_device);
			g_cuda.batch_offsets_device = 0;
			g_cuda.batch_task_capacity = 0;
			return -1;
		}
		g_cuda.batch_task_capacity = task_count;
	}
	return 0;
}

#define LOAD_CUDA_SYMBOL(sym) \
	do { \
		g_cuda.sym = (sym##_t)dlsym(g_cuda.handle, #sym); \
		if (!g_cuda.sym) { \
			WARNING("FastCDC GPU: failed to load CUDA symbol %s", #sym); \
			fastcdc_gpu_release_driver(); \
			return -1; \
		} \
	} while (0)

#define LOAD_CUDA_SYMBOL_ANY(field, type, primary, secondary) \
	do { \
		g_cuda.field = (type)fastcdc_gpu_load_symbol_any(primary, secondary); \
		if (!g_cuda.field) { \
			WARNING("FastCDC GPU: failed to load CUDA symbol %s", primary); \
			fastcdc_gpu_release_driver(); \
			return -1; \
		} \
	} while (0)

static int fastcdc_gpu_prepare_kernel() {
	CUresult rc;

	if (g_cuda.kernel_ready) {
		return 0;
	}

	if (cuda_driver_init_context() != 0) {
		return -1;
	}

	if (!g_cuda.module) {
		if (fastcdc_gpu_resolve_ptx_path(g_cuda.ptx_path, sizeof(g_cuda.ptx_path)) != 0) {
			WARNING("FastCDC GPU: PTX file not found. Build src/chunking/fastcdc_gpu_kernel.ptx first.");
			fastcdc_gpu_release_driver();
			return -1;
		}

		rc = g_cuda.cuModuleLoad(&g_cuda.module, g_cuda.ptx_path);
		if (rc != CUDA_SUCCESS || !g_cuda.module) {
			WARNING("FastCDC GPU: cuModuleLoad failed for %s: %s",
					g_cuda.ptx_path,
					fastcdc_cuda_error_string(rc));
			fastcdc_gpu_release_driver();
			return -1;
		}

		gear_matrix_init();
		rc = g_cuda.cuMemAlloc(&g_cuda.gear_matrix_device,
				sizeof(g_gear_matrix[0]) * FASTCDC_GPU_GEAR_SYMBOL_COUNT);
		if (rc != CUDA_SUCCESS || !g_cuda.gear_matrix_device) {
			WARNING("FastCDC GPU: cuMemAlloc for gear matrix failed: %s",
					fastcdc_cuda_error_string(rc));
			fastcdc_gpu_release_driver();
			return -1;
		}

		rc = g_cuda.cuMemcpyHtoD(g_cuda.gear_matrix_device,
				g_gear_matrix,
				sizeof(g_gear_matrix[0]) * FASTCDC_GPU_GEAR_SYMBOL_COUNT);
		if (rc != CUDA_SUCCESS) {
			WARNING("FastCDC GPU: cuMemcpyHtoD for gear matrix failed: %s",
					fastcdc_cuda_error_string(rc));
			fastcdc_gpu_release_driver();
			return -1;
		}

		NOTICE("Chunk GPU: PTX module loaded from %s", g_cuda.ptx_path);
	}

	rc = g_cuda.cuModuleGetFunction(&g_cuda.fastcdc_kernel,
			g_cuda.module,
			FASTCDC_GPU_KERNEL_SYMBOL);
	if (rc != CUDA_SUCCESS || !g_cuda.fastcdc_kernel) {
		WARNING("FastCDC GPU: cuModuleGetFunction(%s) failed: %s",
				FASTCDC_GPU_KERNEL_SYMBOL,
				fastcdc_cuda_error_string(rc));
		return -1;
	}

	g_cuda.kernel_ready = 1;
	return 0;
}

static int jc_gpu_prepare_kernel() {
	CUresult rc;

	if (g_cuda.jc_kernel_ready) {
		return 0;
	}
	if (fastcdc_gpu_prepare_kernel() != 0) {
		return -1;
	}
	rc = g_cuda.cuModuleGetFunction(&g_cuda.jc_kernel,
			g_cuda.module,
			JC_GPU_KERNEL_SYMBOL);
	if (rc != CUDA_SUCCESS || !g_cuda.jc_kernel) {
		WARNING("JC GPU: cuModuleGetFunction(%s) failed: %s",
				JC_GPU_KERNEL_SYMBOL,
				fastcdc_cuda_error_string(rc));
		return -1;
	}
	g_cuda.jc_kernel_ready = 1;
	return 0;
}

static int cuda_driver_init_context() {
	if (g_cuda.initialized) {
		return 0;
	}

	fastcdc_gpu_reset_state();
	g_cuda.handle = dlopen("libcuda.so.1", RTLD_NOW);
	if (!g_cuda.handle) {
		WARNING("Chunk GPU: CUDA driver not found (libcuda.so.1). Falling back to CPU.");
		return -1;
	}

	LOAD_CUDA_SYMBOL(cuInit);
	LOAD_CUDA_SYMBOL(cuDeviceGetCount);
	LOAD_CUDA_SYMBOL(cuDeviceGet);
	LOAD_CUDA_SYMBOL_ANY(cuCtxCreate, cuCtxCreate_t, "cuCtxCreate_v2", "cuCtxCreate");
	LOAD_CUDA_SYMBOL_ANY(cuCtxDestroy, cuCtxDestroy_t, "cuCtxDestroy_v2", "cuCtxDestroy");
	LOAD_CUDA_SYMBOL(cuCtxSetCurrent);
	LOAD_CUDA_SYMBOL(cuModuleLoad);
	LOAD_CUDA_SYMBOL(cuModuleUnload);
	LOAD_CUDA_SYMBOL(cuModuleGetFunction);
	LOAD_CUDA_SYMBOL_ANY(cuMemAlloc, cuMemAlloc_t, "cuMemAlloc_v2", "cuMemAlloc");
	LOAD_CUDA_SYMBOL_ANY(cuMemFree, cuMemFree_t, "cuMemFree_v2", "cuMemFree");
	LOAD_CUDA_SYMBOL_ANY(cuMemHostAlloc, cuMemHostAlloc_t, "cuMemHostAlloc_v2", "cuMemHostAlloc");
	LOAD_CUDA_SYMBOL_ANY(cuMemFreeHost, cuMemFreeHost_t, "cuMemFreeHost_v2", "cuMemFreeHost");
	LOAD_CUDA_SYMBOL_ANY(cuMemcpyHtoD, cuMemcpyHtoD_t, "cuMemcpyHtoD_v2", "cuMemcpyHtoD");
	LOAD_CUDA_SYMBOL_ANY(cuMemcpyDtoH, cuMemcpyDtoH_t, "cuMemcpyDtoH_v2", "cuMemcpyDtoH");
	LOAD_CUDA_SYMBOL_ANY(cuMemcpyHtoDAsync, cuMemcpyHtoDAsync_t, "cuMemcpyHtoDAsync_v2", "cuMemcpyHtoDAsync");
	LOAD_CUDA_SYMBOL_ANY(cuMemcpyDtoHAsync, cuMemcpyDtoHAsync_t, "cuMemcpyDtoHAsync_v2", "cuMemcpyDtoHAsync");
	LOAD_CUDA_SYMBOL(cuLaunchKernel);
	LOAD_CUDA_SYMBOL_ANY(cuStreamCreate, cuStreamCreate_t, "cuStreamCreate_v2", "cuStreamCreate");
	LOAD_CUDA_SYMBOL_ANY(cuStreamDestroy, cuStreamDestroy_t, "cuStreamDestroy_v2", "cuStreamDestroy");
	LOAD_CUDA_SYMBOL_ANY(cuStreamSynchronize, cuStreamSynchronize_t, "cuStreamSynchronize_v2", "cuStreamSynchronize");
	LOAD_CUDA_SYMBOL_ANY(cuEventCreate, cuEventCreate_t, "cuEventCreate_v2", "cuEventCreate");
	LOAD_CUDA_SYMBOL_ANY(cuEventDestroy, cuEventDestroy_t, "cuEventDestroy_v2", "cuEventDestroy");
	LOAD_CUDA_SYMBOL_ANY(cuEventRecord, cuEventRecord_t, "cuEventRecord_v2", "cuEventRecord");
	LOAD_CUDA_SYMBOL_ANY(cuEventSynchronize, cuEventSynchronize_t, "cuEventSynchronize_v2", "cuEventSynchronize");
	LOAD_CUDA_SYMBOL_ANY(cuEventElapsedTime, cuEventElapsedTime_t, "cuEventElapsedTime_v2", "cuEventElapsedTime");
	LOAD_CUDA_SYMBOL(cuCtxSynchronize);
	g_cuda.cuGetErrorString = (cuGetErrorString_t)dlsym(g_cuda.handle, "cuGetErrorString");

	CUresult rc = g_cuda.cuInit(0);
	if (rc != CUDA_SUCCESS) {
		WARNING("Chunk GPU: cuInit failed: %s", fastcdc_cuda_error_string(rc));
		fastcdc_gpu_release_driver();
		return -1;
	}

	int device_count = 0;
	rc = g_cuda.cuDeviceGetCount(&device_count);
	if (rc != CUDA_SUCCESS || device_count <= 0) {
		WARNING("Chunk GPU: no usable CUDA device found");
		fastcdc_gpu_release_driver();
		return -1;
	}

	if (destor.chunk_gpu_device_id < 0 || destor.chunk_gpu_device_id >= device_count) {
		WARNING("Chunk GPU: device id %d out of range [0, %d), falling back to CPU",
				destor.chunk_gpu_device_id, device_count);
		fastcdc_gpu_release_driver();
		return -1;
	}

	CUdevice dev = 0;
	rc = g_cuda.cuDeviceGet(&dev, destor.chunk_gpu_device_id);
	if (rc != CUDA_SUCCESS) {
		WARNING("Chunk GPU: cuDeviceGet failed: %s", fastcdc_cuda_error_string(rc));
		fastcdc_gpu_release_driver();
		return -1;
	}

	rc = g_cuda.cuCtxCreate(&g_cuda.ctx, 0, dev);
	if (rc != CUDA_SUCCESS || !g_cuda.ctx) {
		WARNING("Chunk GPU: cuCtxCreate failed: %s", fastcdc_cuda_error_string(rc));
		fastcdc_gpu_release_driver();
		return -1;
	}
	rc = g_cuda.cuStreamCreate(&g_cuda.batch_stream, 0);
	if (rc != CUDA_SUCCESS || !g_cuda.batch_stream) {
		WARNING("Chunk GPU: cuStreamCreate failed: %s", fastcdc_cuda_error_string(rc));
		fastcdc_gpu_release_driver();
		return -1;
	}
	rc = g_cuda.cuStreamCreate(&g_cuda.batch_stream_alt, 0);
	if (rc != CUDA_SUCCESS || !g_cuda.batch_stream_alt) {
		WARNING("Chunk GPU: secondary cuStreamCreate failed: %s", fastcdc_cuda_error_string(rc));
		fastcdc_gpu_release_driver();
		return -1;
	}

	g_cuda.initialized = 1;
	NOTICE("Chunk GPU: CUDA context initialized on device %d", destor.chunk_gpu_device_id);
	return 0;
}

int fastcdc_gpu_init() {
	return fastcdc_gpu_prepare_kernel();
}

int fastcdc_gpu_is_ready() {
	return g_cuda.kernel_ready;
}

void fastcdc_gpu_close() {
	if (!g_cuda.initialized && !g_cuda.handle) {
		return;
	}
	fastcdc_gpu_release_driver();
}

static void fastcdc_gpu_chunk_batch_cpu_fallback(unsigned char **buffers,
		const int *sizes,
		int task_count,
		int *chunk_sizes) {
	int i;

	for (i = 0; i < task_count; i++) {
		chunk_sizes[i] = fastcdc_chunk_data(buffers[i], sizes[i]);
	}
}

static void jc_gpu_chunk_batch_cpu_fallback(unsigned char **buffers,
		const int *sizes,
		int task_count,
		int *chunk_sizes) {
	int i;

	for (i = 0; i < task_count; i++) {
		chunk_sizes[i] = gearjump_chunk_data(buffers[i], sizes[i]);
	}
}

int fastcdc_gpu_chunk_batch(unsigned char **buffers,
		const int *sizes,
		int task_count,
		int *chunk_sizes) {
	CUresult rc = CUDA_SUCCESS;
	CUevent start_event[2] = { NULL, NULL };
	CUevent stop_event[2] = { NULL, NULL };
	CUstream streams[2];
	CUdeviceptr input_devices[2];
	CUdeviceptr offsets_devices[2];
	CUdeviceptr lengths_devices[2];
	CUdeviceptr results_devices[2];
	int *offsets_host[2];
	int *lengths_host[2];
	struct fastcdc_gpu_kernel_result *results_host[2];
	int slot_start[2] = { 0, 0 };
	int slot_count[2] = { 0, 0 };
	int slot_busy[2] = { 0, 0 };
	uint64_t mask_s = 0;
	uint64_t mask_l = 0;
	int expect_size = 0;
	int warp_window = destor.chunk_warp_window;
	int threads_per_block = 128;
	int pipeline_tasks = 256;
	int active_slots = 0;
	int next_task = 0;
	int i;
	int ok = 1;
	size_t total_bytes = 0;
	float kernel_ms = 0.0f;

	if (!buffers || !sizes || !chunk_sizes || task_count <= 0) {
		return -1;
	}

	if (!g_cuda.kernel_ready) {
		fastcdc_gpu_chunk_batch_cpu_fallback(buffers, sizes, task_count, chunk_sizes);
		return 0;
	}
	if (fastcdc_gpu_activate_context() != 0) {
		fastcdc_gpu_chunk_batch_cpu_fallback(buffers, sizes, task_count, chunk_sizes);
		return 0;
	}

	for (i = 0; i < task_count; i++) {
		int copy_len = sizes[i] < destor.chunk_max_size ? sizes[i] : destor.chunk_max_size;

		if (copy_len < 0) {
			copy_len = 0;
		}
		total_bytes += (size_t)copy_len;
	}
	if (fastcdc_gpu_ensure_batch_capacity(total_bytes, task_count) != 0) {
		ok = 0;
		goto done;
	}

	if (total_bytes == 0) {
		for (i = 0; i < task_count; i++) {
			chunk_sizes[i] = 0;
		}
		goto done;
	}

	fastcdc_gpu_compute_masks(&mask_s, &mask_l, &expect_size);
	if (pipeline_tasks > task_count) {
		pipeline_tasks = task_count;
	}
	if (pipeline_tasks <= 0) {
		pipeline_tasks = 1;
	}

	streams[0] = g_cuda.batch_stream;
	streams[1] = g_cuda.batch_stream_alt;
	input_devices[0] = g_cuda.batch_input_device;
	input_devices[1] = g_cuda.batch_input_device_alt;
	offsets_devices[0] = g_cuda.batch_offsets_device;
	offsets_devices[1] = g_cuda.batch_offsets_device_alt;
	lengths_devices[0] = g_cuda.batch_lengths_device;
	lengths_devices[1] = g_cuda.batch_lengths_device_alt;
	results_devices[0] = g_cuda.batch_results_device;
	results_devices[1] = g_cuda.batch_results_device_alt;
	offsets_host[0] = g_cuda.batch_offsets_host;
	offsets_host[1] = g_cuda.batch_offsets_host_alt;
	lengths_host[0] = g_cuda.batch_lengths_host;
	lengths_host[1] = g_cuda.batch_lengths_host_alt;
	results_host[0] = g_cuda.batch_results_host;
	results_host[1] = g_cuda.batch_results_host_alt;

	while (next_task < task_count || active_slots > 0) {
		while (next_task < task_count && active_slots < 2) {
			int slot = slot_busy[0] ? 1 : 0;
			int local_count = task_count - next_task;
			int local_blocks;
			size_t local_bytes = 0;

			if (local_count > pipeline_tasks) {
				local_count = pipeline_tasks;
			}
			for (i = 0; i < local_count; i++) {
				int idx = next_task + i;
				int copy_len = sizes[idx] < destor.chunk_max_size ? sizes[idx] : destor.chunk_max_size;

				if (copy_len < 0) {
					copy_len = 0;
				}
				offsets_host[slot][i] = (int)local_bytes;
				lengths_host[slot][i] = copy_len;
				local_bytes += (size_t)copy_len;
			}
			for (i = 0; i < local_count; i++) {
				if (lengths_host[slot][i] <= 0) {
					continue;
				}
				rc = g_cuda.cuMemcpyHtoDAsync(input_devices[slot] + (CUdeviceptr)offsets_host[slot][i],
						buffers[next_task + i],
						(size_t)lengths_host[slot][i],
						streams[slot]);
				if (rc != CUDA_SUCCESS) {
					ok = 0;
					goto done;
				}
			}
			rc = g_cuda.cuMemcpyHtoDAsync(offsets_devices[slot],
					offsets_host[slot],
					sizeof(int) * (size_t)local_count,
					streams[slot]);
			if (rc != CUDA_SUCCESS) {
				ok = 0;
				goto done;
			}
			rc = g_cuda.cuMemcpyHtoDAsync(lengths_devices[slot],
					lengths_host[slot],
					sizeof(int) * (size_t)local_count,
					streams[slot]);
			if (rc != CUDA_SUCCESS) {
				ok = 0;
				goto done;
			}
			if (g_cuda.cuEventCreate && g_cuda.cuEventRecord && g_cuda.cuEventSynchronize
					&& g_cuda.cuEventElapsedTime && g_cuda.cuEventDestroy) {
				rc = g_cuda.cuEventCreate(&start_event[slot], 0);
				if (rc != CUDA_SUCCESS) {
					ok = 0;
					goto done;
				}
				rc = g_cuda.cuEventCreate(&stop_event[slot], 0);
				if (rc != CUDA_SUCCESS) {
					ok = 0;
					goto done;
				}
				rc = g_cuda.cuEventRecord(start_event[slot], streams[slot]);
				if (rc != CUDA_SUCCESS) {
					ok = 0;
					goto done;
				}
			}

			local_blocks = (local_count + threads_per_block - 1) / threads_per_block;
			{
				void *kernel_params[13];
				kernel_params[0] = &input_devices[slot];
				kernel_params[1] = &offsets_devices[slot];
				kernel_params[2] = &lengths_devices[slot];
				kernel_params[3] = &local_count;
				kernel_params[4] = &g_cuda.gear_matrix_device;
				kernel_params[5] = &destor.chunk_min_size;
				kernel_params[6] = &destor.chunk_max_size;
				kernel_params[7] = &expect_size;
				kernel_params[8] = &mask_s;
				kernel_params[9] = &mask_l;
				kernel_params[10] = &warp_window;
				kernel_params[11] = &results_devices[slot];
				kernel_params[12] = NULL;

				rc = g_cuda.cuLaunchKernel(g_cuda.fastcdc_kernel,
						(unsigned int)local_blocks,
						1,
						1,
						(unsigned int)threads_per_block,
						1,
						1,
						0,
						streams[slot],
						kernel_params,
						NULL);
			}
			if (rc == CUDA_SUCCESS && start_event[slot] && stop_event[slot]) {
				rc = g_cuda.cuEventRecord(stop_event[slot], streams[slot]);
			}
			if (rc == CUDA_SUCCESS) {
				rc = g_cuda.cuMemcpyDtoHAsync(results_host[slot],
						results_devices[slot],
						sizeof(struct fastcdc_gpu_kernel_result) * (size_t)local_count,
						streams[slot]);
			}
			if (rc != CUDA_SUCCESS) {
				ok = 0;
				goto done;
			}

			slot_start[slot] = next_task;
			slot_count[slot] = local_count;
			slot_busy[slot] = 1;
			next_task += local_count;
			active_slots++;
		}

		if (active_slots > 0) {
			int slot = slot_busy[0] ? 0 : 1;

			rc = g_cuda.cuStreamSynchronize(streams[slot]);
			if (rc != CUDA_SUCCESS) {
				ok = 0;
				goto done;
			}
			if (start_event[slot] && stop_event[slot]) {
				rc = g_cuda.cuEventElapsedTime(&kernel_ms, start_event[slot], stop_event[slot]);
				if (rc != CUDA_SUCCESS) {
					ok = 0;
					goto done;
				}
				g_cuda.batch_compute_ms_accum += (double)kernel_ms;
			}
			for (i = 0; i < slot_count[slot]; i++) {
				int idx = slot_start[slot] + i;
				if (results_host[slot][i].chunk_size <= 0 || results_host[slot][i].chunk_size > sizes[idx]) {
					chunk_sizes[idx] = fastcdc_chunk_data(buffers[idx], sizes[idx]);
				} else {
					chunk_sizes[idx] = results_host[slot][i].chunk_size;
				}
				fastcdc_gpu_note_kernel_result(results_host[slot] + i);
			}
			if (start_event[slot] && g_cuda.cuEventDestroy) {
				g_cuda.cuEventDestroy(start_event[slot]);
				start_event[slot] = NULL;
			}
			if (stop_event[slot] && g_cuda.cuEventDestroy) {
				g_cuda.cuEventDestroy(stop_event[slot]);
				stop_event[slot] = NULL;
			}
			slot_busy[slot] = 0;
			active_slots--;
		}
	}

done:
	for (i = 0; i < 2; i++) {
		if (start_event[i] && g_cuda.cuEventDestroy) {
			g_cuda.cuEventDestroy(start_event[i]);
		}
		if (stop_event[i] && g_cuda.cuEventDestroy) {
			g_cuda.cuEventDestroy(stop_event[i]);
		}
	}
	if (!ok) {
		WARNING("FastCDC GPU batch: launch/copy failed, falling back to CPU: %s",
				fastcdc_cuda_error_string(rc));
		fastcdc_gpu_chunk_batch_cpu_fallback(buffers, sizes, task_count, chunk_sizes);
	}
	return 0;
}

int fastcdc_gpu_chunk_data(unsigned char *p, int n) {
	unsigned char *buffers[1];
	int sizes[1];
	int chunk_sizes[1];

	buffers[0] = p;
	sizes[0] = n;
	chunk_sizes[0] = 0;
	if (fastcdc_gpu_chunk_batch(buffers, sizes, 1, chunk_sizes) != 0) {
		return fastcdc_chunk_data(p, n);
	}
	return chunk_sizes[0];
}

int jc_gpu_init() {
	return jc_gpu_prepare_kernel();
}

void jc_gpu_close() {
	fastcdc_gpu_close();
}

int jc_gpu_chunk_batch(unsigned char **buffers,
		const int *sizes,
		int task_count,
		int *chunk_sizes) {
	CUresult rc = CUDA_SUCCESS;
	CUevent start_event[2] = { NULL, NULL };
	CUevent stop_event[2] = { NULL, NULL };
	CUstream streams[2];
	CUdeviceptr input_devices[2];
	CUdeviceptr offsets_devices[2];
	CUdeviceptr lengths_devices[2];
	CUdeviceptr results_devices[2];
	int *offsets_host[2];
	int *lengths_host[2];
	struct fastcdc_gpu_kernel_result *results_host[2];
	int slot_start[2] = { 0, 0 };
	int slot_count[2] = { 0, 0 };
	int slot_busy[2] = { 0, 0 };
	uint64_t mask = 0;
	uint64_t jump_mask = 0;
	int jump_len = 0;
	int expect_size = 0;
	int warp_window = destor.chunk_warp_window;
	int threads_per_block = 128;
	int pipeline_tasks = 256;
	int active_slots = 0;
	int next_task = 0;
	int i;
	int ok = 1;
	size_t total_bytes = 0;
	float kernel_ms = 0.0f;

	if (!buffers || !sizes || !chunk_sizes || task_count <= 0) {
		return -1;
	}

	if (!g_cuda.jc_kernel_ready) {
		jc_gpu_chunk_batch_cpu_fallback(buffers, sizes, task_count, chunk_sizes);
		return 0;
	}
	if (fastcdc_gpu_activate_context() != 0) {
		jc_gpu_chunk_batch_cpu_fallback(buffers, sizes, task_count, chunk_sizes);
		return 0;
	}
	if (jc_gpu_compute_params(&mask, &jump_mask, &jump_len, &expect_size) != 0 || jump_len <= 0) {
		jc_gpu_chunk_batch_cpu_fallback(buffers, sizes, task_count, chunk_sizes);
		return 0;
	}

	for (i = 0; i < task_count; i++) {
		int copy_len = sizes[i] < destor.chunk_max_size ? sizes[i] : destor.chunk_max_size;

		if (copy_len < 0) {
			copy_len = 0;
		}
		total_bytes += (size_t)copy_len;
	}
	if (fastcdc_gpu_ensure_batch_capacity(total_bytes, task_count) != 0) {
		ok = 0;
		goto done;
	}

	if (total_bytes == 0) {
		for (i = 0; i < task_count; i++) {
			chunk_sizes[i] = 0;
		}
		goto done;
	}
	if (pipeline_tasks > task_count) {
		pipeline_tasks = task_count;
	}
	if (pipeline_tasks <= 0) {
		pipeline_tasks = 1;
	}

	streams[0] = g_cuda.batch_stream;
	streams[1] = g_cuda.batch_stream_alt;
	input_devices[0] = g_cuda.batch_input_device;
	input_devices[1] = g_cuda.batch_input_device_alt;
	offsets_devices[0] = g_cuda.batch_offsets_device;
	offsets_devices[1] = g_cuda.batch_offsets_device_alt;
	lengths_devices[0] = g_cuda.batch_lengths_device;
	lengths_devices[1] = g_cuda.batch_lengths_device_alt;
	results_devices[0] = g_cuda.batch_results_device;
	results_devices[1] = g_cuda.batch_results_device_alt;
	offsets_host[0] = g_cuda.batch_offsets_host;
	offsets_host[1] = g_cuda.batch_offsets_host_alt;
	lengths_host[0] = g_cuda.batch_lengths_host;
	lengths_host[1] = g_cuda.batch_lengths_host_alt;
	results_host[0] = g_cuda.batch_results_host;
	results_host[1] = g_cuda.batch_results_host_alt;

	while (next_task < task_count || active_slots > 0) {
		while (next_task < task_count && active_slots < 2) {
			int slot = slot_busy[0] ? 1 : 0;
			int local_count = task_count - next_task;
			int local_blocks;
			size_t local_bytes = 0;

			if (local_count > pipeline_tasks) {
				local_count = pipeline_tasks;
			}
			for (i = 0; i < local_count; i++) {
				int idx = next_task + i;
				int copy_len = sizes[idx] < destor.chunk_max_size ? sizes[idx] : destor.chunk_max_size;

				if (copy_len < 0) {
					copy_len = 0;
				}
				offsets_host[slot][i] = (int)local_bytes;
				lengths_host[slot][i] = copy_len;
				local_bytes += (size_t)copy_len;
			}
			for (i = 0; i < local_count; i++) {
				if (lengths_host[slot][i] <= 0) {
					continue;
				}
				rc = g_cuda.cuMemcpyHtoDAsync(input_devices[slot] + (CUdeviceptr)offsets_host[slot][i],
						buffers[next_task + i],
						(size_t)lengths_host[slot][i],
						streams[slot]);
				if (rc != CUDA_SUCCESS) {
					ok = 0;
					goto done;
				}
			}
			rc = g_cuda.cuMemcpyHtoDAsync(offsets_devices[slot],
					offsets_host[slot],
					sizeof(int) * (size_t)local_count,
					streams[slot]);
			if (rc != CUDA_SUCCESS) {
				ok = 0;
				goto done;
			}
			rc = g_cuda.cuMemcpyHtoDAsync(lengths_devices[slot],
					lengths_host[slot],
					sizeof(int) * (size_t)local_count,
					streams[slot]);
			if (rc != CUDA_SUCCESS) {
				ok = 0;
				goto done;
			}
			if (g_cuda.cuEventCreate && g_cuda.cuEventRecord && g_cuda.cuEventSynchronize
					&& g_cuda.cuEventElapsedTime && g_cuda.cuEventDestroy) {
				rc = g_cuda.cuEventCreate(&start_event[slot], 0);
				if (rc != CUDA_SUCCESS) {
					ok = 0;
					goto done;
				}
				rc = g_cuda.cuEventCreate(&stop_event[slot], 0);
				if (rc != CUDA_SUCCESS) {
					ok = 0;
					goto done;
				}
				rc = g_cuda.cuEventRecord(start_event[slot], streams[slot]);
				if (rc != CUDA_SUCCESS) {
					ok = 0;
					goto done;
				}
			}

			local_blocks = (local_count + threads_per_block - 1) / threads_per_block;
			{
				void *kernel_params[13];
				kernel_params[0] = &input_devices[slot];
				kernel_params[1] = &offsets_devices[slot];
				kernel_params[2] = &lengths_devices[slot];
				kernel_params[3] = &local_count;
				kernel_params[4] = &g_cuda.gear_matrix_device;
				kernel_params[5] = &destor.chunk_min_size;
				kernel_params[6] = &destor.chunk_max_size;
				kernel_params[7] = &mask;
				kernel_params[8] = &jump_mask;
				kernel_params[9] = &jump_len;
				kernel_params[10] = &warp_window;
				kernel_params[11] = &results_devices[slot];
				kernel_params[12] = NULL;

				rc = g_cuda.cuLaunchKernel(g_cuda.jc_kernel,
						(unsigned int)local_blocks,
						1,
						1,
						(unsigned int)threads_per_block,
						1,
						1,
						0,
						streams[slot],
						kernel_params,
						NULL);
			}
			if (rc == CUDA_SUCCESS && start_event[slot] && stop_event[slot]) {
				rc = g_cuda.cuEventRecord(stop_event[slot], streams[slot]);
			}
			if (rc == CUDA_SUCCESS) {
				rc = g_cuda.cuMemcpyDtoHAsync(results_host[slot],
						results_devices[slot],
						sizeof(struct fastcdc_gpu_kernel_result) * (size_t)local_count,
						streams[slot]);
			}
			if (rc != CUDA_SUCCESS) {
				ok = 0;
				goto done;
			}

			slot_start[slot] = next_task;
			slot_count[slot] = local_count;
			slot_busy[slot] = 1;
			next_task += local_count;
			active_slots++;
		}

		if (active_slots > 0) {
			int slot = slot_busy[0] ? 0 : 1;

			rc = g_cuda.cuStreamSynchronize(streams[slot]);
			if (rc != CUDA_SUCCESS) {
				ok = 0;
				goto done;
			}
			if (start_event[slot] && stop_event[slot]) {
				rc = g_cuda.cuEventElapsedTime(&kernel_ms, start_event[slot], stop_event[slot]);
				if (rc != CUDA_SUCCESS) {
					ok = 0;
					goto done;
				}
				g_cuda.batch_compute_ms_accum += (double)kernel_ms;
			}
			for (i = 0; i < slot_count[slot]; i++) {
				int idx = slot_start[slot] + i;
				if (results_host[slot][i].chunk_size <= 0 || results_host[slot][i].chunk_size > sizes[idx]) {
					chunk_sizes[idx] = gearjump_chunk_data(buffers[idx], sizes[idx]);
				} else {
					chunk_sizes[idx] = results_host[slot][i].chunk_size;
				}
				fastcdc_gpu_note_kernel_result(results_host[slot] + i);
			}
			if (start_event[slot] && g_cuda.cuEventDestroy) {
				g_cuda.cuEventDestroy(start_event[slot]);
				start_event[slot] = NULL;
			}
			if (stop_event[slot] && g_cuda.cuEventDestroy) {
				g_cuda.cuEventDestroy(stop_event[slot]);
				stop_event[slot] = NULL;
			}
			slot_busy[slot] = 0;
			active_slots--;
		}
	}

done:
	for (i = 0; i < 2; i++) {
		if (start_event[i] && g_cuda.cuEventDestroy) {
			g_cuda.cuEventDestroy(start_event[i]);
		}
		if (stop_event[i] && g_cuda.cuEventDestroy) {
			g_cuda.cuEventDestroy(stop_event[i]);
		}
	}
	if (!ok) {
		WARNING("JC GPU batch: launch/copy failed, falling back to CPU: %s",
				fastcdc_cuda_error_string(rc));
		jc_gpu_chunk_batch_cpu_fallback(buffers, sizes, task_count, chunk_sizes);
	}
	return 0;
}

int jc_gpu_chunk_data(unsigned char *p, int n) {
	unsigned char *buffers[1];
	int sizes[1];
	int chunk_sizes[1];

	buffers[0] = p;
	sizes[0] = n;
	chunk_sizes[0] = 0;
	if (jc_gpu_chunk_batch(buffers, sizes, 1, chunk_sizes) != 0) {
		return gearjump_chunk_data(p, n);
	}
	return chunk_sizes[0];
}
