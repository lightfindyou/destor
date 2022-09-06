void gearjump_init(int chunkSize);
int gearjump_chunk_data(unsigned char *p, int n);

void normalized_gearjump_init(int chunkSize);
int normalized_gearjump_chunk_data(unsigned char *p, int n);

void gear_init(int chunkSize);
int gear_chunk_data(unsigned char *p, int n);
int TTTD_gear_chunk_data(unsigned char *p, int n);
int gearjumpTTTD_chunk_data(unsigned char *p, int n);

void chunkAlg_init(int chunkSize);
int rabin_chunk_data(unsigned char *p, int n);
int normalized_rabin_chunk_data(unsigned char *p, int n);
int tttd_chunk_data(unsigned char *p, int n);

void rabinJump_init(int chunkSize);
int rabinjump_chunk_data(unsigned char *p, int n);

void ae_init(int chunkSize);
int ae_chunk_data(unsigned char *p, int n);

void leap_init(int chunkSize, int parIdx);
int leap_chunk_data(unsigned char *p, int n);

void fastcdc_init(int chunkSize);
int fastcdc_chunk_data(unsigned char *p, int n);

void rabin_simple_init(int chunkSize);
int rabin_simple_chunk_data(unsigned char *p, int n);