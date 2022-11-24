#include "backup.h"
#include "destor.h"
#include "index/index.h"
#include "jcr.h"
#include "storage/containerstore.h"
#include "utils/sync_queue.h"

/* defined in index.c */
extern struct {
    /* Requests to the key-value store */
    int lookup_requests;
    int update_requests;
    int lookup_requests_for_unique;
    /* Overheads of prefetching module */
    int read_prefetching_units;
} index_overhead;

void do_backup(char *path) {

    double dedup_time = 0;
    init_recipe_store();
    init_container_store();
    init_index();

    init_backup_jcr(path);

    puts("==== backup begin ====");

    TIMER_DECLARE(1);
    TIMER_DECLARE(2);
    TIMER_BEGIN(1);

    time_t start = time(NULL);
    if (destor.simulation_level == SIMULATION_ALL) {
        start_read_trace_phase();
    } else {
        start_read_phase();
        TIMER_BEGIN(2);
        start_chunk_phase();
        start_hash_phase();
    }
    start_dedup_phase();
    start_feature_phase();
    start_simi_phase();
    start_xdelta_phase();

//    start_store_phase();
//  start_rewrite_phase();
//  start_filter_phase();

    do {
        usleep(2000);
        /*time_t now = time(NULL);*/
        fprintf(stderr,
                "job %" PRId32 ", %" PRId64 " bytes, %" PRId32
                " chunks, %d files processed\r",
                jcr.id, jcr.data_size, jcr.chunk_num, jcr.file_num);
    } while (jcr.status == JCR_STATUS_RUNNING || jcr.status != JCR_STATUS_DONE);
    fprintf(stderr,
            "job %" PRId32 ", %" PRId64 " bytes, %" PRId32
            " chunks, %d files processed\n",
            jcr.id, jcr.data_size, jcr.chunk_num, jcr.file_num);

    if (destor.simulation_level == SIMULATION_ALL) {
        stop_read_trace_phase();
    } else {
        stop_read_phase();
        stop_chunk_phase();
        stop_hash_phase();
    }
    stop_dedup_phase();
    if (destor.simulation_level != SIMULATION_ALL) {
        TIMER_END(2, dedup_time);
        printf("\x1B[32mDedup time(s)\e: %.3f\n\x1B[37m", dedup_time / 1000000);
    }
    stop_feature_phase();
    stop_simi_phase();
    stop_xdelta_phase();

    TIMER_END(1, jcr.total_time);

    close_index();
    close_container_store();
    close_recipe_store();

    update_backup_version(jcr.bv);

    free_backup_version(jcr.bv);

    puts("==== backup end ====");

    printf("job id: %" PRId32 "\n", jcr.id);
    printf("backup path: %s\n", jcr.path);
    printf("number of files: %d\n", jcr.file_num);
    printf("number of chunks: %" PRId32 " (%" PRId64 " bytes on average)\n",
           jcr.chunk_num, jcr.data_size / jcr.chunk_num);
    printf("number of unique chunks: %" PRId32 "\n", jcr.unique_chunk_num);
    printf("total size(B): %" PRId64 ", identical size: %" PRId64 "\n",
               jcr.data_size, jcr.total_identical_size);
    printf("stored data size(B): %" PRId64 "\n",
           jcr.unique_data_size + jcr.rewritten_chunk_size);
    printf("featured chunks: %d, similarity chunks: %d\n", jcr.tmp1, jcr.tmp2);
    printf("indentical compress ratio: %.4f, indentical chunk number: %" PRId64 "\n",
           jcr.data_size != 0 ? 
              (jcr.total_identical_size)*100 / (double)(jcr.data_size):0,
           jcr.identical_chunk_num);
    printf("xdelta chunks: %" PRId64 " xdelta compressed chunks: %" PRId64 "\n",
           jcr.total_xdelta_chunk, jcr.total_xdelta_compressed_chunk);
    printf("xdelta compress ratio: %.4f\n",
           jcr.data_size != 0 ? (jcr.total_xdelta_saved_size)*100 /
                                    (double)(jcr.data_size):0);
    printf("deduplication ratio: \x1B[32m%.4f\x1B[37m, %.4f\n",
           jcr.data_size != 0 ? (jcr.total_size_after_dedup)*100 /
                                    (double)(jcr.data_size)
                              : 0,
           jcr.data_size/(double)(jcr.total_size_after_dedup));
    printf("\x1B[32mTotal time(s): %.3f\x1B[37m\n", jcr.total_time / 1000000);
    printf("throughput(MB/s): %.2f\n",
           (double)jcr.data_size * 1000000 / (1024 * 1024 * jcr.total_time));
//    printf("number of zero chunks: %" PRId32 "\n", jcr.zero_chunk_num);
//    printf("size of zero chunks: %" PRId64 "\n", jcr.zero_chunk_size);
//    printf("number of rewritten chunks: %" PRId32 "\n",
//           jcr.rewritten_chunk_num);
//    printf("size of rewritten chunks: %" PRId64 "\n", jcr.rewritten_chunk_size);
//    printf("rewritten rate in size: %.3f\n",
//           jcr.rewritten_chunk_size / (double)jcr.data_size);

    destor.data_size += jcr.data_size;
    destor.stored_data_size += jcr.unique_data_size + jcr.rewritten_chunk_size;

    destor.chunk_num += jcr.chunk_num;
    destor.stored_chunk_num += jcr.unique_chunk_num + jcr.rewritten_chunk_num;
    destor.zero_chunk_num += jcr.zero_chunk_num;
    destor.zero_chunk_size += jcr.zero_chunk_size;
    destor.rewritten_chunk_num += jcr.rewritten_chunk_num;
    destor.rewritten_chunk_size += jcr.rewritten_chunk_size;

    printf("read_time : %.3fs, %.2fMB/s\n", jcr.read_time / 1000000,
           jcr.data_size * 1000000 / jcr.read_time / 1024 / 1024);
    printf("chunk_time : %.3fs, %.2fMB/s\n", jcr.chunk_time / 1000000,
           jcr.data_size * 1000000 / jcr.chunk_time / 1024 / 1024);
    printf("hash_time : %.3fs, %.2fMB/s\n", jcr.hash_time / 1000000,
           jcr.data_size * 1000000 / jcr.hash_time / 1024 / 1024);

    printf("dedup_time : %.3fs, %.2fMB/s\n", jcr.dedup_time / 1000000,
           jcr.data_size * 1000000 / jcr.dedup_time / 1024 / 1024);

//    printf("rewrite_time : %.3fs, %.2fMB/s\n", jcr.rewrite_time / 1000000,
//           jcr.data_size * 1000000 / jcr.rewrite_time / 1024 / 1024);
//
//    printf("filter_time : %.3fs, %.2fMB/s\n", jcr.filter_time / 1000000,
//           jcr.data_size * 1000000 / jcr.filter_time / 1024 / 1024);
//
//    printf("write_time : %.3fs, %.2fMB/s\n", jcr.write_time / 1000000,
//           jcr.data_size * 1000000 / jcr.write_time / 1024 / 1024);

    // double seek_time = 0.005; //5ms
    // double bandwidth = 120 * 1024 * 1024; //120MB/s

    /*	double index_lookup_throughput = jcr.data_size
     / (index_read_times * seek_time
     + index_read_entry_counter * 24 / bandwidth) / 1024 / 1024;

     double write_data_throughput = 1.0 * jcr.data_size * bandwidth
     / (jcr->unique_chunk_num) / 1024 / 1024;
     double index_read_throughput = 1.0 * jcr.data_size / 1024 / 1024
     / (index_read_times * seek_time
     + index_read_entry_counter * 24 / bandwidth);
     double index_write_throughput = 1.0 * jcr.data_size / 1024 / 1024
     / (index_write_times * seek_time
     + index_write_entry_counter * 24 / bandwidth);*/

    /*	double estimated_throughput = write_data_throughput;
     if (estimated_throughput > index_read_throughput)
     estimated_throughput = index_read_throughput;*/
    /*if (estimated_throughput > index_write_throughput)
     estimated_throughput = index_write_throughput;*/

    char logfile[] = "backup.log";
    FILE *fp = fopen(logfile, "a");
    /*
     * job id,
     * the size of backup
     * accumulative consumed capacity,
     * deduplication rate,
     * rewritten rate,
     * total container number,
     * sparse container number,
     * inherited container number,
     * 4 * index overhead (4 * int)
     * throughput,
     */
    fprintf(
        fp,
        "%" PRId32 " %" PRId64 " %" PRId64 " %.4f %.4f %" PRId32 " %" PRId32
        " %" PRId32 " %" PRId32 " %" PRId32 " %" PRId32 " %" PRId32 " %.2f\n",
        jcr.id, jcr.data_size, destor.stored_data_size,
        jcr.data_size != 0 ? (jcr.data_size - jcr.rewritten_chunk_size -
                              jcr.unique_data_size) /
                                 (double)(jcr.data_size)
                           : 0,
        jcr.data_size != 0
            ? (double)(jcr.rewritten_chunk_size) / (double)(jcr.data_size)
            : 0,
        jcr.total_container_num, jcr.sparse_container_num,
        jcr.inherited_sparse_num, index_overhead.lookup_requests,
        index_overhead.lookup_requests_for_unique,
        index_overhead.update_requests, index_overhead.read_prefetching_units,
        (double)jcr.data_size * 1000000 / (1024 * 1024 * jcr.total_time));

    fclose(fp);
}
