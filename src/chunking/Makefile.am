noinst_LIBRARIES=libchunk.a
libchunk_a_SOURCES=rabin_chunking.c rabinjump_chunking.c ae_chunking.c fastcdc_chunking.c gear_common.c gear_chunking.c gearjump_chunking.c fastcdc_gpu.c leap_chunking.c
EXTRA_DIST=fastcdc_gpu_kernel.cu
LIBS= -lm -lcrypto -lssl

FASTCDC_GPU_PTX=fastcdc_gpu_kernel.ptx
FASTCDC_GPU_CU=$(srcdir)/fastcdc_gpu_kernel.cu
NVCC?=nvcc

fastcdc_gpu_ptx: $(FASTCDC_GPU_PTX)

$(FASTCDC_GPU_PTX): $(FASTCDC_GPU_CU)
	$(NVCC) -ptx -o $@ $<
