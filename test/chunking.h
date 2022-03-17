void gearjump_init(int chunkSize);
int gearjump_chunk_data(unsigned char *p, int n);

void gear_init(int chunkSize);
int gear_chunk_data(unsigned char *p, int n);

void chunkAlg_init(int chunkSize);
int rabin_chunk_data(unsigned char *p, int n);
int normalized_rabin_chunk_data(unsigned char *p, int n);
int tttd_chunk_data(unsigned char *p, int n);

void rabinJump_init(int chunkSize);
int rabinjump_chunk_data(unsigned char *p, int n);

void ae_init(int chunkSize);
int ae_chunk_data(unsigned char *p, int n);

void leap_init();
int leap_chunk_data(unsigned char *p, int n);