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
		       10960016, 10960016, 1362000, 10700000,
		       STV091X_BLINDSCAN_SEARCH_RANGE_HZ) == 10962000);
	assert(stv091x_blindscan_after_lock_frequency(
		       1220376, 1220376, 4798000, 950000,
		       STV091X_BLINDSCAN_SEARCH_RANGE_HZ) == 1226000);
	assert(stv091x_blindscan_after_lock_frequency(
		       11230417, 11230417, 41994000, 10700000,
		       STV091X_BLINDSCAN_SEARCH_RANGE_HZ) == 11260000);
}

static void after_lock_always_advances_beyond_probe_frequency(void)
{
	assert(stv091x_blindscan_after_lock_frequency(
		       10998500, 11000000, 0, 10700000,
		       STV091X_BLINDSCAN_SEARCH_RANGE_HZ) == 11002000);
}

static void modcod_classifies_dvb_s2_modulation_family(void)
{
	assert(stv091x_blindscan_modulation_from_modcod(1) ==
	       STV091X_BLINDSCAN_MOD_QPSK);
	assert(stv091x_blindscan_modulation_from_modcod(11) ==
	       STV091X_BLINDSCAN_MOD_QPSK);
	assert(stv091x_blindscan_modulation_from_modcod(12) ==
	       STV091X_BLINDSCAN_MOD_8PSK);
	assert(stv091x_blindscan_modulation_from_modcod(17) ==
	       STV091X_BLINDSCAN_MOD_8PSK);
	assert(stv091x_blindscan_modulation_from_modcod(18) ==
	       STV091X_BLINDSCAN_MOD_16APSK);
	assert(stv091x_blindscan_modulation_from_modcod(23) ==
	       STV091X_BLINDSCAN_MOD_16APSK);
	assert(stv091x_blindscan_modulation_from_modcod(24) ==
	       STV091X_BLINDSCAN_MOD_32APSK);
	assert(stv091x_blindscan_modulation_from_modcod(28) ==
	       STV091X_BLINDSCAN_MOD_32APSK);
	assert(stv091x_blindscan_modulation_from_modcod(0) ==
	       STV091X_BLINDSCAN_MOD_UNKNOWN);
	assert(stv091x_blindscan_modulation_from_modcod(29) ==
	       STV091X_BLINDSCAN_MOD_UNKNOWN);
}

int main(void)
{
	grid_constants_match_blscan2_policy();
	next_grid_frequency_advances_on_band_anchor();
	after_lock_skips_carrier_and_aligns_to_grid();
	after_lock_always_advances_beyond_probe_frequency();
	modcod_classifies_dvb_s2_modulation_family();
	puts("stv091x BLScan2 grid policy tests passed");
	return 0;
}
