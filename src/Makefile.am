lib_LTLIBRARIES=libentrance.la
include_HEADERS = debug.h
libentrance_la_SOURCES=destor.c jcr.c config.c do_backup.c read_phase.c chunk_phase.c hash_phase.c trace_phase.c dedup_phase.c rewrite_phase.c filter_phase.c cfl_rewrite.c cap_rewrite.c cbr_rewrite.c har_rewrite.c restore_aware.c do_restore.c optimal_restore.c assembly_restore.c cma.c do_delete.c
libentrance_la_LIBADD= index/libindex.la storage/libstorage.la utils/libutils.la recipe/librecipe.la chunking/libchunk.la fsl/libfsl.la
SUBDIRS=index recipe storage utils chunking fsl
LIBS=-lglib -ldl
