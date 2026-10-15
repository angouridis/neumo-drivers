#include <assert.h>
#include <stdio.h>

#include "stv091x_blindscan_algo.h"

static void grid_constants_match_blscan2_policy(void)
{
	assert(STV091X_BLINDSCAN_STEP_KHZ == 2000);
	assert(STV091X_BLINDSCAN_SEARCH_RANGE_HZ == 2000000);
}

static void next_grid_frequency_advances_on_band_anchor(void)
{
	assert(stv091x_blindscan_next_grid_frequency(10700000, 10700000) ==
	       10702000);
	assert(stv091x_blindscan_next_grid_frequency(10701000, 10700000) ==
	       10702000);
	assert(stv091x_blindscan_next_grid_frequency(10699000, 10700000) ==
	       10700000);
}

static void after_lock_skips_carrier_and_aligns_to_grid(void)
{
	assert(stv091x_blindscan_after_lock_frequency(
		       10960016, 1362000, 10700000,
		       STV091X_BLINDSCAN_SEARCH_RANGE_HZ) == 10962000);
	assert(stv091x_blindscan_after_lock_frequency(
		       1220376, 4798000, 950000,
		       STV091X_BLINDSCAN_SEARCH_RANGE_HZ) == 1226000);
	assert(stv091x_blindscan_after_lock_frequency(
		       11230417, 41994000, 10700000,
		       STV091X_BLINDSCAN_SEARCH_RANGE_HZ) == 11262000);
}

int main(void)
{
	grid_constants_match_blscan2_policy();
	next_grid_frequency_advances_on_band_anchor();
	after_lock_skips_carrier_and_aligns_to_grid();
	puts("stv091x BLScan2 grid policy tests passed");
	return 0;
}
