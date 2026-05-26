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
typedef CUresult (*cuMemcpyHtoD_t)(CUdeviceptr dstDevice, const void *srcHost, size_t ByteCount);
typedef CUresult (*cuMemcpyDtoH_t)(void *dstHost, CUdeviceptr srcDevice, size_t ByteCount);
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
	CUdeviceptr batch_offsets_device;
	CUdeviceptr batch_lengths_device;
	CUdeviceptr batch_results_device;
	size_t batch_input_capacity;
	int batch_task_capacity;
	unsigned char *batch_input_host;
	int *batch_offsets_host;
	int *batch_lengths_host;
	struct fastcdc_gpu_kernel_result *batch_results_host;
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
	cuMemcpyHtoD_t cuMemcpyHtoD;
	cuMemcpyDtoH_t cuMemcpyDtoH;
	cuLaunchKernel_t cuLaunchKernel;
	cuCtxSynchronize_t cuCtxSynchronize;
};

static struct cuda_driver_state g_cuda;
static struct chunk_experiment_stats g_chunk_experiment_stats;
static uint64_t g_chunk_experiment_current_checks;

static int cuda_driver_init_context();

static int fastcdc_gpu_ensure_batch_capacity(size_t total_bytes, int task_count);

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
	free(g_cuda.batch_results_host);
	free(g_cuda.batch_lengths_host);
	free(g_cuda.batch_offsets_host);
	free(g_cuda.batch_input_host);
	if (g_cuda.batch_results_device && g_cuda.cuMemFree) {
		g_cuda.cuMemFree(g_cuda.batch_results_device);
	}
	if (g_cuda.batch_lengths_device && g_cuda.cuMemFree) {
		g_cuda.cuMemFree(g_cuda.batch_lengths_device);
	}
	if (g_cuda.batch_offsets_device && g_cuda.cuMemFree) {
		g_cuda.cuMemFree(g_cuda.batch_offsets_device);
	}
	if (g_cuda.batch_input_device && g_cuda.cuMemFree) {
		g_cuda.cuMemFree(g_cuda.batch_input_device);
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

static int fastcdc_gpu_ensure_batch_capacity(size_t total_bytes, int task_count) {
	CUdeviceptr new_device_ptr = 0;
	void *new_host_ptr;

	if (task_count <= 0) {
		return -1;
	}
	if (fastcdc_gpu_activate_context() != 0) {
		return -1;
	}
	if (total_bytes > g_cuda.batch_input_capacity) {
		new_host_ptr = realloc(g_cuda.batch_input_host, total_bytes);
		if (!new_host_ptr) {
			return -1;
		}
		g_cuda.batch_input_host = (unsigned char *)new_host_ptr;
		if (g_cuda.batch_input_device) {
			g_cuda.cuMemFree(g_cuda.batch_input_device);
			g_cuda.batch_input_device = 0;
		}
		if (total_bytes > 0) {
			if (g_cuda.cuMemAlloc(&new_device_ptr, total_bytes) != CUDA_SUCCESS) {
				g_cuda.batch_input_capacity = 0;
				return -1;
			}
			g_cuda.batch_input_device = new_device_ptr;
		}
		g_cuda.batch_input_capacity = total_bytes;
	}
	if (task_count > g_cuda.batch_task_capacity) {
		new_host_ptr = realloc(g_cuda.batch_offsets_host, sizeof(int) * (size_t)task_count);
		if (!new_host_ptr) {
			return -1;
		}
		g_cuda.batch_offsets_host = (int *)new_host_ptr;

		new_host_ptr = realloc(g_cuda.batch_lengths_host, sizeof(int) * (size_t)task_count);
		if (!new_host_ptr) {
			return -1;
		}
		g_cuda.batch_lengths_host = (int *)new_host_ptr;

		new_host_ptr = realloc(g_cuda.batch_results_host,
				sizeof(struct fastcdc_gpu_kernel_result) * (size_t)task_count);
		if (!new_host_ptr) {
			return -1;
		}
		g_cuda.batch_results_host = (struct fastcdc_gpu_kernel_result *)new_host_ptr;

		if (g_cuda.batch_offsets_device) {
			g_cuda.cuMemFree(g_cuda.batch_offsets_device);
			g_cuda.batch_offsets_device = 0;
		}
		if (g_cuda.batch_lengths_device) {
			g_cuda.cuMemFree(g_cuda.batch_lengths_device);
			g_cuda.batch_lengths_device = 0;
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
		if (g_cuda.cuMemAlloc(&g_cuda.batch_lengths_device,
				sizeof(int) * (size_t)task_count) != CUDA_SUCCESS) {
			g_cuda.cuMemFree(g_cuda.batch_offsets_device);
			g_cuda.batch_offsets_device = 0;
			g_cuda.batch_task_capacity = 0;
			return -1;
		}
		if (g_cuda.cuMemAlloc(&g_cuda.batch_results_device,
				sizeof(struct fastcdc_gpu_kernel_result) * (size_t)task_count) != CUDA_SUCCESS) {
			g_cuda.cuMemFree(g_cuda.batch_lengths_device);
			g_cuda.batch_lengths_device = 0;
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
	LOAD_CUDA_SYMBOL_ANY(cuMemcpyHtoD, cuMemcpyHtoD_t, "cuMemcpyHtoD_v2", "cuMemcpyHtoD");
	LOAD_CUDA_SYMBOL_ANY(cuMemcpyDtoH, cuMemcpyDtoH_t, "cuMemcpyDtoH_v2", "cuMemcpyDtoH");
	LOAD_CUDA_SYMBOL(cuLaunchKernel);
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
	uint64_t mask_s = 0;
	uint64_t mask_l = 0;
	int expect_size = 0;
	int warp_window = destor.chunk_warp_window;
	int threads_per_block = 128;
	int blocks;
	int i;
	int ok = 1;
	size_t total_bytes = 0;

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

	for (i = 0; i < task_count; i++) {
		int copy_len = sizes[i] < destor.chunk_max_size ? sizes[i] : destor.chunk_max_size;

		if (copy_len < 0) {
			copy_len = 0;
		}
		g_cuda.batch_offsets_host[i] = (int)((i == 0) ? 0 : (g_cuda.batch_offsets_host[i - 1] + g_cuda.batch_lengths_host[i - 1]));
		g_cuda.batch_lengths_host[i] = copy_len;
		if (copy_len > 0) {
			memcpy(g_cuda.batch_input_host + g_cuda.batch_offsets_host[i], buffers[i], (size_t)copy_len);
		}
	}

	fastcdc_gpu_compute_masks(&mask_s, &mask_l, &expect_size);
	blocks = (task_count + threads_per_block - 1) / threads_per_block;

	rc = g_cuda.cuMemcpyHtoD(g_cuda.batch_input_device, g_cuda.batch_input_host, total_bytes);
	if (rc != CUDA_SUCCESS) {
		ok = 0;
		goto done;
	}
	rc = g_cuda.cuMemcpyHtoD(g_cuda.batch_offsets_device,
			g_cuda.batch_offsets_host,
			sizeof(int) * (size_t)task_count);
	if (rc != CUDA_SUCCESS) {
		ok = 0;
		goto done;
	}
	rc = g_cuda.cuMemcpyHtoD(g_cuda.batch_lengths_device,
			g_cuda.batch_lengths_host,
			sizeof(int) * (size_t)task_count);
	if (rc != CUDA_SUCCESS) {
		ok = 0;
		goto done;
	}

	{
		void *kernel_params[13];
		kernel_params[0] = &g_cuda.batch_input_device;
		kernel_params[1] = &g_cuda.batch_offsets_device;
		kernel_params[2] = &g_cuda.batch_lengths_device;
		kernel_params[3] = &task_count;
		kernel_params[4] = &g_cuda.gear_matrix_device;
		kernel_params[5] = &destor.chunk_min_size;
		kernel_params[6] = &destor.chunk_max_size;
		kernel_params[7] = &expect_size;
		kernel_params[8] = &mask_s;
		kernel_params[9] = &mask_l;
		kernel_params[10] = &warp_window;
		kernel_params[11] = &g_cuda.batch_results_device;
		kernel_params[12] = NULL;

		rc = g_cuda.cuLaunchKernel(g_cuda.fastcdc_kernel,
				(unsigned int)blocks,
				1,
				1,
				(unsigned int)threads_per_block,
				1,
				1,
				0,
				NULL,
				kernel_params,
				NULL);
	}
	if (rc == CUDA_SUCCESS) {
		rc = g_cuda.cuCtxSynchronize();
	}
	if (rc == CUDA_SUCCESS) {
		rc = g_cuda.cuMemcpyDtoH(g_cuda.batch_results_host,
				g_cuda.batch_results_device,
				sizeof(struct fastcdc_gpu_kernel_result) * (size_t)task_count);
	}
	if (rc != CUDA_SUCCESS) {
		ok = 0;
		goto done;
	}

	for (i = 0; i < task_count; i++) {
		if (g_cuda.batch_results_host[i].chunk_size <= 0 || g_cuda.batch_results_host[i].chunk_size > sizes[i]) {
			chunk_sizes[i] = fastcdc_chunk_data(buffers[i], sizes[i]);
		} else {
			chunk_sizes[i] = g_cuda.batch_results_host[i].chunk_size;
		}
		fastcdc_gpu_note_kernel_result(g_cuda.batch_results_host + i);
	}

done:
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
	uint64_t mask = 0;
	uint64_t jump_mask = 0;
	int jump_len = 0;
	int expect_size = 0;
	int warp_window = destor.chunk_warp_window;
	int threads_per_block = 128;
	int blocks;
	int i;
	int ok = 1;
	size_t total_bytes = 0;

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

	for (i = 0; i < task_count; i++) {
		int copy_len = sizes[i] < destor.chunk_max_size ? sizes[i] : destor.chunk_max_size;

		if (copy_len < 0) {
			copy_len = 0;
		}
		g_cuda.batch_offsets_host[i] = (int)((i == 0) ? 0 : (g_cuda.batch_offsets_host[i - 1] + g_cuda.batch_lengths_host[i - 1]));
		g_cuda.batch_lengths_host[i] = copy_len;
		if (copy_len > 0) {
			memcpy(g_cuda.batch_input_host + g_cuda.batch_offsets_host[i], buffers[i], (size_t)copy_len);
		}
	}

	blocks = (task_count + threads_per_block - 1) / threads_per_block;

	rc = g_cuda.cuMemcpyHtoD(g_cuda.batch_input_device, g_cuda.batch_input_host, total_bytes);
	if (rc != CUDA_SUCCESS) {
		ok = 0;
		goto done;
	}
	rc = g_cuda.cuMemcpyHtoD(g_cuda.batch_offsets_device,
			g_cuda.batch_offsets_host,
			sizeof(int) * (size_t)task_count);
	if (rc != CUDA_SUCCESS) {
		ok = 0;
		goto done;
	}
	rc = g_cuda.cuMemcpyHtoD(g_cuda.batch_lengths_device,
			g_cuda.batch_lengths_host,
			sizeof(int) * (size_t)task_count);
	if (rc != CUDA_SUCCESS) {
		ok = 0;
		goto done;
	}

	{
		void *kernel_params[13];
		kernel_params[0] = &g_cuda.batch_input_device;
		kernel_params[1] = &g_cuda.batch_offsets_device;
		kernel_params[2] = &g_cuda.batch_lengths_device;
		kernel_params[3] = &task_count;
		kernel_params[4] = &g_cuda.gear_matrix_device;
		kernel_params[5] = &destor.chunk_min_size;
		kernel_params[6] = &destor.chunk_max_size;
		kernel_params[7] = &mask;
		kernel_params[8] = &jump_mask;
		kernel_params[9] = &jump_len;
		kernel_params[10] = &warp_window;
		kernel_params[11] = &g_cuda.batch_results_device;
		kernel_params[12] = NULL;

		rc = g_cuda.cuLaunchKernel(g_cuda.jc_kernel,
				(unsigned int)blocks,
				1,
				1,
				(unsigned int)threads_per_block,
				1,
				1,
				0,
				NULL,
				kernel_params,
				NULL);
	}
	if (rc == CUDA_SUCCESS) {
		rc = g_cuda.cuCtxSynchronize();
	}
	if (rc == CUDA_SUCCESS) {
		rc = g_cuda.cuMemcpyDtoH(g_cuda.batch_results_host,
				g_cuda.batch_results_device,
				sizeof(struct fastcdc_gpu_kernel_result) * (size_t)task_count);
	}
	if (rc != CUDA_SUCCESS) {
		ok = 0;
		goto done;
	}

	for (i = 0; i < task_count; i++) {
		if (g_cuda.batch_results_host[i].chunk_size <= 0 || g_cuda.batch_results_host[i].chunk_size > sizes[i]) {
			chunk_sizes[i] = gearjump_chunk_data(buffers[i], sizes[i]);
		} else {
			chunk_sizes[i] = g_cuda.batch_results_host[i].chunk_size;
		}
		fastcdc_gpu_note_kernel_result(g_cuda.batch_results_host + i);
	}

done:
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
