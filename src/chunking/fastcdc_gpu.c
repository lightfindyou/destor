#include "chunking.h"
#include "../destor.h"

#include <dlfcn.h>
#include <string.h>

typedef int CUresult;
typedef int CUdevice;
typedef struct CUctx_st *CUcontext;

#define CUDA_SUCCESS 0

typedef CUresult (*cuInit_t)(unsigned int flags);
typedef CUresult (*cuDeviceGetCount_t)(int *count);
typedef CUresult (*cuDeviceGet_t)(CUdevice *device, int ordinal);
typedef CUresult (*cuCtxCreate_t)(CUcontext *pctx, unsigned int flags, CUdevice dev);
typedef CUresult (*cuCtxDestroy_t)(CUcontext ctx);
typedef CUresult (*cuGetErrorString_t)(CUresult error, const char **pStr);

struct cuda_driver_state {
	void *handle;
	CUcontext ctx;
	int initialized;
	cuInit_t cuInit;
	cuDeviceGetCount_t cuDeviceGetCount;
	cuDeviceGet_t cuDeviceGet;
	cuCtxCreate_t cuCtxCreate;
	cuCtxDestroy_t cuCtxDestroy;
	cuGetErrorString_t cuGetErrorString;
};

static struct cuda_driver_state g_cuda;

static void fastcdc_gpu_reset_state() {
	memset(&g_cuda, 0, sizeof(g_cuda));
}

static const char *fastcdc_cuda_error_string(CUresult code) {
	const char *msg = NULL;
	if (g_cuda.cuGetErrorString && g_cuda.cuGetErrorString(code, &msg) == CUDA_SUCCESS && msg) {
		return msg;
	}
	return "unknown CUDA error";
}

static void fastcdc_gpu_release_driver() {
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

int fastcdc_gpu_init() {
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
	LOAD_CUDA_SYMBOL(cuCtxCreate);
	LOAD_CUDA_SYMBOL(cuCtxDestroy);
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

void fastcdc_gpu_close() {
	if (!g_cuda.initialized && !g_cuda.handle) {
		return;
	}
	fastcdc_gpu_release_driver();
}

int fastcdc_gpu_chunk_data(unsigned char *p, int n) {
	/* CPU fallback remains the execution path until CUDA kernel is added. */
	return fastcdc_chunk_data(p, n);
}

int jc_gpu_init() {
	/* Reuse the same CUDA driver/context bootstrap as FastCDC GPU path. */
	return fastcdc_gpu_init();
}

void jc_gpu_close() {
	fastcdc_gpu_close();
}

int jc_gpu_chunk_data(unsigned char *p, int n) {
	/* CPU fallback remains the execution path until CUDA kernel is added. */
	return gearjump_chunk_data(p, n);
}
