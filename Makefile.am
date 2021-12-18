#bin_PROGRAMS=destor
lib_LTLIBRARIES = libdestor.la
libdestor_la_SOURCES=
libdestor_la_LIBADD=src/libentrance.la src/index/libindex.la src/storage/libstorage.la src/utils/libutils.la src/recipe/librecipe.la src/chunking/libchunk.la src/fsl/libfsl.la
SUBDIRS=src src/index src/recipe src/storage src/utils src/chunking src/fsl
LIBS=-lpthread -lcrypto -lglib -lm -ldl
