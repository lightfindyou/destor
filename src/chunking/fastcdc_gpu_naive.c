#include "chunking.h"
#include "../destor.h"
#include "gear_common.h"

#include <dlfcn.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FASTCDC_GPU_NAIVE_KERNEL_SYMBOL "fastcdc_naive_chunk_kernel"
#define GEAR_GPU_NAIVE_KERNEL_SYMBOL "gear_naive_chunk_kernel"
#define JC_GPU_NAIVE_KERNEL_SYMBOL "jc_naive_chunk_kernel"
#define FASTCDC_GPU_GEAR_SYMBOL_COUNT 256
#define FASTCDC_GPU_NAIVE_PROBE_CAP 256

enum gpu_naive_algorithm {
	GPU_NAIVE_ALGO_FASTCDC = 0,
	GPU_NAIVE_ALGO_GEAR = 1,
	GPU_NAIVE_ALGO_JC = 2,
};

typedef int CUresult;
typedef int CUdevice;
typedef struct CUctx_st *CUcontext;
typedef struct CUmod_st *CUmodule;
typedef struct CUfunc_st *CUfunction;
typedef unsigned long long CUdeviceptr;

#define CUDA_SUCCESS 0

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

struct fastcdc_gpu_naive_kernel_result {
	int chunk_size;
};

struct fastcdc_gpu_naive_state {
	void *handle;
	CUcontext ctx;
	CUmodule module;
	CUfunction naive_kernel;
	CUfunction gear_naive_kernel;
	CUfunction jc_naive_kernel;
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

static int g_naive_mode;
static int g_naive_algorithm = GPU_NAIVE_ALGO_FASTCDC;
static int g_naive_probe_cache = -1;
static pthread_key_t g_naive_tls_key;
static pthread_once_t g_naive_tls_once = PTHREAD_ONCE_INIT;

static void fastcdc_gpu_naive_tls_init_key(void) {
	pthread_key_create(&g_naive_tls_key, free);
}

static struct fastcdc_gpu_naive_state *fastcdc_gpu_naive_state(void) {
	struct fastcdc_gpu_naive_state *state;

