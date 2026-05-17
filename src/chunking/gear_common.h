#ifndef GEAR_COMMON_H_
#define GEAR_COMMON_H_

#include <stdint.h>

extern uint64_t g_gear_matrix[];
extern unsigned long g_condition_mask[];

void gear_matrix_init();

#endif