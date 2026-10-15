#include <assert.h>
#include <stdio.h>

#include "stv091x_blindscan_algo.h"

static void first_failed_probe_advances_half_scan_span(void)
{
	assert(STV091X_BLINDSCAN_SEARCH_RANGE_HZ == 60000000);
	assert(stv091x_blindscan_failed_frequency(
		       950000, STV091X_BLINDSCAN_SEARCH_RANGE_HZ, 1) ==
	       980000);
}

static void later_failed_probe_advances_full_scan_span(void)
{
	assert(stv091x_blindscan_failed_frequency(
		       980000, STV091X_BLINDSCAN_SEARCH_RANGE_HZ, 0) ==
	       1040000);
}

static void lock_skips_half_occupied_bandwidth(void)
{
	s32 next = stv091x_blindscan_next_frequency(1844890, 1, 29995000);

	assert(next == 1865136);
}

int main(void)
{
	first_failed_probe_advances_half_scan_span();
	later_failed_probe_advances_full_scan_span();
	lock_skips_half_occupied_bandwidth();
	puts("stv091x direct blindscan policy tests passed");
	return 0;
}
