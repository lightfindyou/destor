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

extern int fastcdc_gpu_naive_mode_enabled(void);
extern int fastcdc_gpu_naive_init(void);
extern void fastcdc_gpu_naive_close(void);
extern int fastcdc_gpu_naive_is_ready(void);
extern int fastcdc_gpu_naive_chunk_data(unsigned char *p, int n);
extern int gear_gpu_naive_chunk_data(unsigned char *p, int n);
extern int jc_gpu_naive_chunk_data(unsigned char *p, int n);
extern void fastcdc_gpu_set_naive_algorithm(int algorithm);

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
#define GEAR_GPU_KERNEL_SYMBOL "gear_chunk_kernel"
#define FASTCDC_GPU_GEAR_SYMBOL_COUNT 256
#define FASTCDC_GPU_FASTCDC_PIPELINE_TASKS_DEFAULT 256
#define FASTCDC_GPU_JC_PIPELINE_TASKS_DEFAULT 256
#define FASTCDC_GPU_PIPELINE_TASKS_MIN 1
#define FASTCDC_GPU_PIPELINE_TASKS_MAX 4096
#define FASTCDC_GPU_THREADS_PER_BLOCK_DEFAULT 128
#define FASTCDC_GPU_THREADS_PER_BLOCK_MIN 32
#define FASTCDC_GPU_THREADS_PER_BLOCK_MAX 1024
#define FASTCDC_GPU_SEGMENT_BYTES_DEFAULT DEFAULT_BLOCK_SIZE

enum gpu_batch_kernel_kind {
	GPU_BATCH_KERNEL_FASTCDC = 0,
	GPU_BATCH_KERNEL_JC = 1,
	GPU_BATCH_KERNEL_GEAR = 2,
};

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

struct fastcdc_gpu_segment_result {
	int boundary_count;
	int consumed_bytes;
};