	pthread_once(&g_naive_tls_once, fastcdc_gpu_naive_tls_init_key);
	state = pthread_getspecific(g_naive_tls_key);
	if (!state) {
		state = calloc(1, sizeof(*state));
		if (!state) {
			return NULL;
		}
		pthread_setspecific(g_naive_tls_key, state);
	}
	return state;
}

int fastcdc_gpu_naive_mode_enabled(void) {
	return g_naive_mode;
}

void fastcdc_gpu_set_naive_mode(int enabled) {
	g_naive_mode = enabled ? 1 : 0;
}

void fastcdc_gpu_set_naive_algorithm(int algorithm) {
	if (algorithm < GPU_NAIVE_ALGO_FASTCDC || algorithm > GPU_NAIVE_ALGO_JC) {
		g_naive_algorithm = GPU_NAIVE_ALGO_FASTCDC;
		return;
	}
	g_naive_algorithm = algorithm;
}

static const char *fastcdc_gpu_naive_error_string(struct fastcdc_gpu_naive_state *state, CUresult code) {
	const char *message = NULL;

	if (state && state->cuGetErrorString) {
		state->cuGetErrorString(code, &message);
	}
	return message ? message : "unknown CUDA error";
}

static void *fastcdc_gpu_naive_dlsym(void *handle, const char *primary, const char *secondary) {
	void *symbol = dlsym(handle, primary);
	if (!symbol && secondary) {
		symbol = dlsym(handle, secondary);
	}
	return symbol;
}

int fastcdc_gpu_naive_probe_max_workers(int file_count) {
	void *handle;
	cuInit_t cuInit_fn;
	cuDeviceGetCount_t cuDeviceGetCount_fn;
	cuDeviceGet_t cuDeviceGet_fn;
	cuCtxCreate_t cuCtxCreate_fn;
	cuCtxDestroy_t cuCtxDestroy_fn;
	CUresult rc;
	CUdevice dev = 0;
	int device_count = 0;
	long cpus;
	int upper;
	int i;
	int created = 0;
	CUcontext contexts[FASTCDC_GPU_NAIVE_PROBE_CAP];

	if (file_count <= 0) {
		return 1;
	}
	if (g_naive_probe_cache > 0) {
		return g_naive_probe_cache > file_count ? file_count : g_naive_probe_cache;
	}

	cpus = sysconf(_SC_NPROCESSORS_ONLN);
	if (cpus < 1) {
		cpus = 1;
	}
	upper = file_count;
	if ((long)upper > cpus) {
		upper = (int)cpus;
	}
	if (upper > FASTCDC_GPU_NAIVE_PROBE_CAP) {
		upper = FASTCDC_GPU_NAIVE_PROBE_CAP;
	}

	handle = dlopen("libcuda.so.1", RTLD_NOW);
	if (!handle) {
		g_naive_probe_cache = 1;
		return 1;
	}

	cuInit_fn = (cuInit_t)dlsym(handle, "cuInit");
	cuDeviceGetCount_fn = (cuDeviceGetCount_t)dlsym(handle, "cuDeviceGetCount");
	cuDeviceGet_fn = (cuDeviceGet_t)dlsym(handle, "cuDeviceGet");
	cuCtxCreate_fn = (cuCtxCreate_t)fastcdc_gpu_naive_dlsym(handle, "cuCtxCreate_v2", "cuCtxCreate");
	cuCtxDestroy_fn = (cuCtxDestroy_t)fastcdc_gpu_naive_dlsym(handle, "cuCtxDestroy_v2", "cuCtxDestroy");
	if (!cuInit_fn || !cuDeviceGetCount_fn || !cuDeviceGet_fn || !cuCtxCreate_fn || !cuCtxDestroy_fn) {
		dlclose(handle);
		g_naive_probe_cache = 1;
		return 1;
	}

	if (cuInit_fn(0) != CUDA_SUCCESS) {
		dlclose(handle);
		g_naive_probe_cache = 1;
		return 1;
	}
	if (cuDeviceGetCount_fn(&device_count) != CUDA_SUCCESS || device_count <= 0) {
		dlclose(handle);
		g_naive_probe_cache = 1;
		return 1;
	}
	if (destor.chunk_gpu_device_id < 0 || destor.chunk_gpu_device_id >= device_count) {
		dlclose(handle);
		g_naive_probe_cache = 1;
		return 1;
	}
	if (cuDeviceGet_fn(&dev, destor.chunk_gpu_device_id) != CUDA_SUCCESS) {
		dlclose(handle);
		g_naive_probe_cache = 1;
		return 1;
	}

	for (i = 0; i < upper; i++) {
		rc = cuCtxCreate_fn(&contexts[i], 0, dev);
		if (rc != CUDA_SUCCESS || !contexts[i]) {
			break;
		}
		created++;
	}
	for (i = 0; i < created; i++) {
		cuCtxDestroy_fn(contexts[i]);
	}
	dlclose(handle);

	if (created < 1) {
		created = 1;
	}
	g_naive_probe_cache = created;
	return created > file_count ? file_count : created;
}

static int fastcdc_gpu_naive_floor_log2(unsigned int value) {
	int index = 0;

	while (value > 1U) {
		value >>= 1U;
		index++;
	}
	return index;
}

static void fastcdc_gpu_naive_compute_masks(uint64_t *mask_s,
		uint64_t *mask_l,
		int *expect_chunk_size) {
	int index = fastcdc_gpu_naive_floor_log2((unsigned int)destor.chunk_avg_size);
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

static int fastcdc_gpu_naive_gear_mask(uint64_t *mask) {
	int index = fastcdc_gpu_naive_floor_log2((unsigned int)destor.chunk_avg_size);

	if (index <= 6 || index >= 17) {
		return -1;
	}
	if (mask) {
		*mask = (uint64_t)g_condition_mask[index];
	}
	return 0;
}

static int fastcdc_gpu_naive_jc_params(uint64_t *mask, uint64_t *jump_mask, int *jump_len) {
	int index = fastcdc_gpu_naive_floor_log2((unsigned int)destor.chunk_avg_size);
	int c_ones = destor.chunk_mask_bits > 0 ? destor.chunk_mask_bits : index - 1;
	int jump_delta = destor.jumpOnes > 0 ? destor.jumpOnes : 1;
	int j_ones = c_ones - jump_delta;
	uint64_t numerator;
	uint64_t denominator;

	if (c_ones <= 1 || c_ones >= 17 || j_ones <= 0 || j_ones >= c_ones) {
		return -1;
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
	return (*jump_len > 0) ? 0 : -1;
}

static int fastcdc_gpu_naive_resolve_ptx_path(char *path, size_t path_size) {
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

static void *fastcdc_gpu_naive_load_symbol(struct fastcdc_gpu_naive_state *state, const char *name) {
	return dlsym(state->handle, name);
}

static void *fastcdc_gpu_naive_load_symbol_any(struct fastcdc_gpu_naive_state *state,
		const char *primary,
		const char *secondary) {
	return fastcdc_gpu_naive_dlsym(state->handle, primary, secondary);
}

static void fastcdc_gpu_naive_release_state(struct fastcdc_gpu_naive_state *state) {
	if (!state) {
		return;
	}
	if (state->gear_matrix_device) {
		state->cuMemFree(state->gear_matrix_device);
		state->gear_matrix_device = 0;
	}
	if (state->module) {
		state->cuModuleUnload(state->module);
		state->module = NULL;
	}
	if (state->ctx) {
		state->cuCtxDestroy(state->ctx);
		state->ctx = NULL;
	}
	if (state->handle) {
		dlclose(state->handle);
		state->handle = NULL;
	}
	state->initialized = 0;
	state->kernel_ready = 0;
	state->naive_kernel = NULL;
	state->gear_naive_kernel = NULL;
	state->jc_naive_kernel = NULL;
}

#define LOAD_NAIVE_CUDA_SYMBOL(state, field, name) \
	do { \
		(state)->field = (name##_t)fastcdc_gpu_naive_load_symbol((state), #name); \
		if (!(state)->field) { \
			WARNING("FastCDC GPU naive: failed to load CUDA symbol %s", #name); \
			fastcdc_gpu_naive_release_state(state); \
			return -1; \
		} \
	} while (0)

#define LOAD_NAIVE_CUDA_SYMBOL_ANY(state, field, type, primary, secondary) \
	do { \
		(state)->field = (type)fastcdc_gpu_naive_load_symbol_any((state), primary, secondary); \
		if (!(state)->field) { \
			WARNING("FastCDC GPU naive: failed to load CUDA symbol %s", primary); \
			fastcdc_gpu_naive_release_state(state); \
			return -1; \
		} \
	} while (0)

static int fastcdc_gpu_naive_prepare_kernel(struct fastcdc_gpu_naive_state *state) {
	CUresult rc;

	if (!state || state->kernel_ready) {
		return state && state->kernel_ready ? 0 : -1;
	}

	if (fastcdc_gpu_naive_resolve_ptx_path(state->ptx_path, sizeof(state->ptx_path)) != 0) {
		WARNING("FastCDC GPU naive: PTX file not found");
		fastcdc_gpu_naive_release_state(state);
		return -1;
	}

	rc = state->cuModuleLoad(&state->module, state->ptx_path);
	if (rc != CUDA_SUCCESS || !state->module) {
		WARNING("FastCDC GPU naive: cuModuleLoad failed for %s: %s",
				state->ptx_path,
				fastcdc_gpu_naive_error_string(state, rc));
		fastcdc_gpu_naive_release_state(state);
		return -1;
	}

	rc = state->cuModuleGetFunction(&state->naive_kernel,
			state->module,
			FASTCDC_GPU_NAIVE_KERNEL_SYMBOL);
	if (rc != CUDA_SUCCESS || !state->naive_kernel) {
		WARNING("FastCDC GPU naive: cuModuleGetFunction(%s) failed: %s",
				FASTCDC_GPU_NAIVE_KERNEL_SYMBOL,
				fastcdc_gpu_naive_error_string(state, rc));
		fastcdc_gpu_naive_release_state(state);
		return -1;
	}
	rc = state->cuModuleGetFunction(&state->gear_naive_kernel,
			state->module,
			GEAR_GPU_NAIVE_KERNEL_SYMBOL);
	if (rc != CUDA_SUCCESS || !state->gear_naive_kernel) {
		WARNING("FastCDC GPU naive: cuModuleGetFunction(%s) failed: %s",
				GEAR_GPU_NAIVE_KERNEL_SYMBOL,
				fastcdc_gpu_naive_error_string(state, rc));
		fastcdc_gpu_naive_release_state(state);
		return -1;
	}
	rc = state->cuModuleGetFunction(&state->jc_naive_kernel,
			state->module,
			JC_GPU_NAIVE_KERNEL_SYMBOL);
	if (rc != CUDA_SUCCESS || !state->jc_naive_kernel) {
		WARNING("FastCDC GPU naive: cuModuleGetFunction(%s) failed: %s",
				JC_GPU_NAIVE_KERNEL_SYMBOL,
				fastcdc_gpu_naive_error_string(state, rc));
		fastcdc_gpu_naive_release_state(state);
		return -1;
	}

	gear_matrix_init();
	rc = state->cuMemAlloc(&state->gear_matrix_device,
			sizeof(g_gear_matrix[0]) * FASTCDC_GPU_GEAR_SYMBOL_COUNT);
	if (rc != CUDA_SUCCESS || !state->gear_matrix_device) {
		WARNING("FastCDC GPU naive: cuMemAlloc for gear matrix failed: %s",
				fastcdc_gpu_naive_error_string(state, rc));
		fastcdc_gpu_naive_release_state(state);
		return -1;
	}

	rc = state->cuMemcpyHtoD(state->gear_matrix_device,
			g_gear_matrix,
			sizeof(g_gear_matrix[0]) * FASTCDC_GPU_GEAR_SYMBOL_COUNT);
	if (rc != CUDA_SUCCESS) {
		WARNING("FastCDC GPU naive: cuMemcpyHtoD for gear matrix failed: %s",
				fastcdc_gpu_naive_error_string(state, rc));
		fastcdc_gpu_naive_release_state(state);
		return -1;
	}

	state->kernel_ready = 1;
	return 0;
}

int fastcdc_gpu_naive_init(void) {
	struct fastcdc_gpu_naive_state *state = fastcdc_gpu_naive_state();
	CUresult rc;
	CUdevice dev = 0;
	int device_count = 0;

	if (!state) {
		return -1;
	}
	if (state->initialized) {
		return state->kernel_ready ? 0 : -1;
	}

	fastcdc_gpu_naive_release_state(state);
	state->handle = dlopen("libcuda.so.1", RTLD_NOW);
	if (!state->handle) {
		WARNING("Chunk GPU naive: CUDA driver not found (libcuda.so.1)");
		return -1;
	}

	LOAD_NAIVE_CUDA_SYMBOL(state, cuInit, cuInit);
	LOAD_NAIVE_CUDA_SYMBOL(state, cuDeviceGetCount, cuDeviceGetCount);
	LOAD_NAIVE_CUDA_SYMBOL(state, cuDeviceGet, cuDeviceGet);
	LOAD_NAIVE_CUDA_SYMBOL_ANY(state, cuCtxCreate, cuCtxCreate_t, "cuCtxCreate_v2", "cuCtxCreate");
	LOAD_NAIVE_CUDA_SYMBOL_ANY(state, cuCtxDestroy, cuCtxDestroy_t, "cuCtxDestroy_v2", "cuCtxDestroy");
	LOAD_NAIVE_CUDA_SYMBOL(state, cuModuleLoad, cuModuleLoad);
	LOAD_NAIVE_CUDA_SYMBOL(state, cuModuleUnload, cuModuleUnload);
	LOAD_NAIVE_CUDA_SYMBOL(state, cuModuleGetFunction, cuModuleGetFunction);
	LOAD_NAIVE_CUDA_SYMBOL_ANY(state, cuMemAlloc, cuMemAlloc_t, "cuMemAlloc_v2", "cuMemAlloc");
	LOAD_NAIVE_CUDA_SYMBOL_ANY(state, cuMemFree, cuMemFree_t, "cuMemFree_v2", "cuMemFree");
	LOAD_NAIVE_CUDA_SYMBOL_ANY(state, cuMemcpyHtoD, cuMemcpyHtoD_t, "cuMemcpyHtoD_v2", "cuMemcpyHtoD");
	LOAD_NAIVE_CUDA_SYMBOL_ANY(state, cuMemcpyDtoH, cuMemcpyDtoH_t, "cuMemcpyDtoH_v2", "cuMemcpyDtoH");
	LOAD_NAIVE_CUDA_SYMBOL(state, cuLaunchKernel, cuLaunchKernel);
	LOAD_NAIVE_CUDA_SYMBOL(state, cuCtxSynchronize, cuCtxSynchronize);
	state->cuGetErrorString = (cuGetErrorString_t)dlsym(state->handle, "cuGetErrorString");

	rc = state->cuInit(0);
	if (rc != CUDA_SUCCESS) {
		WARNING("Chunk GPU naive: cuInit failed: %s", fastcdc_gpu_naive_error_string(state, rc));
		fastcdc_gpu_naive_release_state(state);
		return -1;
	}

	rc = state->cuDeviceGetCount(&device_count);
	if (rc != CUDA_SUCCESS || device_count <= 0) {
		WARNING("Chunk GPU naive: no usable CUDA device found");
		fastcdc_gpu_naive_release_state(state);
		return -1;
	}

	if (destor.chunk_gpu_device_id < 0 || destor.chunk_gpu_device_id >= device_count) {
		WARNING("Chunk GPU naive: device id %d out of range [0, %d)",
				destor.chunk_gpu_device_id, device_count);
		fastcdc_gpu_naive_release_state(state);
		return -1;
	}

	rc = state->cuDeviceGet(&dev, destor.chunk_gpu_device_id);
	if (rc != CUDA_SUCCESS) {
		WARNING("Chunk GPU naive: cuDeviceGet failed: %s", fastcdc_gpu_naive_error_string(state, rc));
		fastcdc_gpu_naive_release_state(state);
		return -1;
	}

	rc = state->cuCtxCreate(&state->ctx, 0, dev);
	if (rc != CUDA_SUCCESS || !state->ctx) {
		WARNING("Chunk GPU naive: cuCtxCreate failed: %s", fastcdc_gpu_naive_error_string(state, rc));
		fastcdc_gpu_naive_release_state(state);
		return -1;
	}

	state->initialized = 1;
	return fastcdc_gpu_naive_prepare_kernel(state);
}

void fastcdc_gpu_naive_close(void) {
	struct fastcdc_gpu_naive_state *state = fastcdc_gpu_naive_state();

	if (!state || (!state->initialized && !state->handle)) {
		return;
	}
	fastcdc_gpu_naive_release_state(state);
}

int fastcdc_gpu_naive_is_ready(void) {
	struct fastcdc_gpu_naive_state *state = fastcdc_gpu_naive_state();

	return state && state->kernel_ready;
}

int fastcdc_gpu_naive_chunk_data(unsigned char *p, int n) {
	struct fastcdc_gpu_naive_state *state = fastcdc_gpu_naive_state();
	CUdeviceptr device_input = 0;
	CUdeviceptr device_result = 0;
	CUresult rc;
	uint64_t mask_s = 0;
	uint64_t mask_l = 0;
	int expect_size = 0;
	int copy_len;
	struct fastcdc_gpu_naive_kernel_result result;
	void *kernel_params[9];

	if (!state || !state->kernel_ready) {
		return fastcdc_chunk_data(p, n);
	}
	if (n <= 0) {
		return -1;
	}

	fastcdc_gpu_naive_compute_masks(&mask_s, &mask_l, &expect_size);
	copy_len = n < destor.chunk_max_size ? n : destor.chunk_max_size;
	memset(&result, 0, sizeof(result));

	rc = state->cuMemAlloc(&device_input, (size_t)copy_len);
	if (rc != CUDA_SUCCESS) {
		WARNING("FastCDC GPU naive: cuMemAlloc input failed: %s",
				fastcdc_gpu_naive_error_string(state, rc));
		return fastcdc_chunk_data(p, n);
	}
	rc = state->cuMemAlloc(&device_result, sizeof(result));
	if (rc != CUDA_SUCCESS) {
		WARNING("FastCDC GPU naive: cuMemAlloc result failed: %s",
				fastcdc_gpu_naive_error_string(state, rc));
		state->cuMemFree(device_input);
		return fastcdc_chunk_data(p, n);
	}
	rc = state->cuMemcpyHtoD(device_input, p, (size_t)copy_len);
	if (rc != CUDA_SUCCESS) {
		WARNING("FastCDC GPU naive: cuMemcpyHtoD input failed: %s",
				fastcdc_gpu_naive_error_string(state, rc));
		state->cuMemFree(device_result);
		state->cuMemFree(device_input);
		return fastcdc_chunk_data(p, n);
	}
	rc = state->cuMemcpyHtoD(device_result, &result, sizeof(result));
	if (rc != CUDA_SUCCESS) {
		WARNING("FastCDC GPU naive: cuMemcpyHtoD result init failed: %s",
				fastcdc_gpu_naive_error_string(state, rc));
		state->cuMemFree(device_result);
		state->cuMemFree(device_input);
		return fastcdc_chunk_data(p, n);
	}

	kernel_params[0] = &device_input;
	kernel_params[1] = &copy_len;
	kernel_params[2] = &state->gear_matrix_device;
	kernel_params[3] = &destor.chunk_min_size;
	kernel_params[4] = &destor.chunk_max_size;
	kernel_params[5] = &expect_size;
	kernel_params[6] = &mask_s;
	kernel_params[7] = &mask_l;
	kernel_params[8] = &device_result;

	rc = state->cuLaunchKernel(state->naive_kernel,
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
		rc = state->cuCtxSynchronize();
	}
	if (rc == CUDA_SUCCESS) {
		rc = state->cuMemcpyDtoH(&result, device_result, sizeof(result));
	}

	state->cuMemFree(device_result);
	state->cuMemFree(device_input);

	if (rc != CUDA_SUCCESS) {
		WARNING("FastCDC GPU naive: kernel launch failed, falling back to CPU: %s",
				fastcdc_gpu_naive_error_string(state, rc));
		return fastcdc_chunk_data(p, n);
	}
	if (result.chunk_size <= 0 || result.chunk_size > n) {
		WARNING("FastCDC GPU naive: invalid kernel chunk size %d, falling back to CPU",
				result.chunk_size);
		return fastcdc_chunk_data(p, n);
	}

	return result.chunk_size;
}

int gear_gpu_naive_chunk_data(unsigned char *p, int n) {
	struct fastcdc_gpu_naive_state *state = fastcdc_gpu_naive_state();
	CUdeviceptr device_input = 0;
	CUdeviceptr device_result = 0;
	CUresult rc;
	uint64_t mask = 0;
	int copy_len;
	struct fastcdc_gpu_naive_kernel_result result;
	void *kernel_params[7];

	if (!state || !state->kernel_ready || !state->gear_naive_kernel) {
		return gear_chunk_data(p, n);
	}
	if (n <= 0) {
		return -1;
	}
	if (fastcdc_gpu_naive_gear_mask(&mask) != 0) {
		return gear_chunk_data(p, n);
	}

	copy_len = n < destor.chunk_max_size ? n : destor.chunk_max_size;
	memset(&result, 0, sizeof(result));

	rc = state->cuMemAlloc(&device_input, (size_t)copy_len);
	if (rc != CUDA_SUCCESS) {
		return gear_chunk_data(p, n);
	}
	rc = state->cuMemAlloc(&device_result, sizeof(result));
	if (rc != CUDA_SUCCESS) {
		state->cuMemFree(device_input);
		return gear_chunk_data(p, n);
	}
	rc = state->cuMemcpyHtoD(device_input, p, (size_t)copy_len);
	if (rc != CUDA_SUCCESS) {
		state->cuMemFree(device_result);
		state->cuMemFree(device_input);
		return gear_chunk_data(p, n);
	}
	rc = state->cuMemcpyHtoD(device_result, &result, sizeof(result));
	if (rc != CUDA_SUCCESS) {
		state->cuMemFree(device_result);
		state->cuMemFree(device_input);
		return gear_chunk_data(p, n);
	}

	kernel_params[0] = &device_input;
	kernel_params[1] = &copy_len;
	kernel_params[2] = &state->gear_matrix_device;
	kernel_params[3] = &destor.chunk_min_size;
	kernel_params[4] = &destor.chunk_max_size;
	kernel_params[5] = &mask;
	kernel_params[6] = &device_result;

	rc = state->cuLaunchKernel(state->gear_naive_kernel,
			1, 1, 1, 1, 1, 1, 0, NULL, kernel_params, NULL);
	if (rc == CUDA_SUCCESS) {
		rc = state->cuCtxSynchronize();
	}
	if (rc == CUDA_SUCCESS) {
		rc = state->cuMemcpyDtoH(&result, device_result, sizeof(result));
	}
	state->cuMemFree(device_result);
	state->cuMemFree(device_input);
	if (rc != CUDA_SUCCESS || result.chunk_size <= 0 || result.chunk_size > n) {
		return gear_chunk_data(p, n);
	}
	return result.chunk_size;
}

int jc_gpu_naive_chunk_data(unsigned char *p, int n) {
	struct fastcdc_gpu_naive_state *state = fastcdc_gpu_naive_state();
	CUdeviceptr device_input = 0;
	CUdeviceptr device_result = 0;
	CUresult rc;
	uint64_t mask = 0;
	uint64_t jump_mask = 0;
	int jump_len = 0;
	int copy_len;
	struct fastcdc_gpu_naive_kernel_result result;
	void *kernel_params[9];

	if (!state || !state->kernel_ready || !state->jc_naive_kernel) {
		return gearjump_chunk_data(p, n);
	}
	if (n <= 0) {
		return -1;
	}
	if (fastcdc_gpu_naive_jc_params(&mask, &jump_mask, &jump_len) != 0) {
		return gearjump_chunk_data(p, n);
	}

	copy_len = n < destor.chunk_max_size ? n : destor.chunk_max_size;
	memset(&result, 0, sizeof(result));

	rc = state->cuMemAlloc(&device_input, (size_t)copy_len);
	if (rc != CUDA_SUCCESS) {
		return gearjump_chunk_data(p, n);
	}
	rc = state->cuMemAlloc(&device_result, sizeof(result));
	if (rc != CUDA_SUCCESS) {
		state->cuMemFree(device_input);
		return gearjump_chunk_data(p, n);
	}
	rc = state->cuMemcpyHtoD(device_input, p, (size_t)copy_len);
	if (rc != CUDA_SUCCESS) {
		state->cuMemFree(device_result);
		state->cuMemFree(device_input);
		return gearjump_chunk_data(p, n);
	}
	rc = state->cuMemcpyHtoD(device_result, &result, sizeof(result));
	if (rc != CUDA_SUCCESS) {
		state->cuMemFree(device_result);
		state->cuMemFree(device_input);
		return gearjump_chunk_data(p, n);
	}

	kernel_params[0] = &device_input;
	kernel_params[1] = &copy_len;
	kernel_params[2] = &state->gear_matrix_device;
	kernel_params[3] = &destor.chunk_min_size;
	kernel_params[4] = &destor.chunk_max_size;
	kernel_params[5] = &mask;
	kernel_params[6] = &jump_mask;
	kernel_params[7] = &jump_len;
	kernel_params[8] = &device_result;

	rc = state->cuLaunchKernel(state->jc_naive_kernel,
			1, 1, 1, 1, 1, 1, 0, NULL, kernel_params, NULL);
	if (rc == CUDA_SUCCESS) {
		rc = state->cuCtxSynchronize();
	}
	if (rc == CUDA_SUCCESS) {
		rc = state->cuMemcpyDtoH(&result, device_result, sizeof(result));
	}
	state->cuMemFree(device_result);
	state->cuMemFree(device_input);
	if (rc != CUDA_SUCCESS || result.chunk_size <= 0 || result.chunk_size > n) {
		return gearjump_chunk_data(p, n);
	}
	return result.chunk_size;
}
