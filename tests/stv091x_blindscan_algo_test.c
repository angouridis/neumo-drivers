#include <assert.h>
#include <stdio.h>

#include "stv091x_blindscan_algo.h"

static void empty_probe_advances_one_mhz(void)
{
	assert(stv091x_blindscan_next_frequency(950000, 0, 0) == 951000);
}

static void lock_skips_half_occupied_bandwidth(void)
{
	s32 next = stv091x_blindscan_next_frequency(1844890, 1, 29995000);

	assert(next == 1865136);
}

int main(void)
{
	empty_probe_advances_one_mhz();
	lock_skips_half_occupied_bandwidth();
	puts("stv091x direct blindscan policy tests passed");
	return 0;
}
