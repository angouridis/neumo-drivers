#include <assert.h>
#include <stdio.h>

#include "stv091x_blindscan_algo.h"

static void scan_constants_restore_wide_search_policy(void)
{
	assert(STV091X_BLINDSCAN_STEP_KHZ == 1000);
	assert(STV091X_BLINDSCAN_SEARCH_RANGE_HZ == 60000000);
}

static void first_failed_probe_advances_half_scan_span(void)
{
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

static void lock_moves_next_window_past_carrier(void)
{
	s32 next = stv091x_blindscan_after_lock_frequency(
		1220376, 4798000, STV091X_BLINDSCAN_SEARCH_RANGE_HZ);

	assert(next == 1253614);
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
	scan_constants_restore_wide_search_policy();
	first_failed_probe_advances_half_scan_span();
	later_failed_probe_advances_full_scan_span();
	lock_moves_next_window_past_carrier();
	modcod_classifies_dvb_s2_modulation_family();
	puts("stv091x restored blindscan policy tests passed");
	return 0;
}
