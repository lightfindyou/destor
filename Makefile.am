bin_PROGRAMS=destor
destor_SOURCES=
destor_LDADD=src/libdestor.a src/index/libindex.a src/storage/libstorage.a src/utils/libutils.a src/recipe/librecipe.a src/chunking/libchunk.a src/fsl/libfsl.a src/featuring/libfeature.a src/similariting/libsimilar.a src/xdelta3/libxdelta.a
# src/featuring/deepsketch/libdeepsketchModel.a src/similariting/deepsketch/libdeepsketchANN.a src/similariting/deepsketch/NGT/libngtstatic.a

SUBDIRS=src src/index src/recipe src/storage src/utils src/chunking src/fsl src/featuring src/similariting src/xdelta3
# src/featuring/deepsketch/ src/similariting/deepsketch/ src/similariting/deepsketch/NGT/

LIBS=-lpthread -lcrypto -lglib -lxxhash -lm

# -ltorch -lnvrtc -lc10 -lcudnn -lcuda -lcudart -lcublas -lcurand -lcusolver
