/* The terrain's drawing in hand-written C (native_land.c). */
#pragma once
#include <stddef.h>
#include "cpu.h"

void native_land_init(void);
/* Each does the whole call: the hand-written function, or the original if the option is off */
void native_walk_call(CPU *c);
void native_land_call(CPU *c);
void native_fog_call(CPU *c);

/* ORR_TERRAIN_AB: d3d.c records the calls of the two runs. 1 the original's (draws only recorded), 2 ours */
extern int native_ab_mode;
void native_ab_record(uint32_t kind, const void *p, size_t n);

/* mem.c, for the check: program memory as ranges, and a count of the allocator's changes (its books are not in program memory) */
int mem_ranges(uint32_t *base, uint32_t *size, int max);
extern unsigned long mem_heap_ops;
