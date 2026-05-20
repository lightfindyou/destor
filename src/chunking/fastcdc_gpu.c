#include "chunking.h"
#include "../destor.h"
#include "gear_common.h"

#include <dlfcn.h>
#include <limits.h>
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
#define FASTCDC_GPU_GEAR_SYMBOL_COUNT 256

typedef CUresult (*cuInit_t)(unsigned int flags);
typedef CUresult (*cuDeviceGetCount_t)(int *count);
typedef CUresult (*cuDeviceGet_t)(CUdevice *device, int ordinal);
typedef CUresult (*cuCtxCreate_t)(CUcontext *pctx, unsigned int flags, CUdevice dev);
typedef CUresult (*cuCtxDestroy_t)(CUcontext ctx);
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
	uint64_t fingerprint_updates;
};

struct cuda_driver_state {
	void *handle;
	CUcontext ctx;
	CUmodule module;
	CUfunction fastcdc_kernel;
	CUdeviceptr gear_matrix_device;
	int initialized;
	int kernel_ready;
	char ptx_path[PATH_MAX];
	cuInit_t cuInit;
	cuDeviceGetCount_t cuDeviceGetCount;
	cuDeviceGet_t cuDeviceGet;
	cuCtxCreate_t cuCtxCreate;
	cuCtxDestroy_t cuCtxDestroy;
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
	g_chunk_experiment_current_checks = result->fingerprint_updates;
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

void chunk_experiment_note_redundancy(int redundant_checks, int warp_group_size) {
	if (!destor.chunk_profile_enabled) {
		return;
	}
	if (redundant_checks > 0) {
		g_chunk_experiment_stats.redundant_checks += (uint64_t)redundant_checks;
	}
	if (warp_group_size > 0) {
		g_chunk_experiment_stats.simulated_warp_groups++;
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

static void fastcdc_gpu_release_driver() {
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

	rc = g_cuda.cuModuleGetFunction(&g_cuda.fastcdc_kernel,
			g_cuda.module,
			FASTCDC_GPU_KERNEL_SYMBOL);
	if (rc != CUDA_SUCCESS || !g_cuda.fastcdc_kernel) {
		WARNING("FastCDC GPU: cuModuleGetFunction failed: %s", fastcdc_cuda_error_string(rc));
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

	g_cuda.kernel_ready = 1;
	NOTICE("Chunk GPU: FastCDC PTX module loaded from %s", g_cuda.ptx_path);
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
	if (cuda_driver_init_context() != 0) {
		return -1;
	}
	return fastcdc_gpu_prepare_kernel();
}

void fastcdc_gpu_close() {
	if (!g_cuda.initialized && !g_cuda.handle) {
		return;
	}
	fastcdc_gpu_release_driver();
}

int fastcdc_gpu_chunk_data(unsigned char *p, int n) {
	CUdeviceptr device_input = 0;
	CUdeviceptr device_result = 0;
	CUresult rc;
	uint64_t mask_s = 0;
	uint64_t mask_l = 0;
	int expect_size = 0;
	int copy_len;
	struct fastcdc_gpu_kernel_result result;
	void *kernel_params[9];

	if (!g_cuda.kernel_ready) {
		return fastcdc_chunk_data(p, n);
	}

	fastcdc_gpu_compute_masks(&mask_s, &mask_l, &expect_size);
	copy_len = n < destor.chunk_max_size ? n : destor.chunk_max_size;
	memset(&result, 0, sizeof(result));

	rc = g_cuda.cuMemAlloc(&device_input, (size_t)copy_len);
	if (rc != CUDA_SUCCESS) {
		WARNING("FastCDC GPU: cuMemAlloc input failed: %s", fastcdc_cuda_error_string(rc));
		return fastcdc_chunk_data(p, n);
	}
	rc = g_cuda.cuMemAlloc(&device_result, sizeof(result));
	if (rc != CUDA_SUCCESS) {
		WARNING("FastCDC GPU: cuMemAlloc result failed: %s", fastcdc_cuda_error_string(rc));
		g_cuda.cuMemFree(device_input);
		return fastcdc_chunk_data(p, n);
	}
	rc = g_cuda.cuMemcpyHtoD(device_input, p, (size_t)copy_len);
	if (rc != CUDA_SUCCESS) {
		WARNING("FastCDC GPU: cuMemcpyHtoD input failed: %s", fastcdc_cuda_error_string(rc));
		g_cuda.cuMemFree(device_result);
		g_cuda.cuMemFree(device_input);
		return fastcdc_chunk_data(p, n);
	}
	rc = g_cuda.cuMemcpyHtoD(device_result, &result, sizeof(result));
	if (rc != CUDA_SUCCESS) {
		WARNING("FastCDC GPU: cuMemcpyHtoD result init failed: %s", fastcdc_cuda_error_string(rc));
		g_cuda.cuMemFree(device_result);
		g_cuda.cuMemFree(device_input);
		return fastcdc_chunk_data(p, n);
	}

	kernel_params[0] = &device_input;
	kernel_params[1] = &copy_len;
	kernel_params[2] = &g_cuda.gear_matrix_device;
	kernel_params[3] = &destor.chunk_min_size;
	kernel_params[4] = &destor.chunk_max_size;
	kernel_params[5] = &expect_size;
	kernel_params[6] = &mask_s;
	kernel_params[7] = &mask_l;
	kernel_params[8] = &device_result;

	rc = g_cuda.cuLaunchKernel(g_cuda.fastcdc_kernel,
			1,
			1,
			1,
			1,
			1,
			1,
			0,
			NULL,
			kernel_params,
			NULL);
	if (rc == CUDA_SUCCESS) {
		rc = g_cuda.cuCtxSynchronize();
	}
	if (rc == CUDA_SUCCESS) {
		rc = g_cuda.cuMemcpyDtoH(&result, device_result, sizeof(result));
	}

	g_cuda.cuMemFree(device_result);
	g_cuda.cuMemFree(device_input);

	if (rc != CUDA_SUCCESS) {
		WARNING("FastCDC GPU: kernel launch failed, falling back to CPU: %s",
				fastcdc_cuda_error_string(rc));
		return fastcdc_chunk_data(p, n);
	}
	if (result.chunk_size <= 0 || result.chunk_size > n) {
		WARNING("FastCDC GPU: invalid kernel chunk size %d, falling back to CPU", result.chunk_size);
		return fastcdc_chunk_data(p, n);
	}

	fastcdc_gpu_note_kernel_result(&result);
	return result.chunk_size;
}

int jc_gpu_init() {
	/* JC still uses CPU chunking, but can reuse a single CUDA context for experiments. */
	return cuda_driver_init_context();
}

void jc_gpu_close() {
	fastcdc_gpu_close();
}

int jc_gpu_chunk_data(unsigned char *p, int n) {
	/* CPU fallback remains the execution path until CUDA kernel is added. */
	return gearjump_chunk_data(p, n);
}