struct cuda_driver_state {
	void *handle;
	CUcontext ctx;
	CUmodule module;
	CUfunction fastcdc_kernel;
	CUfunction jc_kernel;
	CUfunction gear_kernel;
	CUdeviceptr gear_matrix_device;
	CUdeviceptr batch_input_device;
	CUdeviceptr batch_input_device_alt;
	CUdeviceptr batch_offsets_device;
	CUdeviceptr batch_offsets_device_alt;
	CUdeviceptr batch_lengths_device;
	CUdeviceptr batch_lengths_device_alt;
	CUdeviceptr batch_target_lengths_device;
	CUdeviceptr batch_target_lengths_device_alt;
	CUdeviceptr batch_results_device;
	CUdeviceptr batch_results_device_alt;
	CUdeviceptr batch_boundaries_device;
	CUdeviceptr batch_boundaries_device_alt;
	CUstream batch_stream;
	CUstream batch_stream_alt;
	CUevent batch_start_event[2];
	CUevent batch_stop_event[2];
	size_t batch_input_capacity;
	int batch_task_capacity;
	unsigned char *batch_input_host;
	unsigned char *batch_input_host_alt;
	int *batch_offsets_host;
	int *batch_offsets_host_alt;
	int *batch_lengths_host;
	int *batch_lengths_host_alt;
	int *batch_target_lengths_host;
	int *batch_target_lengths_host_alt;
	struct fastcdc_gpu_segment_result *batch_results_host;
	struct fastcdc_gpu_segment_result *batch_results_host_alt;
	int *batch_boundaries_host;
	int *batch_boundaries_host_alt;
	int initialized;
	int kernel_ready;
	int jc_kernel_ready;
	int gear_kernel_ready;
	int pipeline_tasks;
	int threads_per_block;
	int batch_boundary_stride;
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

int fastcdc_gpu_segment_bytes(void) {
	return FASTCDC_GPU_SEGMENT_BYTES_DEFAULT;
}

static int fastcdc_gpu_segment_window_bytes(void) {
	int segment_bytes = fastcdc_gpu_segment_bytes();

	if (destor.chunk_max_size > 0 && segment_bytes <= INT_MAX - destor.chunk_max_size) {
		segment_bytes += destor.chunk_max_size;
	}
	return segment_bytes;
}

int fastcdc_gpu_segment_boundary_limit(int segment_bytes) {
	int min_size = destor.chunk_min_size > 0 ? destor.chunk_min_size : 1;
	int limit;

	if (segment_bytes <= 0) {
		segment_bytes = fastcdc_gpu_segment_window_bytes();
	}
	limit = segment_bytes / min_size + 2;
	if (limit < 2) {
		limit = 2;
	}
	return limit;
}

static int fastcdc_gpu_clamp_pipeline_tasks(int tasks) {
	if (tasks < FASTCDC_GPU_PIPELINE_TASKS_MIN) {
		return FASTCDC_GPU_PIPELINE_TASKS_MIN;
	}
	if (tasks > FASTCDC_GPU_PIPELINE_TASKS_MAX) {
		return FASTCDC_GPU_PIPELINE_TASKS_MAX;
	}
	return tasks;
}

static int fastcdc_gpu_clamp_threads_per_block(int threads) {
	int clamped;

	if (threads < FASTCDC_GPU_THREADS_PER_BLOCK_MIN) {
		threads = FASTCDC_GPU_THREADS_PER_BLOCK_MIN;
	}
	if (threads > FASTCDC_GPU_THREADS_PER_BLOCK_MAX) {
		threads = FASTCDC_GPU_THREADS_PER_BLOCK_MAX;
	}
	clamped = (threads / 32) * 32;
	if (clamped < FASTCDC_GPU_THREADS_PER_BLOCK_MIN) {
		clamped = FASTCDC_GPU_THREADS_PER_BLOCK_MIN;
	}
	return clamped;
}

static int fastcdc_gpu_runtime_pipeline_tasks(int default_tasks) {
	if (g_cuda.pipeline_tasks > 0) {
		return g_cuda.pipeline_tasks;
	}
	return fastcdc_gpu_clamp_pipeline_tasks(default_tasks);
}

static int fastcdc_gpu_runtime_threads_per_block(void) {
	if (g_cuda.threads_per_block > 0) {
		return g_cuda.threads_per_block;
	}
	return FASTCDC_GPU_THREADS_PER_BLOCK_DEFAULT;
}

static size_t fastcdc_gpu_kernel_shared_bytes(int threads_per_block) {
	if (threads_per_block <= 0) {
		threads_per_block = FASTCDC_GPU_THREADS_PER_BLOCK_DEFAULT;
	}
	return sizeof(unsigned long long) * 4U * (size_t)threads_per_block;
}

void fastcdc_gpu_set_threads_per_block(int threads) {
	if (threads <= 0) {
		g_cuda.threads_per_block = 0;
		return;
	}
	g_cuda.threads_per_block = fastcdc_gpu_clamp_threads_per_block(threads);
}

void fastcdc_gpu_set_pipeline_tasks(int tasks) {
	if (tasks <= 0) {
		g_cuda.pipeline_tasks = 0;
		return;
	}
	g_cuda.pipeline_tasks = fastcdc_gpu_clamp_pipeline_tasks(tasks);
}

static void fastcdc_gpu_load_threads_per_block_from_env(void) {
	const char *env = getenv("DESTOR_GPU_THREADS_PER_BLOCK");
	char *end = NULL;
	long parsed;

	if (!env || !*env) {
		return;
	}
	parsed = strtol(env, &end, 10);
	if (end && *end == '\0' && parsed >= INT_MIN && parsed <= INT_MAX) {
		fastcdc_gpu_set_threads_per_block((int)parsed);
	}
}

static void fastcdc_gpu_load_pipeline_tasks_from_env(void) {
	const char *env = getenv("DESTOR_GPU_PIPELINE_TASKS");
	char *end = NULL;
	long parsed;

	if (!env || !*env) {
		return;
	}
	parsed = strtol(env, &end, 10);
	if (end && *end == '\0' && parsed >= INT_MIN && parsed <= INT_MAX) {
		fastcdc_gpu_set_pipeline_tasks((int)parsed);
	}
}

static int cuda_driver_init_context();

static int fastcdc_gpu_ensure_batch_capacity(size_t total_bytes,
		int task_count,
		int boundary_stride);

static void fastcdc_gpu_destroy_batch_events() {
	int i;

	if (!g_cuda.cuEventDestroy) {
		return;
	}
	for (i = 0; i < 2; i++) {
		if (g_cuda.batch_start_event[i]) {
			g_cuda.cuEventDestroy(g_cuda.batch_start_event[i]);
			g_cuda.batch_start_event[i] = NULL;
		}
		if (g_cuda.batch_stop_event[i]) {
			g_cuda.cuEventDestroy(g_cuda.batch_stop_event[i]);
			g_cuda.batch_stop_event[i] = NULL;
		}
	}
}

static void fastcdc_gpu_init_batch_events() {
	CUresult rc;
	int i;

	if (!g_cuda.cuEventCreate || !g_cuda.cuEventRecord || !g_cuda.cuEventElapsedTime) {
		return;
	}
	for (i = 0; i < 2; i++) {
		rc = g_cuda.cuEventCreate(&g_cuda.batch_start_event[i], 0);
		if (rc != CUDA_SUCCESS) {
			fastcdc_gpu_destroy_batch_events();
			return;
		}
		rc = g_cuda.cuEventCreate(&g_cuda.batch_stop_event[i], 0);
		if (rc != CUDA_SUCCESS) {
			fastcdc_gpu_destroy_batch_events();
			return;
		}
	}
}
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

static int gear_gpu_compute_mask(uint64_t *mask, int *expect_chunk_size) {
	int index = fastcdc_gpu_floor_log2((unsigned int)destor.chunk_avg_size);

	if (index <= 6 || index >= 17) {
		return -1;
	}
	if (expect_chunk_size) {
		*expect_chunk_size = 1 << index;
	}
	if (mask) {
		*mask = (uint64_t)g_condition_mask[index];
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

static void fastcdc_gpu_reset_state() {
	memset(&g_cuda, 0, sizeof(g_cuda));
	g_cuda.pipeline_tasks = 0;
	g_cuda.threads_per_block = 0;
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
	fastcdc_gpu_destroy_batch_events();
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
	if (g_cuda.batch_boundaries_host_alt) {
		if (g_cuda.cuMemFreeHost) {
			g_cuda.cuMemFreeHost(g_cuda.batch_boundaries_host_alt);
		} else {
			free(g_cuda.batch_boundaries_host_alt);
		}
	}
	if (g_cuda.batch_boundaries_host) {
		if (g_cuda.cuMemFreeHost) {
			g_cuda.cuMemFreeHost(g_cuda.batch_boundaries_host);
		} else {
			free(g_cuda.batch_boundaries_host);
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
	if (g_cuda.batch_target_lengths_host_alt) {
		if (g_cuda.cuMemFreeHost) {
			g_cuda.cuMemFreeHost(g_cuda.batch_target_lengths_host_alt);
		} else {
			free(g_cuda.batch_target_lengths_host_alt);
		}
	}
	if (g_cuda.batch_target_lengths_host) {
		if (g_cuda.cuMemFreeHost) {
			g_cuda.cuMemFreeHost(g_cuda.batch_target_lengths_host);
		} else {
			free(g_cuda.batch_target_lengths_host);
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
	if (g_cuda.batch_input_host_alt) {
		if (g_cuda.cuMemFreeHost) {
			g_cuda.cuMemFreeHost(g_cuda.batch_input_host_alt);
		} else {
			free(g_cuda.batch_input_host_alt);
		}
	}
	if (g_cuda.batch_results_device && g_cuda.cuMemFree) {
		g_cuda.cuMemFree(g_cuda.batch_results_device);
	}
	if (g_cuda.batch_results_device_alt && g_cuda.cuMemFree) {
		g_cuda.cuMemFree(g_cuda.batch_results_device_alt);
	}
	if (g_cuda.batch_boundaries_device && g_cuda.cuMemFree) {
		g_cuda.cuMemFree(g_cuda.batch_boundaries_device);
	}
	if (g_cuda.batch_boundaries_device_alt && g_cuda.cuMemFree) {
		g_cuda.cuMemFree(g_cuda.batch_boundaries_device_alt);
	}
	if (g_cuda.batch_lengths_device && g_cuda.cuMemFree) {
		g_cuda.cuMemFree(g_cuda.batch_lengths_device);
	}
	if (g_cuda.batch_lengths_device_alt && g_cuda.cuMemFree) {
		g_cuda.cuMemFree(g_cuda.batch_lengths_device_alt);
	}
	if (g_cuda.batch_target_lengths_device && g_cuda.cuMemFree) {
		g_cuda.cuMemFree(g_cuda.batch_target_lengths_device);
	}
	if (g_cuda.batch_target_lengths_device_alt && g_cuda.cuMemFree) {
		g_cuda.cuMemFree(g_cuda.batch_target_lengths_device_alt);
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

static int fastcdc_gpu_ensure_batch_capacity(size_t total_bytes,
		int task_count,
		int boundary_stride) {
	CUdeviceptr new_device_ptr = 0;
	CUdeviceptr new_device_ptr_alt = 0;
	unsigned char *new_input_host = NULL;
	unsigned char *new_input_host_alt = NULL;
	int *new_offsets_host = NULL;
	int *new_offsets_host_alt = NULL;
	int *new_lengths_host = NULL;
	int *new_lengths_host_alt = NULL;
	int *new_target_lengths_host = NULL;
	int *new_target_lengths_host_alt = NULL;
	struct fastcdc_gpu_segment_result *new_results_host = NULL;
	struct fastcdc_gpu_segment_result *new_results_host_alt = NULL;
	int *new_boundaries_host = NULL;
	int *new_boundaries_host_alt = NULL;
	size_t boundaries_bytes;

	if (task_count <= 0) {
		return -1;
	}
	if (boundary_stride <= 0) {
		boundary_stride = 1;
	}
	boundaries_bytes = sizeof(int) * (size_t)task_count * (size_t)boundary_stride;
	if (fastcdc_gpu_activate_context() != 0) {
		return -1;
	}
	if (total_bytes > g_cuda.batch_input_capacity) {
		if (fastcdc_gpu_alloc_host_buffer((void **)&new_input_host, total_bytes) != 0) {
			return -1;
		}
		if (fastcdc_gpu_alloc_host_buffer((void **)&new_input_host_alt, total_bytes) != 0) {
			fastcdc_gpu_free_host_buffer(new_input_host);
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
				fastcdc_gpu_free_host_buffer(new_input_host_alt);
				fastcdc_gpu_free_host_buffer(new_input_host);
				return -1;
			}
			if (g_cuda.cuMemAlloc(&new_device_ptr_alt, total_bytes) != CUDA_SUCCESS) {
				g_cuda.cuMemFree(new_device_ptr);
				fastcdc_gpu_free_host_buffer(new_input_host_alt);
				fastcdc_gpu_free_host_buffer(new_input_host);
				return -1;
			}
			g_cuda.batch_input_device = new_device_ptr;
			g_cuda.batch_input_device_alt = new_device_ptr_alt;
		}
		fastcdc_gpu_free_host_buffer(g_cuda.batch_input_host_alt);
		fastcdc_gpu_free_host_buffer(g_cuda.batch_input_host);
		g_cuda.batch_input_host = new_input_host;
		g_cuda.batch_input_host_alt = new_input_host_alt;
		g_cuda.batch_input_capacity = total_bytes;
	}
	if (task_count > g_cuda.batch_task_capacity || boundary_stride != g_cuda.batch_boundary_stride) {
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
		if (fastcdc_gpu_alloc_host_buffer((void **)&new_target_lengths_host, sizeof(int) * (size_t)task_count) != 0) {
			fastcdc_gpu_free_host_buffer(new_lengths_host_alt);
			fastcdc_gpu_free_host_buffer(new_lengths_host);
			fastcdc_gpu_free_host_buffer(new_offsets_host_alt);
			fastcdc_gpu_free_host_buffer(new_offsets_host);
			return -1;
		}
		if (fastcdc_gpu_alloc_host_buffer((void **)&new_target_lengths_host_alt, sizeof(int) * (size_t)task_count) != 0) {
			fastcdc_gpu_free_host_buffer(new_target_lengths_host);
			fastcdc_gpu_free_host_buffer(new_lengths_host_alt);
			fastcdc_gpu_free_host_buffer(new_lengths_host);
			fastcdc_gpu_free_host_buffer(new_offsets_host_alt);
			fastcdc_gpu_free_host_buffer(new_offsets_host);
			return -1;
		}
		if (fastcdc_gpu_alloc_host_buffer((void **)&new_results_host,
				sizeof(struct fastcdc_gpu_segment_result) * (size_t)task_count) != 0) {
			fastcdc_gpu_free_host_buffer(new_target_lengths_host_alt);
			fastcdc_gpu_free_host_buffer(new_target_lengths_host);
			fastcdc_gpu_free_host_buffer(new_lengths_host_alt);
			fastcdc_gpu_free_host_buffer(new_lengths_host);
			fastcdc_gpu_free_host_buffer(new_offsets_host_alt);
			fastcdc_gpu_free_host_buffer(new_offsets_host);
			return -1;
		}
		if (fastcdc_gpu_alloc_host_buffer((void **)&new_results_host_alt,
				sizeof(struct fastcdc_gpu_segment_result) * (size_t)task_count) != 0) {
			fastcdc_gpu_free_host_buffer(new_results_host);
			fastcdc_gpu_free_host_buffer(new_target_lengths_host_alt);
			fastcdc_gpu_free_host_buffer(new_target_lengths_host);
			fastcdc_gpu_free_host_buffer(new_lengths_host_alt);
			fastcdc_gpu_free_host_buffer(new_lengths_host);
			fastcdc_gpu_free_host_buffer(new_offsets_host_alt);
			fastcdc_gpu_free_host_buffer(new_offsets_host);
			return -1;
		}
		if (fastcdc_gpu_alloc_host_buffer((void **)&new_boundaries_host, boundaries_bytes) != 0) {
			fastcdc_gpu_free_host_buffer(new_results_host_alt);
			fastcdc_gpu_free_host_buffer(new_results_host);
			fastcdc_gpu_free_host_buffer(new_target_lengths_host_alt);
			fastcdc_gpu_free_host_buffer(new_target_lengths_host);
			fastcdc_gpu_free_host_buffer(new_lengths_host_alt);
			fastcdc_gpu_free_host_buffer(new_lengths_host);
			fastcdc_gpu_free_host_buffer(new_offsets_host_alt);
			fastcdc_gpu_free_host_buffer(new_offsets_host);
			return -1;
		}
		if (fastcdc_gpu_alloc_host_buffer((void **)&new_boundaries_host_alt, boundaries_bytes) != 0) {
			fastcdc_gpu_free_host_buffer(new_boundaries_host);
			fastcdc_gpu_free_host_buffer(new_results_host_alt);
			fastcdc_gpu_free_host_buffer(new_results_host);
			fastcdc_gpu_free_host_buffer(new_target_lengths_host_alt);
			fastcdc_gpu_free_host_buffer(new_target_lengths_host);
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
		fastcdc_gpu_free_host_buffer(g_cuda.batch_target_lengths_host_alt);
		fastcdc_gpu_free_host_buffer(g_cuda.batch_target_lengths_host);
		fastcdc_gpu_free_host_buffer(g_cuda.batch_results_host_alt);
		fastcdc_gpu_free_host_buffer(g_cuda.batch_results_host);
		fastcdc_gpu_free_host_buffer(g_cuda.batch_boundaries_host_alt);
		fastcdc_gpu_free_host_buffer(g_cuda.batch_boundaries_host);
		g_cuda.batch_offsets_host = new_offsets_host;
		g_cuda.batch_offsets_host_alt = new_offsets_host_alt;
		g_cuda.batch_lengths_host = new_lengths_host;
		g_cuda.batch_lengths_host_alt = new_lengths_host_alt;
		g_cuda.batch_target_lengths_host = new_target_lengths_host;
		g_cuda.batch_target_lengths_host_alt = new_target_lengths_host_alt;
		g_cuda.batch_results_host = new_results_host;
		g_cuda.batch_results_host_alt = new_results_host_alt;
		g_cuda.batch_boundaries_host = new_boundaries_host;
		g_cuda.batch_boundaries_host_alt = new_boundaries_host_alt;

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
		if (g_cuda.batch_target_lengths_device_alt) {
			g_cuda.cuMemFree(g_cuda.batch_target_lengths_device_alt);
			g_cuda.batch_target_lengths_device_alt = 0;
		}
		if (g_cuda.batch_target_lengths_device) {
			g_cuda.cuMemFree(g_cuda.batch_target_lengths_device);
			g_cuda.batch_target_lengths_device = 0;
		}
		if (g_cuda.batch_results_device_alt) {
			g_cuda.cuMemFree(g_cuda.batch_results_device_alt);
			g_cuda.batch_results_device_alt = 0;
		}
		if (g_cuda.batch_results_device) {
			g_cuda.cuMemFree(g_cuda.batch_results_device);
			g_cuda.batch_results_device = 0;
		}
		if (g_cuda.batch_boundaries_device_alt) {
			g_cuda.cuMemFree(g_cuda.batch_boundaries_device_alt);
			g_cuda.batch_boundaries_device_alt = 0;
		}
		if (g_cuda.batch_boundaries_device) {
			g_cuda.cuMemFree(g_cuda.batch_boundaries_device);
			g_cuda.batch_boundaries_device = 0;
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
		if (g_cuda.cuMemAlloc(&g_cuda.batch_target_lengths_device,
				sizeof(int) * (size_t)task_count) != CUDA_SUCCESS) {
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
		if (g_cuda.cuMemAlloc(&g_cuda.batch_target_lengths_device_alt,
				sizeof(int) * (size_t)task_count) != CUDA_SUCCESS) {
			g_cuda.cuMemFree(g_cuda.batch_target_lengths_device);
			g_cuda.batch_target_lengths_device = 0;
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
		if (g_cuda.cuMemAlloc(&g_cuda.batch_results_device,
				sizeof(struct fastcdc_gpu_segment_result) * (size_t)task_count) != CUDA_SUCCESS) {
			g_cuda.cuMemFree(g_cuda.batch_target_lengths_device_alt);
			g_cuda.batch_target_lengths_device_alt = 0;
			g_cuda.cuMemFree(g_cuda.batch_target_lengths_device);
			g_cuda.batch_target_lengths_device = 0;
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
				sizeof(struct fastcdc_gpu_segment_result) * (size_t)task_count) != CUDA_SUCCESS) {
			g_cuda.cuMemFree(g_cuda.batch_results_device);
			g_cuda.batch_results_device = 0;
			g_cuda.cuMemFree(g_cuda.batch_target_lengths_device_alt);
			g_cuda.batch_target_lengths_device_alt = 0;
			g_cuda.cuMemFree(g_cuda.batch_target_lengths_device);
			g_cuda.batch_target_lengths_device = 0;
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
		if (g_cuda.cuMemAlloc(&g_cuda.batch_boundaries_device, boundaries_bytes) != CUDA_SUCCESS) {
			g_cuda.cuMemFree(g_cuda.batch_results_device_alt);
			g_cuda.batch_results_device_alt = 0;
			g_cuda.cuMemFree(g_cuda.batch_results_device);
			g_cuda.batch_results_device = 0;
			g_cuda.cuMemFree(g_cuda.batch_target_lengths_device_alt);
			g_cuda.batch_target_lengths_device_alt = 0;
			g_cuda.cuMemFree(g_cuda.batch_target_lengths_device);
			g_cuda.batch_target_lengths_device = 0;
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
		if (g_cuda.cuMemAlloc(&g_cuda.batch_boundaries_device_alt, boundaries_bytes) != CUDA_SUCCESS) {
			g_cuda.cuMemFree(g_cuda.batch_boundaries_device);
			g_cuda.batch_boundaries_device = 0;
			g_cuda.cuMemFree(g_cuda.batch_results_device_alt);
			g_cuda.batch_results_device_alt = 0;
			g_cuda.cuMemFree(g_cuda.batch_results_device);
			g_cuda.batch_results_device = 0;
			g_cuda.cuMemFree(g_cuda.batch_target_lengths_device_alt);
			g_cuda.batch_target_lengths_device_alt = 0;
			g_cuda.cuMemFree(g_cuda.batch_target_lengths_device);
			g_cuda.batch_target_lengths_device = 0;
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
		g_cuda.batch_boundary_stride = boundary_stride;
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

static int gear_gpu_prepare_kernel() {
	CUresult rc;

	if (g_cuda.gear_kernel_ready) {
		return 0;
	}
	if (fastcdc_gpu_prepare_kernel() != 0) {
		return -1;
	}
	rc = g_cuda.cuModuleGetFunction(&g_cuda.gear_kernel,
			g_cuda.module,
			GEAR_GPU_KERNEL_SYMBOL);
	if (rc != CUDA_SUCCESS || !g_cuda.gear_kernel) {
		WARNING("Gear GPU: cuModuleGetFunction(%s) failed: %s",
				GEAR_GPU_KERNEL_SYMBOL,
				fastcdc_cuda_error_string(rc));
		return -1;
	}
	g_cuda.gear_kernel_ready = 1;
	return 0;
}

static int cuda_driver_init_context() {
	if (g_cuda.initialized) {
		return 0;
	}

	fastcdc_gpu_reset_state();
	fastcdc_gpu_load_pipeline_tasks_from_env();
	fastcdc_gpu_load_threads_per_block_from_env();
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
	fastcdc_gpu_init_batch_events();

	g_cuda.initialized = 1;
	NOTICE("Chunk GPU: CUDA context initialized on device %d", destor.chunk_gpu_device_id);
	return 0;
}

int fastcdc_gpu_init() {
	if (fastcdc_gpu_naive_mode_enabled()) {
		return fastcdc_gpu_naive_init();
	}
	return fastcdc_gpu_prepare_kernel();
}

int fastcdc_gpu_is_ready() {
	if (fastcdc_gpu_naive_mode_enabled()) {
		return fastcdc_gpu_naive_is_ready();
	}
	return g_cuda.kernel_ready;
}

void fastcdc_gpu_close() {
	if (fastcdc_gpu_naive_mode_enabled()) {
		fastcdc_gpu_naive_close();
		return;
	}
	if (!g_cuda.initialized && !g_cuda.handle) {
		return;
	}
	fastcdc_gpu_release_driver();
}

typedef int (*fastcdc_gpu_cpu_chunk_fn)(unsigned char *buffer, int size);

static void fastcdc_gpu_chunk_segments_cpu_fallback(unsigned char **buffers,
		const int *sizes,
		int task_count,
		int boundary_stride,
		int *boundary_counts,
		int *chunk_sizes,
		fastcdc_gpu_cpu_chunk_fn chunk_fn) {
	int i;
	int payload_bytes = fastcdc_gpu_segment_bytes();

	for (i = 0; i < task_count; i++) {
		int segment_size = sizes[i] < fastcdc_gpu_segment_window_bytes()
				? sizes[i]
				: fastcdc_gpu_segment_window_bytes();
		int target_size = sizes[i] < payload_bytes ? sizes[i] : payload_bytes;
		int offset = 0;
		int count = 0;

		if (segment_size < 0) {
			segment_size = 0;
		}
		if (target_size < 0) {
			target_size = 0;
		}
		while (offset < segment_size && count < boundary_stride) {
			int chunk_size = chunk_fn(buffers[i] + offset, segment_size - offset);

			if (chunk_size <= 0 || chunk_size > segment_size - offset) {
				chunk_size = segment_size - offset;
			}
			chunk_sizes[i * boundary_stride + count] = chunk_size;
			offset += chunk_size;
			count++;
			if (offset >= target_size) {
				break;
			}
		}
		boundary_counts[i] = count;
	}
}

static void fastcdc_gpu_segment_fallback_one(unsigned char *buffer,
		int size,
		int boundary_stride,
		int *boundary_count,
		int *chunk_sizes,
		fastcdc_gpu_cpu_chunk_fn chunk_fn) {
	unsigned char *buffers[1];
	int sizes[1];

	buffers[0] = buffer;
	sizes[0] = size;
	fastcdc_gpu_chunk_segments_cpu_fallback(buffers,
			sizes,
			1,
			boundary_stride,
			boundary_count,
			chunk_sizes,
			chunk_fn);
}

static int fastcdc_gpu_chunk_segments_batch_common(unsigned char **buffers,
		const int *sizes,
		int task_count,
		int boundary_stride,
		int *boundary_counts,
		int *chunk_sizes,
		enum gpu_batch_kernel_kind kernel_kind) {
	CUresult rc = CUDA_SUCCESS;
	CUstream streams[2];
	CUdeviceptr input_devices[2];
	CUdeviceptr offsets_devices[2];
	CUdeviceptr lengths_devices[2];
	CUdeviceptr target_lengths_devices[2];
	CUdeviceptr results_devices[2];
	CUdeviceptr boundaries_devices[2];
	unsigned char *input_host[2];
	int *offsets_host[2];
	int *lengths_host[2];
	int *target_lengths_host[2];
	struct fastcdc_gpu_segment_result *results_host[2];
	int *boundaries_host[2];
	int slot_start[2] = { 0, 0 };
	int slot_count[2] = { 0, 0 };
	int slot_busy[2] = { 0, 0 };
	uint64_t mask_s = 0;
	uint64_t mask_l = 0;
	uint64_t mask = 0;
	uint64_t jump_mask = 0;
	int jump_len = 0;
	int expect_size = 0;
	int warp_window = destor.chunk_warp_window;
	int threads_per_block = fastcdc_gpu_runtime_threads_per_block();
	int pipeline_tasks = fastcdc_gpu_runtime_pipeline_tasks(kernel_kind != GPU_BATCH_KERNEL_FASTCDC
			? FASTCDC_GPU_JC_PIPELINE_TASKS_DEFAULT
			: FASTCDC_GPU_FASTCDC_PIPELINE_TASKS_DEFAULT);
	int active_slots = 0;
	int next_task = 0;
	int segment_bytes = fastcdc_gpu_segment_window_bytes();
	int i;
	int ok = 1;
	size_t total_bytes = 0;
	float kernel_ms = 0.0f;
	CUfunction kernel;
	fastcdc_gpu_cpu_chunk_fn cpu_chunk_fn;

	switch (kernel_kind) {
	case GPU_BATCH_KERNEL_JC:
		kernel = g_cuda.jc_kernel;
		cpu_chunk_fn = gearjump_chunk_data;
		break;
	case GPU_BATCH_KERNEL_GEAR:
		kernel = g_cuda.gear_kernel;
		cpu_chunk_fn = gear_chunk_data;
		break;
	default:
		kernel = g_cuda.fastcdc_kernel;
		cpu_chunk_fn = fastcdc_chunk_data;
		break;
	}

	if (!buffers || !sizes || !boundary_counts || !chunk_sizes || task_count <= 0 || boundary_stride <= 0) {
		return -1;
	}

	if ((kernel_kind == GPU_BATCH_KERNEL_FASTCDC && !g_cuda.kernel_ready)
			|| (kernel_kind == GPU_BATCH_KERNEL_JC && !g_cuda.jc_kernel_ready)
			|| (kernel_kind == GPU_BATCH_KERNEL_GEAR && !g_cuda.gear_kernel_ready)) {
		fastcdc_gpu_chunk_segments_cpu_fallback(buffers,
				sizes,
				task_count,
				boundary_stride,
				boundary_counts,
				chunk_sizes,
				cpu_chunk_fn);
		return 0;
	}
	if (fastcdc_gpu_activate_context() != 0) {
		fastcdc_gpu_chunk_segments_cpu_fallback(buffers,
				sizes,
				task_count,
				boundary_stride,
				boundary_counts,
				chunk_sizes,
				cpu_chunk_fn);
		return 0;
	}
	if (kernel_kind == GPU_BATCH_KERNEL_JC
			&& (jc_gpu_compute_params(&mask, &jump_mask, &jump_len, &expect_size) != 0 || jump_len <= 0)) {
		fastcdc_gpu_chunk_segments_cpu_fallback(buffers,
				sizes,
				task_count,
				boundary_stride,
				boundary_counts,
				chunk_sizes,
				cpu_chunk_fn);
		return 0;
	}
	if (kernel_kind == GPU_BATCH_KERNEL_GEAR && gear_gpu_compute_mask(&mask, &expect_size) != 0) {
		fastcdc_gpu_chunk_segments_cpu_fallback(buffers,
				sizes,
				task_count,
				boundary_stride,
				boundary_counts,
				chunk_sizes,
				cpu_chunk_fn);
		return 0;
	}
	if (kernel_kind == GPU_BATCH_KERNEL_FASTCDC) {
		fastcdc_gpu_compute_masks(&mask_s, &mask_l, &expect_size);
	}

	for (i = 0; i < task_count; i++) {
		int copy_len = sizes[i] < segment_bytes ? sizes[i] : segment_bytes;

		if (copy_len < 0) {
			copy_len = 0;
		}
		total_bytes += (size_t)copy_len;
		boundary_counts[i] = 0;
	}
	if (fastcdc_gpu_ensure_batch_capacity(total_bytes, task_count, boundary_stride) != 0) {
		ok = 0;
		goto done;
	}

	if (total_bytes == 0) {
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
	target_lengths_devices[0] = g_cuda.batch_target_lengths_device;
	target_lengths_devices[1] = g_cuda.batch_target_lengths_device_alt;
	results_devices[0] = g_cuda.batch_results_device;
	results_devices[1] = g_cuda.batch_results_device_alt;
	boundaries_devices[0] = g_cuda.batch_boundaries_device;
	boundaries_devices[1] = g_cuda.batch_boundaries_device_alt;
	input_host[0] = g_cuda.batch_input_host;
	input_host[1] = g_cuda.batch_input_host_alt;
	offsets_host[0] = g_cuda.batch_offsets_host;
	offsets_host[1] = g_cuda.batch_offsets_host_alt;
	lengths_host[0] = g_cuda.batch_lengths_host;
	lengths_host[1] = g_cuda.batch_lengths_host_alt;
	target_lengths_host[0] = g_cuda.batch_target_lengths_host;
	target_lengths_host[1] = g_cuda.batch_target_lengths_host_alt;
	results_host[0] = g_cuda.batch_results_host;
	results_host[1] = g_cuda.batch_results_host_alt;
	boundaries_host[0] = g_cuda.batch_boundaries_host;
	boundaries_host[1] = g_cuda.batch_boundaries_host_alt;

	while (next_task < task_count || active_slots > 0) {
		while (next_task < task_count && active_slots < 2) {
			int slot = slot_busy[0] ? 1 : 0;
			int local_count = task_count - next_task;
			int local_blocks;
			size_t local_bytes = 0;
			size_t shared_bytes = fastcdc_gpu_kernel_shared_bytes(threads_per_block);

			if (local_count > pipeline_tasks) {
				local_count = pipeline_tasks;
			}
			for (i = 0; i < local_count; i++) {
				int idx = next_task + i;
				int target_len = sizes[idx] < fastcdc_gpu_segment_bytes() ? sizes[idx] : fastcdc_gpu_segment_bytes();
				int copy_len = sizes[idx] < segment_bytes ? sizes[idx] : segment_bytes;

				if (copy_len < 0) {
					copy_len = 0;
				}
				if (target_len < 0) {
					target_len = 0;
				}
				offsets_host[slot][i] = (int)local_bytes;
				lengths_host[slot][i] = copy_len;
				target_lengths_host[slot][i] = target_len;
				if (copy_len > 0) {
					memcpy(input_host[slot] + offsets_host[slot][i],
							buffers[idx],
							(size_t)copy_len);
				}
				local_bytes += (size_t)copy_len;
			}
			if (local_bytes > 0) {
				rc = g_cuda.cuMemcpyHtoDAsync(input_devices[slot],
						input_host[slot],
						local_bytes,
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
			rc = g_cuda.cuMemcpyHtoDAsync(target_lengths_devices[slot],
					target_lengths_host[slot],
					sizeof(int) * (size_t)local_count,
					streams[slot]);
			if (rc != CUDA_SUCCESS) {
				ok = 0;
				goto done;
			}
			if (g_cuda.batch_start_event[slot] && g_cuda.batch_stop_event[slot] && g_cuda.cuEventRecord
					&& g_cuda.cuEventElapsedTime) {
				rc = g_cuda.cuEventRecord(g_cuda.batch_start_event[slot], streams[slot]);
				if (rc != CUDA_SUCCESS) {
					ok = 0;
					goto done;
				}
			}

			local_blocks = local_count;
			if (kernel_kind == GPU_BATCH_KERNEL_JC) {
				void *kernel_params[16];

				kernel_params[0] = &input_devices[slot];
				kernel_params[1] = &offsets_devices[slot];
				kernel_params[2] = &lengths_devices[slot];
				kernel_params[3] = &target_lengths_devices[slot];
				kernel_params[4] = &local_count;
				kernel_params[5] = &g_cuda.gear_matrix_device;
				kernel_params[6] = &destor.chunk_min_size;
				kernel_params[7] = &destor.chunk_max_size;
				kernel_params[8] = &mask;
				kernel_params[9] = &jump_mask;
				kernel_params[10] = &jump_len;
				kernel_params[11] = &warp_window;
				kernel_params[12] = &boundary_stride;
				kernel_params[13] = &boundaries_devices[slot];
				kernel_params[14] = &results_devices[slot];
				kernel_params[15] = NULL;
				rc = g_cuda.cuLaunchKernel(kernel,
						(unsigned int)local_blocks,
						1,
						1,
						(unsigned int)threads_per_block,
						1,
						1,
						(unsigned int)shared_bytes,
						streams[slot],
						kernel_params,
						NULL);
			} else if (kernel_kind == GPU_BATCH_KERNEL_GEAR) {
				void *kernel_params[16];

				kernel_params[0] = &input_devices[slot];
				kernel_params[1] = &offsets_devices[slot];
				kernel_params[2] = &lengths_devices[slot];
				kernel_params[3] = &target_lengths_devices[slot];
				kernel_params[4] = &local_count;
				kernel_params[5] = &g_cuda.gear_matrix_device;
				kernel_params[6] = &destor.chunk_min_size;
				kernel_params[7] = &destor.chunk_max_size;
				kernel_params[8] = &mask;
				kernel_params[9] = &warp_window;
				kernel_params[10] = &boundary_stride;
				kernel_params[11] = &boundaries_devices[slot];
				kernel_params[12] = &results_devices[slot];
				kernel_params[13] = NULL;
				rc = g_cuda.cuLaunchKernel(kernel,
						(unsigned int)local_blocks,
						1,
						1,
						(unsigned int)threads_per_block,
						1,
						1,
						(unsigned int)shared_bytes,
						streams[slot],
						kernel_params,
						NULL);
			} else {
				void *kernel_params[16];

				kernel_params[0] = &input_devices[slot];
				kernel_params[1] = &offsets_devices[slot];
				kernel_params[2] = &lengths_devices[slot];
				kernel_params[3] = &target_lengths_devices[slot];
				kernel_params[4] = &local_count;
				kernel_params[5] = &g_cuda.gear_matrix_device;
				kernel_params[6] = &destor.chunk_min_size;
				kernel_params[7] = &destor.chunk_max_size;
				kernel_params[8] = &expect_size;
				kernel_params[9] = &mask_s;
				kernel_params[10] = &mask_l;
				kernel_params[11] = &warp_window;
				kernel_params[12] = &boundary_stride;
				kernel_params[13] = &boundaries_devices[slot];
				kernel_params[14] = &results_devices[slot];
				kernel_params[15] = NULL;
				rc = g_cuda.cuLaunchKernel(kernel,
						(unsigned int)local_blocks,
						1,
						1,
						(unsigned int)threads_per_block,
						1,
						1,
						(unsigned int)shared_bytes,
						streams[slot],
						kernel_params,
						NULL);
			}
			if (rc == CUDA_SUCCESS && g_cuda.batch_start_event[slot] && g_cuda.batch_stop_event[slot]) {
				rc = g_cuda.cuEventRecord(g_cuda.batch_stop_event[slot], streams[slot]);
			}
			if (rc == CUDA_SUCCESS) {
				rc = g_cuda.cuMemcpyDtoHAsync(results_host[slot],
						results_devices[slot],
						sizeof(struct fastcdc_gpu_segment_result) * (size_t)local_count,
						streams[slot]);
			}
			if (rc == CUDA_SUCCESS) {
				rc = g_cuda.cuMemcpyDtoHAsync(boundaries_host[slot],
						boundaries_devices[slot],
						sizeof(int) * (size_t)local_count * (size_t)boundary_stride,
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
			if (g_cuda.batch_start_event[slot] && g_cuda.batch_stop_event[slot] && g_cuda.cuEventElapsedTime) {
				rc = g_cuda.cuEventElapsedTime(&kernel_ms,
						g_cuda.batch_start_event[slot],
						g_cuda.batch_stop_event[slot]);
				if (rc != CUDA_SUCCESS) {
					ok = 0;
					goto done;
				}
				g_cuda.batch_compute_ms_accum += (double)kernel_ms;
			}
			for (i = 0; i < slot_count[slot]; i++) {
				int idx = slot_start[slot] + i;
				int copy_len = lengths_host[slot][i];
				int base = idx * boundary_stride;
				int local_base = i * boundary_stride;
				int prev = 0;
				int valid = 1;
				int j;

				if (results_host[slot][i].boundary_count <= 0
						|| results_host[slot][i].boundary_count > boundary_stride
						|| results_host[slot][i].consumed_bytes < target_lengths_host[slot][i]
						|| results_host[slot][i].consumed_bytes > copy_len) {
					valid = 0;
				}
				if (valid) {
					for (j = 0; j < results_host[slot][i].boundary_count; j++) {
						int boundary = boundaries_host[slot][local_base + j];

						if (boundary <= prev || boundary > copy_len) {
							valid = 0;
							break;
						}
						chunk_sizes[base + j] = boundary - prev;
						prev = boundary;
					}
				}
				if (!valid) {
					fastcdc_gpu_segment_fallback_one(buffers[idx],
							sizes[idx] < segment_bytes ? sizes[idx] : segment_bytes,
							boundary_stride,
							boundary_counts + idx,
							chunk_sizes + base,
							cpu_chunk_fn);
				} else {
					boundary_counts[idx] = results_host[slot][i].boundary_count;
				}
			}
			slot_busy[slot] = 0;
			active_slots--;
		}
	}

done:
	if (!ok) {
		const char *kernel_name = "FastCDC";
		if (kernel_kind == GPU_BATCH_KERNEL_JC) {
			kernel_name = "JC";
		} else if (kernel_kind == GPU_BATCH_KERNEL_GEAR) {
			kernel_name = "Gear";
		}
		WARNING("%s GPU segment batch: launch/copy failed, falling back to CPU: %s",
				kernel_name,
				fastcdc_cuda_error_string(rc));
		fastcdc_gpu_chunk_segments_cpu_fallback(buffers,
				sizes,
				task_count,
				boundary_stride,
				boundary_counts,
				chunk_sizes,
				cpu_chunk_fn);
	}
	return 0;
}

int fastcdc_gpu_chunk_segments_batch(unsigned char **buffers,
		const int *sizes,
		int task_count,
		int boundary_stride,
		int *boundary_counts,
		int *chunk_sizes) {
	return fastcdc_gpu_chunk_segments_batch_common(buffers,
			sizes,
			task_count,
			boundary_stride,
			boundary_counts,
			chunk_sizes,
			GPU_BATCH_KERNEL_FASTCDC);
}

int fastcdc_gpu_chunk_batch(unsigned char **buffers,
		const int *sizes,
		int task_count,
		int *chunk_sizes) {
	int boundary_counts_local[task_count];
	return fastcdc_gpu_chunk_segments_batch(buffers,
			sizes,
			task_count,
			1,
			boundary_counts_local,
			chunk_sizes);
}

int fastcdc_gpu_chunk_data(unsigned char *p, int n) {
	if (fastcdc_gpu_naive_mode_enabled()) {
		return fastcdc_gpu_naive_chunk_data(p, n);
	}
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
	if (fastcdc_gpu_naive_mode_enabled()) {
		return fastcdc_gpu_naive_init();
	}
	return jc_gpu_prepare_kernel();
}

void jc_gpu_close() {
	fastcdc_gpu_close();
}

int jc_gpu_chunk_segments_batch(unsigned char **buffers,
		const int *sizes,
		int task_count,
		int boundary_stride,
		int *boundary_counts,
		int *chunk_sizes) {
	return fastcdc_gpu_chunk_segments_batch_common(buffers,
			sizes,
			task_count,
			boundary_stride,
			boundary_counts,
			chunk_sizes,
			GPU_BATCH_KERNEL_JC);
}

int jc_gpu_chunk_batch(unsigned char **buffers,
		const int *sizes,
		int task_count,
		int *chunk_sizes) {
	int boundary_counts_local[task_count];
	return jc_gpu_chunk_segments_batch(buffers,
			sizes,
			task_count,
			1,
			boundary_counts_local,
			chunk_sizes);
}

int jc_gpu_chunk_data(unsigned char *p, int n) {
	if (fastcdc_gpu_naive_mode_enabled()) {
		return jc_gpu_naive_chunk_data(p, n);
	}
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

int gear_gpu_init() {
	if (fastcdc_gpu_naive_mode_enabled()) {
		return fastcdc_gpu_naive_init();
	}
	return gear_gpu_prepare_kernel();
}

void gear_gpu_close() {
	fastcdc_gpu_close();
}

int gear_gpu_chunk_segments_batch(unsigned char **buffers,
		const int *sizes,
		int task_count,
		int boundary_stride,
		int *boundary_counts,
		int *chunk_sizes) {
	return fastcdc_gpu_chunk_segments_batch_common(buffers,
			sizes,
			task_count,
			boundary_stride,
			boundary_counts,
			chunk_sizes,
			GPU_BATCH_KERNEL_GEAR);
}

int gear_gpu_chunk_batch(unsigned char **buffers,
		const int *sizes,
		int task_count,
		int *chunk_sizes) {
	int boundary_counts_local[task_count];
	return gear_gpu_chunk_segments_batch(buffers,
			sizes,
			task_count,
			1,
			boundary_counts_local,
			chunk_sizes);
}

int gear_gpu_chunk_data(unsigned char *p, int n) {
	if (fastcdc_gpu_naive_mode_enabled()) {
		return gear_gpu_naive_chunk_data(p, n);
	}
	unsigned char *buffers[1];
	int sizes[1];
	int chunk_sizes[1];

	buffers[0] = p;
	sizes[0] = n;
	chunk_sizes[0] = 0;
	if (gear_gpu_chunk_batch(buffers, sizes, 1, chunk_sizes) != 0) {
		return gear_chunk_data(p, n);
	}
	return chunk_sizes[0];
}
