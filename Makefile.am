bin_PROGRAMS=destor
destor_SOURCES=
# Dummy C++ source to cause C++ linking.
nodist_EXTRA_destor_SOURCES = dummy.cxx
destor_LDADD=src/libdestor.a src/index/libindex.a src/xdelta3/libxdelta.a src/storage/libstorage.a src/utils/libutils.a src/recipe/librecipe.a src/chunking/libchunk.a src/fsl/libfsl.a src/featuring/libfeature.a src/featuring/deepsketch/libdeepsketchfeature.a src/similariting/libsimilar.a src/similariting/deepsketch/libsimilardeepsketch.a src/similariting/deepsketch/NGT/libngt.a

SUBDIRS=src src/index src/recipe src/storage src/utils src/chunking src/fsl src/featuring src/featuring/deepsketch/ src/similariting src/similariting/deepsketch src/similariting/deepsketch/NGT src/xdelta3

LIBS=-lpthread -lcrypto -lglib -lxxhash -lm -lc10 -ltorch -ltorch_cpu

# -ltorch -lnvrtc -lc10 -lcudnn -lcuda -lcudart -lcublas -lcurand -lcusolver
